#pragma once

// The archive formats a package container needs and zlib does not provide: a
// POSIX tar (the payload of a .deb, and the portable linux package) and the ar
// archive a .deb is wrapped in. Everything is written from memory, so a package
// is a value the caller can inspect before it is written.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zui_tool {

/// gzip (RFC 1952), the compression a .deb member and a .tar.gz carry.
bool gzip_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out,
                std::string& error);
bool gunzip_bytes(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                  std::string& error);

/// One member of a tar or ar archive.
struct ArchiveMember {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::uint32_t mode = 0644;
    bool directory = false;
};

/// A tar archive in ustar form. Names up to 100 bytes go in the name field;
/// longer ones are split over prefix and name.
class TarWriter {
public:
    void add(const std::string& name, const std::vector<std::uint8_t>& bytes,
             std::uint32_t mode = 0644);
    void add_text(const std::string& name, const std::string& text, std::uint32_t mode = 0644);
    void add_directory(const std::string& name, std::uint32_t mode = 0755);

    /// The archive, finished with its end-of-archive blocks.
    std::vector<std::uint8_t> finish() const;

private:
    void append_header(const std::string& name, std::uint64_t size, std::uint32_t mode,
                       char type);
    std::vector<std::uint8_t> bytes_;
};

/// Lists a tar archive: name and size for each member.
bool tar_list(const std::uint8_t* data, std::size_t size,
              std::vector<std::pair<std::string, std::size_t>>& out, std::string& error);

/// An ar archive (the outer container of a .deb). Member names are short and
/// padded; debian-binary, control.tar.gz, data.tar.gz is the order dpkg wants.
class ArWriter {
public:
    void add(const std::string& name, const std::vector<std::uint8_t>& bytes);

    std::vector<std::uint8_t> finish() const;

private:
    std::vector<std::uint8_t> bytes_;
};

/// Lists an ar archive: name and size for each member.
bool ar_list(const std::uint8_t* data, std::size_t size,
             std::vector<std::pair<std::string, std::size_t>>& out, std::string& error);

}  // namespace zui_tool
