#ifndef READER_BOOK_ZIP_H
#define READER_BOOK_ZIP_H

#include <filesystem>
#include <string>
#include <vector>

namespace reader {

/// Read-only ZIP helper over vendored minizip (EPUB containers).
class ZipReader {
public:
    ZipReader() = default;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader();

    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const { return file_ != nullptr; }

    /// Entry names use forward slashes as stored in the archive.
    std::vector<std::string> entries() const;
    bool read(const std::string& entry, std::string& out) const;
    bool extract_to(const std::string& entry, const std::filesystem::path& dest) const;

private:
    void* file_ = nullptr;  // unzFile
};

std::string zip_dirname(std::string_view path);
std::string zip_join(std::string_view base, std::string_view rel);
std::string zip_normalize(std::string path);

}  // namespace reader

#endif
