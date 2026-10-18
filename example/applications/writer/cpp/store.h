#ifndef WRITER_STORE_H
#define WRITER_STORE_H

#include <filesystem>
#include <string>
#include <vector>

namespace writer {

struct Snap {
    std::string id;
    std::string label;
    std::string text;
};

struct Doc {
    std::string id;
    std::string parent;
    std::string name;
    bool folder = false;
    std::string body;
    std::string saved_body;
    std::string created;
    std::string edited;
    std::vector<Snap> history;
};

/// Pieces, folders, and their files. No `App`.
class DocStore {
public:
    void load(const std::filesystem::path& root);
    void save(const std::filesystem::path& root) const;
    void erase_files(const std::filesystem::path& root, const Doc& doc) const;
    std::string fresh_id();
    std::string fresh_snap_id();

    Doc* find(const std::string& id);
    const Doc* find(const std::string& id) const;

    std::vector<Doc>& docs() { return docs_; }
    const std::vector<Doc>& docs() const { return docs_; }
    void clear() {
        docs_.clear();
        seq_ = 0;
    }

private:
    std::vector<Doc> docs_;
    int seq_ = 0;
};

}  // namespace writer

#endif
