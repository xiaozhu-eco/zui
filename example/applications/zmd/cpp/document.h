#ifndef ZMD_DOCUMENT_H
#define ZMD_DOCUMENT_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zmd {

/// The open document: text, where it lives, whether it is dirty, its save state
/// and its undo history. No UI, no zui types.
class Document {
public:
    enum class Save { Saved, Typing, Error };

    void reset(std::string title, std::string source, std::filesystem::path path);
    /// Replaces the text; the caller says whether that counts as a user edit.
    void set_source(std::string source, bool edited);

    const std::string& title() const { return title_; }
    const std::string& source() const { return source_; }
    const std::filesystem::path& path() const { return path_; }
    bool dirty() const { return dirty_; }
    Save save_state() const { return save_; }

    void set_title(std::string title);
    void set_path(std::filesystem::path path);
    void mark_typing();
    void mark_saved();
    void mark_error();

    /// History is kept as whole snapshots: a document is small and the rule is
    /// simple enough to stay obviously correct.
    void push_undo();
    bool undo(std::string& out);
    bool redo(std::string& out);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

private:
    std::string title_{"未命名.md"};
    std::string source_;
    std::filesystem::path path_;
    bool dirty_ = false;
    Save save_ = Save::Saved;
    std::vector<std::string> undo_;
    std::vector<std::string> redo_;
};

}  // namespace zmd

#endif
