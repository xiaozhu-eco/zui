#ifndef ZMD_WORKSPACE_H
#define ZMD_WORKSPACE_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "file_list_model.h"

namespace zmd {

/// First free name honouring the prefix, e.g. `未命名.md`, `未命名 2.md`, in @p dir.
std::string unique_name_in(const std::filesystem::path& dir, std::string_view stem,
                           std::string_view ext);

/// The working directory. Everything the app writes lands inside it, and it is
/// the base every relative path (images, assets, exports) resolves against.
class Workspace {
public:
    Workspace() = default;
    explicit Workspace(std::filesystem::path root);

    void set_root(std::filesystem::path root);
    const std::filesystem::path& root() const { return root_; }
    bool valid() const { return !root_.empty(); }

    /// Last path segment, for the tree root and the settings row.
    std::string name() const;
    std::filesystem::path path_of(std::string_view name) const;

    /// The folder the file panel shows, as a path under the root. Empty is the root itself.
    const std::string& folder() const { return folder_; }
    /// Enters @p name (a folder row) - ignored when it is not a folder under the panel's folder.
    bool enter(std::string_view name);
    /// Goes up one level. @return false at the root, which is where the caller keeps the panel.
    bool up();
    /// The folder's own path: root joined with the relative `folder()`.
    std::filesystem::path dir() const;
    /// The panel's title: 文件 at the root, the folder's name inside it.
    std::string folder_name() const;

    /// Lists the shown folder: folders and markdown documents one level deep, folders first.
    void rescan();
    const std::vector<FileEntry>& entries() const { return entries_; }

    /// First free name honouring the prefix, e.g. 未命名.md, 未命名 2.md.
    std::string unique_name(std::string_view stem, std::string_view ext) const;

private:
    std::filesystem::path root_;
    std::string folder_;
    std::vector<FileEntry> entries_;
};

}  // namespace zmd

#endif
