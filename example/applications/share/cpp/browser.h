#ifndef SHARE_BROWSER_H
#define SHARE_BROWSER_H

#include "util.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace share {

/// The directory being browsed, its entries, and the selection. It owns no `zui::App` state, so
/// the listing rules can be tested on a scratch directory.
///
/// An empty directory means the platform root chooser (drives / volumes); that is what
/// `browse_roots_label()` names.
class FileBrowser {
public:
    explicit FileBrowser(std::string dir) : dir_(std::move(dir)) {}

    const std::string& directory() const { return dir_; }
    const std::vector<DirEnt>& entries() const { return entries_; }
    const std::set<std::string>& selection() const { return selected_; }
    const std::string& error() const { return error_; }

    /// What the toolbar shows: the roots label while the directory is empty, else the path.
    std::string label() const;

    /// Re-reads the directory after it changed, or when @p force is set.
    void refresh(bool force = false);

    /// Moves to @p path. False when it is not a readable directory (the listing stays put).
    bool open(const std::string& path);
    /// Moves to the parent. False at the platform root.
    bool up();

    bool is_selected(const std::string& path) const { return selected_.count(path) != 0; }
    void toggle(const std::string& path);
    void clear_selection() { selected_.clear(); }

    std::size_t selected_count() const { return selected_.size(); }
    /// Bytes of the selected *files* (folders count as 0, like the source app).
    std::uint64_t selected_bytes() const;
    /// "已选 3 项 · 189.6 MB" / "已选 0 项".
    std::string selection_label() const;
    /// Everything to send: folders expand recursively, with the wire-relative names.
    std::vector<ShareFile> expand() const;

    /// Drops selection entries that are no longer in the listing.
    void prune_selection();

private:
    std::string dir_;
    std::string listed_;  ///< the directory `entries_` was read from
    std::vector<DirEnt> entries_;
    std::set<std::string> selected_;
    std::string error_;
};

}  // namespace share

#endif  // SHARE_BROWSER_H
