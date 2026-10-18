#include "project_files.h"

#include "template.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace zui_tool {
namespace {

bool write_text(const std::filesystem::path& path, const std::string& body, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << body;
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool inside_project(const std::filesystem::path& root, const std::filesystem::path& abs,
                    const std::string& label, const std::string& rel, std::string& error) {
    std::error_code ec;
    const std::filesystem::path canon_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        error = label + " " + rel + " is outside the project";
        return false;
    }
    const std::filesystem::path canon = std::filesystem::weakly_canonical(abs, ec);
    if (ec) {
        error = "missing " + label + " " + rel;
        return false;
    }
    const std::filesystem::path relative = std::filesystem::relative(canon, canon_root, ec);
    if (ec) {
        error = label + " " + rel + " is outside the project";
        return false;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            error = label + " " + rel + " is outside the project";
            return false;
        }
    }
    return true;
}

bool require_path(const std::filesystem::path& root, const std::string& rel, const char* label,
                  bool allow_directory, std::string& error) {
    if (!valid_rel_path(rel)) {
        error = std::string(label) + " " + rel + " is outside the project";
        return false;
    }
    const std::filesystem::path abs =
        root / std::filesystem::path(rel, std::filesystem::path::generic_format);
    std::error_code ec;
    const bool file = std::filesystem::is_regular_file(abs, ec);
    const bool directory = allow_directory && std::filesystem::is_directory(abs, ec);
    if (!file && !directory) {
        error = "missing " + std::string(label) + " " + rel;
        return false;
    }
    return inside_project(root, abs, label, rel, error);
}

void add_file(std::vector<std::pair<std::string, std::filesystem::path>>& out, const std::string& rel,
              const std::filesystem::path& abs) {
    for (const auto& have : out) {
        if (have.first == rel) {
            return;
        }
    }
    out.emplace_back(rel, abs);
}

bool add_tree(const std::filesystem::path& root, const std::filesystem::path& abs, const std::string& rel,
              std::vector<std::pair<std::string, std::filesystem::path>>& out, std::string& error) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(abs, ec)) {
        if (!inside_project(root, abs, "file", rel, error)) {
            return false;
        }
        add_file(out, rel, abs);
        return true;
    }
    if (!std::filesystem::is_directory(abs, ec)) {
        error = "missing file " + rel;
        return false;
    }
    if (!inside_project(root, abs, "file", rel, error)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(abs, ec)) {
        if (ec) {
            error = "missing file " + rel;
            return false;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path child_rel = std::filesystem::relative(entry.path(), root, ec);
        if (ec) {
            error = "file " + rel + " is outside the project";
            return false;
        }
        const std::string name = child_rel.generic_string();
        if (!valid_rel_path(name)) {
            error = "file " + name + " is outside the project";
            return false;
        }
        if (!inside_project(root, entry.path(), "file", name, error)) {
            return false;
        }
        add_file(out, name, entry.path());
    }
    return !ec;
}

}  // namespace

bool ProjectWriter::read(const std::filesystem::path& file, Project& project, std::string& error) {
    const std::string json = read_text(file);
    if (json.empty()) {
        error = "missing " + file.filename().string();
        return false;
    }
    return parse_project(json, project, error);
}

bool ProjectWriter::write(const std::filesystem::path& dir, const Project& project, std::string& error) {
    if (!valid_project(project, error)) {
        return false;
    }
    if (!write_text(dir / kDeclFile, project_json(project), error) ||
        !write_text(dir / std::filesystem::path(project.entry, std::filesystem::path::generic_format),
                    Templates::entry(project), error) ||
        !write_text(dir / std::filesystem::path(project.page, std::filesystem::path::generic_format),
                    Templates::page(project), error) ||
        !write_text(dir / std::filesystem::path(Templates::app_css_rel(project),
                                                std::filesystem::path::generic_format),
                    Templates::style(project), error) ||
        !write_text(dir / std::filesystem::path(project.icon, std::filesystem::path::generic_format),
                    Templates::icon(project), error) ||
        !write_text(dir / "CMakeLists.txt", Templates::cmake(project), error) ||
        !write_text(dir / "README.md", Templates::readme(project), error) ||
        !write_text(dir / ".gitignore", Templates::gitignore(), error)) {
        return false;
    }
    for (const std::string& rel : project.files) {
        const std::filesystem::path target =
            dir / std::filesystem::path(rel, std::filesystem::path::generic_format);
        if (rel == Templates::app_css_rel(project)) {
            continue;  // the stylesheet is written above
        }
        if (Templates::is_directory_entry(rel)) {
            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            continue;
        }
        if (!write_text(target, Templates::placeholder(rel), error)) {
            return false;
        }
    }
    return true;
}

bool ProjectWriter::require_inputs(const std::filesystem::path& dir, const Project& project, std::string& error) {
    if (!valid_project(project, error)) {
        return false;
    }
    if (!require_path(dir, project.page, "page", false, error) ||
        !require_path(dir, project.icon, "icon", false, error) ||
        !require_path(dir, project.entry, "entry", false, error)) {
        return false;
    }
    const std::filesystem::path page =
        dir / std::filesystem::path(project.page, std::filesystem::path::generic_format);
    std::error_code ec;
    if (std::filesystem::file_size(page, ec) < 8 || ec) {
        error = "page " + project.page + " is empty";
        return false;
    }
    for (const std::string& rel : project.files) {
        if (!require_path(dir, rel, "file", true, error)) {
            return false;
        }
    }
    return true;
}

bool ProjectWriter::collect_runtime_files(const std::filesystem::path& dir, const Project& project,
                           std::vector<std::pair<std::string, std::filesystem::path>>& out,
                           std::string& error) {
    if (project.icon != project.page) {
        const std::filesystem::path icon =
            dir / std::filesystem::path(project.icon, std::filesystem::path::generic_format);
        if (!add_tree(dir, icon, project.icon, out, error)) {
            return false;
        }
    }
    for (const std::string& rel : project.files) {
        if (rel == project.page) {
            continue;
        }
        const std::filesystem::path abs =
            dir / std::filesystem::path(rel, std::filesystem::path::generic_format);
        if (!add_tree(dir, abs, rel, out, error)) {
            return false;
        }
    }
    return true;
}

namespace {

// Every local file name a document refers to: href="…", src="…", and the
// url(…) of a stylesheet. Remote, absolute, and fragment references are not
// the packer's business.
void collect_references(const std::string& text, std::vector<std::string>& out) {
    const char* keys[] = {"href=\"", "src=\"", "url("};
    for (const char* key : keys) {
        for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at)) {
            at += std::strlen(key);
            const std::size_t end =
                key[std::strlen(key) - 1] == '(' ? text.find(')', at) : text.find('"', at);
            if (end == std::string::npos) {
                break;
            }
            std::string value = text.substr(at, end - at);
            if (!value.empty() && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }
            if (value.empty() || value.front() == '#' || value.find("://") != std::string::npos ||
                value.rfind("//", 0) == 0 || value.rfind("data:", 0) == 0 ||
                value.rfind("mailto:", 0) == 0) {
                at = end;
                continue;
            }
            const std::size_t cut = value.find_first_of("?#");
            if (cut != std::string::npos) {
                value.resize(cut);
            }
            if (!value.empty()) {
                out.push_back(value);
            }
            at = end;
        }
    }
}

}  // namespace

std::vector<std::string> ProjectWriter::unshipped_references(const std::filesystem::path& dir,
                                              const Project& project) {
    std::vector<std::string> declared{project.page, project.icon};
    declared.insert(declared.end(), project.files.begin(), project.files.end());
    auto carried = [&declared](const std::string& rel) {
        for (const std::string& have : declared) {
            if (rel == have) {
                return true;
            }
            if (rel.size() > have.size() && rel.compare(0, have.size(), have) == 0 &&
                rel[have.size()] == '/') {
                return true;  // inside a declared directory
            }
        }
        return false;
    };

    // The page and every declared document are read; a stylesheet is read for
    // its url(…) references.
    std::vector<std::string> documents{project.page};
    for (const std::string& rel : project.files) {
        const std::string lowered = rel;
        const std::size_t dot = lowered.find_last_of('.');
        if (dot == std::string::npos) {
            continue;
        }
        const std::string extension = lowered.substr(dot);
        if (extension == ".html" || extension == ".htm" || extension == ".css") {
            documents.push_back(rel);
        }
    }
    std::vector<std::string> missing;
    for (const std::string& document : documents) {
        const std::filesystem::path path =
            dir / std::filesystem::path(document, std::filesystem::path::generic_format);
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        std::vector<std::string> references;
        collect_references(text, references);
        const std::string base = document.substr(0, document.find_last_of('/') + 1);
        for (const std::string& reference : references) {
            const std::filesystem::path resolved =
                std::filesystem::path(base + reference).lexically_normal();
            const std::string rel = resolved.generic_string();
            if (rel.empty() || rel.rfind("..", 0) == 0 || carried(rel)) {
                continue;
            }
            std::error_code ec;
            const bool exists =
                std::filesystem::exists(dir / std::filesystem::path(rel, std::filesystem::path::generic_format), ec);
            const std::string note = document + " refers to " + rel +
                                     (exists ? ", which zui.json does not declare"
                                             : ", which is not in the project");
            if (std::find(missing.begin(), missing.end(), note) == missing.end()) {
                missing.push_back(note);
            }
        }
    }
    return missing;
}

}  // namespace zui_tool
