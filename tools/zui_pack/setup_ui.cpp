#include "setup_ui.h"

#include <algorithm>

namespace zui_tool::setup_ui {
namespace {

using namespace Gdiplus;

const Color kBrand(255, 44, 107, 78);
const Color kBrandHot(255, 53, 121, 90);
const Color kBrandDown(255, 35, 85, 62);
const Color kInk(255, 31, 42, 36);
const Color kBody(255, 85, 96, 90);
const Color kMuted(255, 138, 147, 141);
const Color kBorder(255, 216, 224, 218);
const Color kBlock(255, 241, 244, 241);
const Color kBlockHot(255, 232, 238, 233);
const Color kTrack(255, 232, 236, 233);
const Color kRule(255, 238, 241, 238);
const Color kDanger(255, 179, 64, 47);
const Color kDangerHot(255, 196, 78, 60);
const Color kDangerDown(255, 152, 52, 38);
const Color kWhite(255, 255, 255, 255);
const Color kFocusRing(150, 44, 107, 78);

RectF box(float x, float y, float width, float height, float scale) {
    return RectF(x * scale, y * scale, width * scale, height * scale);
}

void add_round_rect(GraphicsPath& path, const RectF& rect, float radius) {
    const float limit = std::min(rect.Width, rect.Height) / 2.0f;
    const float r = std::max(0.0f, std::min(radius, limit));
    if (r <= 0.0f) {
        path.AddRectangle(rect);
        return;
    }
    const float d = r * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void fill_round_rect(Graphics& graphics, const RectF& rect, float radius, const Color& color) {
    GraphicsPath path;
    add_round_rect(path, rect, radius);
    SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void stroke_round_rect(Graphics& graphics, const RectF& rect, float radius, const Color& color,
                       float width) {
    GraphicsPath path;
    add_round_rect(path, rect, radius);
    Pen pen(color, width);
    graphics.DrawPath(&pen, &path);
}

void draw_line(Graphics& graphics, const std::wstring& text, const Font& font, const Color& color,
               const RectF& rect, StringAlignment align = StringAlignmentNear, bool wrap = false,
               bool path_ellipsis = false) {
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(path_ellipsis ? StringTrimmingEllipsisPath
                                     : StringTrimmingEllipsisCharacter);
    if (!wrap) {
        format.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    SolidBrush brush(color);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

void draw_check(Graphics& graphics, const RectF& rect, const Color& color, float width) {
    PointF points[3] = {
        PointF(rect.X + rect.Width * 0.24f, rect.Y + rect.Height * 0.52f),
        PointF(rect.X + rect.Width * 0.43f, rect.Y + rect.Height * 0.71f),
        PointF(rect.X + rect.Width * 0.77f, rect.Y + rect.Height * 0.30f),
    };
    Pen pen(color, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    graphics.DrawLines(&pen, points, 3);
}

// A check mark or an exclamation mark in a filled circle: how the run ended.
void paint_badge(Graphics& graphics, const RectF& rect, const Color& color, bool check,
                 float scale, const FontFamily* family) {
    SolidBrush brush(color);
    graphics.FillEllipse(&brush, rect);
    if (check) {
        RectF mark = rect;
        mark.Inflate(-rect.Width * 0.24f, -rect.Height * 0.24f);
        draw_check(graphics, mark, kWhite, 3.0f * scale);
        return;
    }
    Font font(family, rect.Height * 0.66f, FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    SolidBrush text(kWhite);
    graphics.DrawString(L"!", 1, &font, rect, &format, &text);
}

void paint_icon(Graphics& graphics, const RectF& rect, Image* icon, const std::wstring& product,
                float scale, const FontFamily* family) {
    if (icon != nullptr) {
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.DrawImage(icon, rect);
        return;
    }
    fill_round_rect(graphics, rect, 11.0f * scale, kBrand);
    wchar_t glyph = product.empty() ? L'Z' : product[0];
    if (glyph >= L'a' && glyph <= L'z') {
        glyph = static_cast<wchar_t>(glyph - L'a' + L'A');
    }
    const std::wstring letter(1, glyph);
    Font font(family, rect.Height * 0.5f, FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    SolidBrush brush(kWhite);
    graphics.DrawString(letter.c_str(), 1, &font, rect, &format, &brush);
}

enum class Button { Primary, Secondary, Danger };

void paint_button(Graphics& graphics, const RectF& rect, Button kind, bool hot, bool pressed,
                  bool focused, const std::wstring& label, const Font& font, float scale) {
    const float radius = 8.0f * scale;
    Color text_color = kWhite;
    switch (kind) {
    case Button::Primary:
        fill_round_rect(graphics, rect, radius, pressed ? kBrandDown : (hot ? kBrandHot : kBrand));
        break;
    case Button::Danger:
        fill_round_rect(graphics, rect, radius, pressed ? kDangerDown : (hot ? kDangerHot : kDanger));
        break;
    case Button::Secondary:
        fill_round_rect(graphics, rect, radius, hot ? kBlockHot : kBlock);
        stroke_round_rect(graphics, rect, radius, kBorder, std::max(1.0f, scale));
        text_color = kInk;
        break;
    }
    if (focused) {
        RectF ring = rect;
        ring.Inflate(3.0f * scale, 3.0f * scale);
        stroke_round_rect(graphics, ring, radius + 3.0f * scale, kFocusRing, 2.0f * scale);
    }
    draw_line(graphics, label, font, text_color, rect, StringAlignmentCenter);
}

// The path field is a rounded white box; the window puts a real EDIT inside.
void paint_field(Graphics& graphics, const RectF& rect, float scale, bool focused) {
    const float radius = 8.0f * scale;
    fill_round_rect(graphics, rect, radius, kWhite);
    stroke_round_rect(graphics, rect, radius, focused ? kBrand : kBorder,
                      (focused ? 2.0f : 1.0f) * scale);
}

void paint_checkbox(Graphics& graphics, const RectF& rect, float scale, bool checked,
                    bool focused) {
    const float radius = 5.0f * scale;
    if (checked) {
        fill_round_rect(graphics, rect, radius, kBrand);
        RectF mark = rect;
        mark.Inflate(-rect.Width * 0.18f, -rect.Height * 0.18f);
        draw_check(graphics, mark, kWhite, 2.0f * scale);
        return;
    }
    fill_round_rect(graphics, rect, radius, kWhite);
    stroke_round_rect(graphics, rect, radius, focused ? kBrand : kBorder, 1.5f * scale);
}

void paint_progress(Graphics& graphics, const RectF& rect, int step, int total) {
    fill_round_rect(graphics, rect, rect.Height / 2.0f, kTrack);
    if (total <= 0 || step <= 0) {
        return;
    }
    const float fraction = std::min(1.0f, static_cast<float>(step) / static_cast<float>(total));
    RectF fill = rect;
    fill.Width = std::max(rect.Height, rect.Width * fraction);
    fill_round_rect(graphics, fill, rect.Height / 2.0f, kBrand);
}

bool is_hot(const Model& model, Item item) { return model.hot == static_cast<int>(item); }

bool is_focused(const Model& model, Item item) { return model.focus == static_cast<int>(item); }

}  // namespace

const Labels& labels(Lang lang) {
    static const Labels english = {
        L"Install to",
        L"Browse\u2026",
        L"Install",
        L"Cancel",
        L"Installing\u2026",
        L"Installation complete",
        L"Installed to",
        L"Run after installation",
        L"Finish",
        L"Close",
        L"Installation failed",
        L"Uninstall",
        L"Uninstall %s from this computer?",
        L"The application files, the Start Menu shortcut, and the uninstall entry are removed.",
        L"Uninstall",
        L"Cancel",
        L"Segoe UI",
    };
    static const Labels chinese = {
        L"\u5b89\u88c5\u5230",
        L"\u6d4f\u89c8\u2026",
        L"\u5b89\u88c5",
        L"\u53d6\u6d88",
        L"\u6b63\u5728\u5b89\u88c5\u2026",
        L"\u5b89\u88c5\u5b8c\u6210",
        L"\u5df2\u5b89\u88c5\u5230",
        L"\u5b89\u88c5\u5b8c\u6210\u540e\u8fd0\u884c",
        L"\u5b8c\u6210",
        L"\u5173\u95ed",
        L"\u5b89\u88c5\u5931\u8d25",
        L"\u5378\u8f7d",
        L"\u8981\u4ece\u8fd9\u53f0\u7535\u8111\u5378\u8f7d %s \u5417\uff1f",
        L"\u5e94\u7528\u6587\u4ef6\u3001\u5f00\u59cb\u83dc\u5355\u5feb\u6377\u65b9\u5f0f\u548c"
        L"\u5378\u8f7d\u9879\u90fd\u4f1a\u88ab\u5220\u9664\u3002",
        L"\u5378\u8f7d",
        L"\u53d6\u6d88",
        L"Microsoft YaHei UI",
    };
    return lang == Lang::Zh ? chinese : english;
}

Lang system_lang() {
    const LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_CHINESE ? Lang::Zh : Lang::En;
}

SizeF client_size(float scale) {
    return SizeF(kClientWidth * scale, kClientHeight * scale);
}

RectF item_rect(Item item, float scale) {
    switch (item) {
    case Item::Path:
        return box(28, 108, 344, 40, scale);
    case Item::Edit:
        return box(40, 119, 320, 20, scale);
    case Item::Browse:
        return box(384, 108, 108, 40, scale);
    case Item::Primary:
        return box(372, 248, 120, 40, scale);
    case Item::Secondary:
        return box(260, 248, 100, 40, scale);
    case Item::RunAfter:
        return box(28, 202, 464, 24, scale);
    }
    return RectF();
}

std::vector<Item> screen_items(Screen screen) {
    switch (screen) {
    case Screen::Install:
        return {Item::Path, Item::Edit, Item::Browse, Item::Primary, Item::Secondary};
    case Screen::Done:
        return {Item::Primary, Item::RunAfter};
    case Screen::Error:
        return {Item::Primary};
    case Screen::Uninstall:
        return {Item::Primary, Item::Secondary};
    case Screen::Progress:
        break;
    }
    return {};
}

void paint(Graphics& graphics, const Model& model, float scale, Image* icon) {
    const Labels& text = labels(model.lang);
    FontFamily requested(text.font_family);
    const FontFamily* family =
        requested.IsAvailable() ? &requested : FontFamily::GenericSansSerif();
    Font heading(family, 20.0f * scale, FontStyleBold, UnitPixel);
    Font subtitle(family, 13.0f * scale, FontStyleRegular, UnitPixel);
    Font body(family, 13.5f * scale, FontStyleRegular, UnitPixel);
    Font lead(family, 15.0f * scale, FontStyleBold, UnitPixel);
    Font button(family, 14.0f * scale, FontStyleRegular, UnitPixel);
    Font caption(family, 12.5f * scale, FontStyleRegular, UnitPixel);

    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const SizeF size = client_size(scale);
    SolidBrush page(kWhite);
    graphics.FillRectangle(&page, RectF(0.0f, 0.0f, size.Width, size.Height));

    const bool done = model.screen == Screen::Done;
    const bool failed = model.screen == Screen::Error;
    const RectF badge = box(28, 22, 44, 44, scale);
    if (done || failed) {
        paint_badge(graphics, badge, done ? kBrand : kDanger, done, scale, family);
    } else {
        paint_icon(graphics, badge, icon, model.product, scale, family);
    }
    const std::wstring title =
        done ? std::wstring(text.done_title)
             : (failed ? std::wstring(text.failed_title) : model.product);
    draw_line(graphics, title, heading, kInk, box(88, 18, 404, 30, scale));
    if (!done && !failed) {
        draw_line(graphics, model.version, subtitle, kMuted, box(88, 48, 404, 20, scale));
    }

    switch (model.screen) {
    case Screen::Install:
        draw_line(graphics, text.install_to, subtitle, kBody, box(28, 86, 464, 18, scale));
        paint_field(graphics, item_rect(Item::Path, scale), scale, is_focused(model, Item::Edit));
        // The window puts an EDIT over this text; painting it as well keeps the
        // empty field from looking empty, and offscreen renders faithful.
        draw_line(graphics, model.destination, body, kInk, box(40, 117, 320, 22, scale),
                  StringAlignmentNear, false, true);
        paint_button(graphics, item_rect(Item::Browse, scale), Button::Secondary,
                     is_hot(model, Item::Browse),
                     model.pressed && is_hot(model, Item::Browse),
                     is_focused(model, Item::Browse), text.browse, button, scale);
        break;
    case Screen::Progress:
        draw_line(graphics, text.installing, body, kInk, box(28, 92, 464, 20, scale));
        paint_progress(graphics, box(28, 124, 464, 8, scale), model.step, model.total);
        draw_line(graphics, model.status, caption, kMuted, box(28, 140, 464, 18, scale));
        break;
    case Screen::Done:
        draw_line(graphics, text.installed_to, subtitle, kBody, box(28, 96, 464, 18, scale));
        draw_line(graphics, model.destination, body, kInk, box(28, 116, 464, 20, scale),
                  StringAlignmentNear, false, true);
        paint_checkbox(graphics, box(28, 208, 18, 18, scale), scale, model.run_after,
                       is_focused(model, Item::RunAfter));
        draw_line(graphics, text.run_after, body, kBody, box(54, 206, 438, 22, scale));
        break;
    case Screen::Error:
        draw_line(graphics, model.message, body, kBody, box(28, 96, 464, 64, scale),
                  StringAlignmentNear, true);
        break;
    case Screen::Uninstall:
        draw_line(graphics, model.status, lead, kInk, box(28, 92, 464, 24, scale),
                  StringAlignmentNear, true);
        draw_line(graphics, model.destination, caption, kMuted, box(28, 122, 464, 18, scale),
                  StringAlignmentNear, false, true);
        draw_line(graphics, text.uninstall_note, caption, kMuted, box(28, 144, 464, 36, scale),
                  StringAlignmentNear, true);
        break;
    }

    if (screen_items(model.screen).empty()) {
        return;
    }
    Pen rule(kRule, std::max(1.0f, scale));
    graphics.DrawLine(&rule, PointF(28.0f * scale, 228.0f * scale),
                      PointF(492.0f * scale, 228.0f * scale));
    if (model.screen == Screen::Error) {
        paint_button(graphics, item_rect(Item::Primary, scale), Button::Secondary,
                     is_hot(model, Item::Primary),
                     model.pressed && is_hot(model, Item::Primary),
                     is_focused(model, Item::Primary), text.close, button, scale);
        return;
    }
    const bool uninstall = model.screen == Screen::Uninstall;
    const std::wstring primary =
        uninstall ? std::wstring(text.uninstall)
                  : (done ? std::wstring(text.finish) : std::wstring(text.install));
    paint_button(graphics, item_rect(Item::Primary, scale),
                 uninstall ? Button::Danger : Button::Primary, is_hot(model, Item::Primary),
                 model.pressed && is_hot(model, Item::Primary),
                 is_focused(model, Item::Primary), primary, button, scale);
    if (!done) {
        const std::wstring secondary = uninstall ? text.keep : text.cancel;
        paint_button(graphics, item_rect(Item::Secondary, scale), Button::Secondary,
                     is_hot(model, Item::Secondary),
                     model.pressed && is_hot(model, Item::Secondary),
                     is_focused(model, Item::Secondary), secondary, button, scale);
    }
}

}  // namespace zui_tool::setup_ui
