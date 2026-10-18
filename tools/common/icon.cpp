#include "icon.h"

#include <fstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace zui_tool {
namespace {

struct Paint {
    bool from_file = false;
    std::wstring file;
    Gdiplus::Color background{255, 44, 107, 78};
    Gdiplus::Color foreground{255, 244, 247, 242};
    std::wstring glyph;
};

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse_hex_color(const std::string& text, std::size_t pos, Gdiplus::Color& out) {
    if (pos + 7 > text.size() || text[pos] != '#') {
        return false;
    }
    int value = 0;
    for (int i = 1; i <= 6; ++i) {
        const int nibble = hex_nibble(text[pos + static_cast<std::size_t>(i)]);
        if (nibble < 0) {
            return false;
        }
        value = (value << 4) | nibble;
    }
    out = Gdiplus::Color(255, (value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
    return true;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string trim_copy(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                             text.back() == '\t')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) {
        ++start;
    }
    return text.substr(start);
}

// The scaffold's icon is a rounded tile with a text glyph. Anything else
// cannot be drawn here, and pretending it can would ship a flat rectangle, so
// a shape this function does not understand is reported instead.
bool parse_svg(const std::filesystem::path& path, Paint& paint) {
    const std::string xml = read_file(path);
    if (xml.empty()) {
        return false;
    }
    bool background = false;
    bool glyph = false;
    const auto fill = xml.find("fill=\"#");
    if (fill != std::string::npos) {
        background = parse_hex_color(xml, fill + 6, paint.background);
    }
    const auto text = xml.find("<text");
    if (text != std::string::npos) {
        const auto text_fill = xml.find("fill=\"#", text);
        const auto close = xml.find('>', text);
        if (text_fill != std::string::npos && (close == std::string::npos || text_fill < close)) {
            parse_hex_color(xml, text_fill + 6, paint.foreground);
        }
        if (close != std::string::npos) {
            const auto end = xml.find("</text>", close);
            if (end != std::string::npos) {
                paint.glyph = utf8_to_wide(trim_copy(xml.substr(close + 1, end - close - 1)));
                glyph = !paint.glyph.empty();
            }
        }
    }
    return background && glyph;
}

std::filesystem::path brand_png(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> candidates;
    if (!root.empty()) {
        candidates.push_back(root / "resources" / "brand" / "bamboo" / "app-icon.png");
    }
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
        const std::filesystem::path exe_dir = std::filesystem::path(buf).parent_path();
        candidates.push_back(exe_dir / ".." / ".." / ".." / "resources" / "brand" / "bamboo" /
                             "app-icon.png");
    }
    for (const auto& candidate : candidates) {
        std::error_code ec;
        const auto normal = candidate.lexically_normal();
        if (std::filesystem::is_regular_file(normal, ec)) {
            return normal;
        }
    }
    return {};
}

Paint choose_paint(const std::filesystem::path& project, const std::filesystem::path& root,
                   const std::string& icon_rel, std::string* error) {
    Paint paint;
    std::error_code ec;
    if (!icon_rel.empty()) {
        const std::filesystem::path declared =
            project / std::filesystem::path(icon_rel, std::filesystem::path::generic_format);
        const std::string ext = declared.extension().string();
        if ((ext == ".png" || ext == ".PNG") && std::filesystem::is_regular_file(declared, ec)) {
            paint.from_file = true;
            paint.file = declared.wstring();
            return paint;
        }
        if ((ext == ".svg" || ext == ".SVG") && std::filesystem::is_regular_file(declared, ec) &&
            parse_svg(declared, paint)) {
            return paint;
        }
        if (std::filesystem::is_regular_file(declared, ec) && error != nullptr) {
            *error = "cannot draw " + icon_rel +
                     "; a .png is drawn exactly, a .svg only as a tile with a text glyph";
        }
    }
    const std::filesystem::path png = project / "icon.png";
    if (std::filesystem::is_regular_file(png, ec)) {
        paint.from_file = true;
        paint.file = png.wstring();
        return paint;
    }
    const std::filesystem::path svg = project / "icon.svg";
    if (std::filesystem::is_regular_file(svg, ec) && parse_svg(svg, paint)) {
        return paint;
    }
    const std::filesystem::path brand = brand_png(root);
    if (!brand.empty()) {
        paint.from_file = true;
        paint.file = brand.wstring();
    }
    return paint;
}

bool font_available(const wchar_t* name) {
    Gdiplus::FontFamily family(name);
    return family.IsAvailable() == TRUE;
}

const wchar_t* glyph_font() {
    if (font_available(L"SimSun")) {
        return L"SimSun";
    }
    if (font_available(L"Microsoft YaHei")) {
        return L"Microsoft YaHei";
    }
    return L"Segoe UI";
}

void add_round_rect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    const float d = r * 2.f;
    path.AddArc(x, y, d, d, 180.f, 90.f);
    path.AddArc(x + w - d, y, d, d, 270.f, 90.f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.f, 90.f);
    path.AddArc(x, y + h - d, d, d, 90.f, 90.f);
    path.CloseFigure();
}

bool draw_icon(Gdiplus::Bitmap& bitmap, const Paint& paint) {
    const int size = static_cast<int>(bitmap.GetWidth());
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    if (paint.from_file) {
        Gdiplus::Bitmap source(paint.file.c_str());
        if (source.GetLastStatus() != Gdiplus::Ok) {
            return false;
        }
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(&source, 0, 0, size, size);
        return graphics.GetLastStatus() == Gdiplus::Ok;
    }
    const float radius = static_cast<float>(size) * 14.f / 64.f;
    Gdiplus::GraphicsPath shape;
    add_round_rect(shape, 0.f, 0.f, static_cast<float>(size), static_cast<float>(size), radius);
    Gdiplus::SolidBrush background(paint.background);
    graphics.FillPath(&background, &shape);
    if (!paint.glyph.empty()) {
        Gdiplus::FontFamily family(glyph_font());
        const float px = static_cast<float>(size) * 34.f / 64.f;
        Gdiplus::Font font(&family, px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush foreground(paint.foreground);
        const Gdiplus::RectF box(0.f, static_cast<float>(size) * 0.04f, static_cast<float>(size),
                                 static_cast<float>(size));
        graphics.DrawString(paint.glyph.c_str(), -1, &font, box, &format, &foreground);
    }
    return graphics.GetLastStatus() == Gdiplus::Ok;
}

int encoder_clsid(const wchar_t* mime, CLSID* clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) {
        return -1;
    }
    std::vector<std::uint8_t> storage(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) {
        return -1;
    }
    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool save_png(Gdiplus::Bitmap& bitmap, const std::filesystem::path& dest) {
    CLSID clsid{};
    if (encoder_clsid(L"image/png", &clsid) < 0) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    return bitmap.Save(dest.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

void append_u16(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

std::vector<std::uint8_t> dib_32(Gdiplus::Bitmap& bitmap) {
    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    Gdiplus::BitmapData data{};
    const Gdiplus::Rect rect(0, 0, width, height);
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) !=
        Gdiplus::Ok) {
        return {};
    }
    const int and_stride = ((width + 31) / 32) * 4;
    const int xor_bytes = width * height * 4;
    const int and_bytes = and_stride * height;
    std::vector<std::uint8_t> out(40 + static_cast<std::size_t>(xor_bytes + and_bytes), 0);
    out[0] = 40;
    auto put32 = [&](int offset, int value) {
        const auto bits = static_cast<unsigned>(value);
        out[static_cast<std::size_t>(offset)] = static_cast<std::uint8_t>(bits & 0xff);
        out[static_cast<std::size_t>(offset + 1)] = static_cast<std::uint8_t>((bits >> 8) & 0xff);
        out[static_cast<std::size_t>(offset + 2)] = static_cast<std::uint8_t>((bits >> 16) & 0xff);
        out[static_cast<std::size_t>(offset + 3)] = static_cast<std::uint8_t>((bits >> 24) & 0xff);
    };
    put32(4, width);
    put32(8, height * 2);
    out[12] = 1;
    out[13] = 0;
    out[14] = 32;
    out[15] = 0;
    put32(20, xor_bytes);
    auto* pixels = reinterpret_cast<std::uint8_t*>(data.Scan0);
    const int stride = data.Stride;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = pixels + static_cast<std::ptrdiff_t>(y) * stride;
        std::uint8_t* dest =
            out.data() + 40 + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4;
        std::memcpy(dest, src, static_cast<std::size_t>(width) * 4);
    }
    bitmap.UnlockBits(&data);
    return out;
}

void put_u16(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void put_u32(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void set_u16(std::vector<std::uint8_t>& out, std::size_t at, std::size_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xff);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void pad4(std::vector<std::uint8_t>& out) {
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

void put_utf16(std::vector<std::uint8_t>& out, const std::wstring& text) {
    for (wchar_t c : text) {
        put_u16(out, static_cast<unsigned>(c));
    }
}

// One String entry: the key, then the value, both UTF-16 and NUL terminated.
std::vector<std::uint8_t> version_string(const std::wstring& key, const std::wstring& value) {
    std::vector<std::uint8_t> out;
    put_u16(out, 0);                                            // wLength, fixed up below
    put_u16(out, static_cast<unsigned>(value.size() + 1));      // wValueLength counts the NUL
    put_u16(out, 1);                                            // wType: text
    put_utf16(out, key);
    put_u16(out, 0);
    pad4(out);
    put_utf16(out, value);
    put_u16(out, 0);
    pad4(out);
    set_u16(out, 0, out.size());
    return out;
}

std::vector<std::uint8_t> version_string_table(
    const std::vector<std::pair<std::wstring, std::wstring>>& entries) {
    std::vector<std::uint8_t> out;
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 1);
    put_utf16(out, L"040904B0");  // English (United States), Unicode
    put_u16(out, 0);
    pad4(out);
    for (const auto& entry : entries) {
        const std::vector<std::uint8_t> item = version_string(entry.first, entry.second);
        out.insert(out.end(), item.begin(), item.end());
    }
    set_u16(out, 0, out.size());
    return out;
}

// The four numbers of a dotted version, missing parts zero.
void version_parts(const std::string& version, unsigned parts[4]) {
    parts[0] = parts[1] = parts[2] = parts[3] = 0;
    int index = 0;
    unsigned value = 0;
    bool any = false;
    for (std::size_t i = 0; i <= version.size() && index < 4; ++i) {
        if (i == version.size() || version[i] == '.') {
            if (any) {
                parts[index] = value;
            }
            ++index;
            value = 0;
            any = false;
        } else if (version[i] >= '0' && version[i] <= '9') {
            any = true;
            value = value * 10 + static_cast<unsigned>(version[i] - '0');
            if (value > 65535) {
                value = 65535;
            }
        }
    }
}

// The whole VS_VERSIONINFO blob, ready for UpdateResource(RT_VERSION).
std::vector<std::uint8_t> version_blob(const ExeMetadata& metadata) {
    std::vector<std::pair<std::wstring, std::wstring>> entries;
    auto add = [&entries](const char* key, const std::string& value) {
        if (!value.empty()) {
            entries.emplace_back(utf8_to_wide(key), utf8_to_wide(value));
        }
    };
    add("CompanyName", metadata.company);
    add("FileDescription", metadata.description.empty() ? metadata.product : metadata.description);
    add("FileVersion", metadata.version);
    add("InternalName", metadata.product);
    add("LegalCopyright", metadata.copyright);
    add("OriginalFilename", metadata.original_filename);
    add("ProductName", metadata.product);
    add("ProductVersion", metadata.version);
    const std::vector<std::uint8_t> table = version_string_table(entries);

    std::vector<std::uint8_t> strings;
    put_u16(strings, 0);
    put_u16(strings, 0);
    put_u16(strings, 1);
    put_utf16(strings, L"StringFileInfo");
    put_u16(strings, 0);
    pad4(strings);
    strings.insert(strings.end(), table.begin(), table.end());
    set_u16(strings, 0, strings.size());

    std::vector<std::uint8_t> translation;
    put_u16(translation, 0);
    put_u16(translation, 4);
    put_u16(translation, 0);
    put_utf16(translation, L"Translation");
    put_u16(translation, 0);
    pad4(translation);
    put_u32(translation, 0x040904B0);
    set_u16(translation, 0, translation.size());

    std::vector<std::uint8_t> variables;
    put_u16(variables, 0);
    put_u16(variables, 0);
    put_u16(variables, 1);
    put_utf16(variables, L"VarFileInfo");
    put_u16(variables, 0);
    pad4(variables);
    variables.insert(variables.end(), translation.begin(), translation.end());
    set_u16(variables, 0, variables.size());

    unsigned parts[4] = {0, 0, 0, 0};
    version_parts(metadata.version, parts);
    std::vector<std::uint8_t> fixed;
    put_u32(fixed, 0xFEEF04BD);                    // dwSignature
    put_u32(fixed, 0x00010000);                    // dwStrucVersion
    put_u32(fixed, (parts[0] << 16) | parts[1]);   // dwFileVersionMS
    put_u32(fixed, (parts[2] << 16) | parts[3]);   // dwFileVersionLS
    put_u32(fixed, (parts[0] << 16) | parts[1]);   // dwProductVersionMS
    put_u32(fixed, (parts[2] << 16) | parts[3]);   // dwProductVersionLS
    put_u32(fixed, 0x3F);                          // dwFileFlagsMask
    put_u32(fixed, 0);                             // dwFileFlags
    put_u32(fixed, 0x00040004);                    // dwFileOS: VOS_NT_WINDOWS32
    put_u32(fixed, 0x00000001);                    // dwFileType: VFT_APP
    put_u32(fixed, 0);                             // dwFileSubtype
    put_u32(fixed, 0);                             // dwFileDateMS
    put_u32(fixed, 0);                             // dwFileDateLS

    std::vector<std::uint8_t> out;
    put_u16(out, 0);
    put_u16(out, fixed.size());
    put_u16(out, 0);
    put_utf16(out, L"VS_VERSION_INFO");
    put_u16(out, 0);
    pad4(out);
    out.insert(out.end(), fixed.begin(), fixed.end());
    pad4(out);
    out.insert(out.end(), strings.begin(), strings.end());
    out.insert(out.end(), variables.begin(), variables.end());
    set_u16(out, 0, out.size());
    return out;
}

struct GdiSession {
    ULONG_PTR token = 0;
    bool open = false;

    GdiSession() {
        Gdiplus::GdiplusStartupInput input;
        open = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    ~GdiSession() {
        if (open) {
            Gdiplus::GdiplusShutdown(token);
        }
    }
    GdiSession(const GdiSession&) = delete;
    GdiSession& operator=(const GdiSession&) = delete;
};

bool render_bitmap(int size, const Paint& paint, Gdiplus::Bitmap& bitmap) {
    (void)size;
    return draw_icon(bitmap, paint);
}

bool write_png_size(const std::filesystem::path& dest, int size, const Paint& paint) {
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    if (!render_bitmap(size, paint, bitmap)) {
        return false;
    }
    return save_png(bitmap, dest);
}

}  // namespace

bool write_launcher_icons(const std::filesystem::path& res_dir, const std::filesystem::path& project,
                          const std::filesystem::path& root, const std::string& icon_rel,
                          std::string& error) {
    GdiSession session;
    if (!session.open) {
        error = "cannot start GDI+";
        return false;
    }
    std::string icon_error;
    const Paint paint = choose_paint(project, root, icon_rel, &icon_error);
    if (!icon_error.empty()) {
        error = icon_error;
        return false;
    }
    const struct {
        const char* dir;
        int size;
    } sizes[] = {
        {"mipmap-mdpi", 48},
        {"mipmap-hdpi", 72},
        {"mipmap-xhdpi", 96},
        {"mipmap-xxhdpi", 144},
        {"mipmap-xxxhdpi", 192},
    };
    for (const auto& size : sizes) {
        const std::filesystem::path dest = res_dir / size.dir / "ic_launcher.png";
        if (!write_png_size(dest, size.size, paint)) {
            error = "cannot write " + dest.string();
            return false;
        }
    }
    return true;
}

bool stamp_exe_bytes(std::vector<std::uint8_t>& bytes, const std::filesystem::path& project,
                     const std::filesystem::path& root, const std::string& icon_rel,
                     const ExeMetadata& metadata, std::string& error) {
    if (bytes.size() < 2 || bytes[0] != 'M' || bytes[1] != 'Z') {
        error = "executable icon source is not a PE image";
        return false;
    }
    GdiSession session;
    if (!session.open) {
        error = "cannot start GDI+";
        return false;
    }
    std::string icon_error;
    const Paint paint = choose_paint(project, root, icon_rel, &icon_error);
    if (!icon_error.empty()) {
        error = icon_error;
        return false;
    }
    const int sizes[] = {16, 32, 48, 256};
    std::vector<std::vector<std::uint8_t>> images;
    images.reserve(4);
    for (int size : sizes) {
        Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
        if (!render_bitmap(size, paint, bitmap)) {
            error = "cannot draw the executable icon";
            return false;
        }
        std::vector<std::uint8_t> dib = dib_32(bitmap);
        if (dib.size() < 40) {
            error = "cannot draw the executable icon";
            return false;
        }
        images.push_back(std::move(dib));
    }

    std::vector<std::uint8_t> group;
    append_u16(group, 0);
    append_u16(group, 1);
    append_u16(group, static_cast<unsigned>(images.size()));
    for (std::size_t i = 0; i < images.size(); ++i) {
        const int side = sizes[i] >= 256 ? 0 : sizes[i];
        group.push_back(static_cast<std::uint8_t>(side));
        group.push_back(static_cast<std::uint8_t>(side));
        group.push_back(0);
        group.push_back(0);
        append_u16(group, 1);
        append_u16(group, 32);
        append_u32(group, static_cast<unsigned>(images[i].size()));
        append_u16(group, static_cast<unsigned>(i + 1));
    }

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "zui-pack-icon" / "stamp.exe";
    std::error_code ec;
    std::filesystem::create_directories(tmp.parent_path(), ec);
    std::filesystem::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "cannot write a temporary executable for the icon";
            return false;
        }
    }
    HANDLE update = BeginUpdateResourceW(tmp.c_str(), FALSE);
    if (update == nullptr) {
        error = "cannot open the executable for an icon resource";
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < images.size(); ++i) {
        // RT_ICON is 3. The A macro is char* when UNICODE is off.
        if (UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(i + 1),
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), images[i].data(),
                            static_cast<DWORD>(images[i].size())) == FALSE) {
            ok = false;
        }
    }
    // RT_GROUP_ICON is 14.
    if (UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1),
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), group.data(),
                        static_cast<DWORD>(group.size())) == FALSE) {
        ok = false;
    }
    // RT_VERSION is 16: what Properties shows for the file.
    const std::vector<std::uint8_t> version = version_blob(metadata);
    if (!version.empty() &&
        UpdateResourceW(update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1),
                        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                        const_cast<std::uint8_t*>(version.data()),
                        static_cast<DWORD>(version.size())) == FALSE) {
        ok = false;
    }
    if (EndUpdateResourceW(update, ok ? FALSE : TRUE) == FALSE) {
        ok = false;
    }
    if (!ok) {
        error = "cannot write the executable icon resource";
        return false;
    }
    std::ifstream in(tmp, std::ios::binary);
    std::vector<std::uint8_t> stamped((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
    if (stamped.size() < bytes.size() || stamped[0] != 'M' || stamped[1] != 'Z') {
        error = "icon resource update dropped the executable";
        return false;
    }
    bytes.swap(stamped);
    return true;
}

}  // namespace zui_tool

#else

namespace zui_tool {

bool stamp_exe_bytes(std::vector<std::uint8_t>&, const std::filesystem::path&,
                     const std::filesystem::path&, const std::string&, const ExeMetadata&,
                     std::string&) {
    return true;
}

bool write_launcher_icons(const std::filesystem::path&, const std::filesystem::path&,
                          const std::filesystem::path&, const std::string&, std::string& error) {
    error = "launcher icons are written on Windows";
    return false;
}

}  // namespace zui_tool

#endif
