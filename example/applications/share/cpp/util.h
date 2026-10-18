#ifndef SHARE_UTIL_H
#define SHARE_UTIL_H

#include <zui/host.h>

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace share {

/// Identity + where received files go. Persisted as `config.txt` in the config directory.
struct Config {
    std::uint64_t id = 0;
    std::string name;
    std::string save_dir;
};

/// One row of the file listing.
struct DirEnt {
    std::string name;
    std::string path;
    bool is_dir = false;
    std::uint64_t size = 0;
};

/// One entry of a share: what the sender tells the receiver about a file or folder.
struct ShareFile {
    std::string rel;   ///< path relative to the share root; the wire name
    std::string abs;   ///< local path
    std::uint64_t size = 0;
    std::uint64_t content_id = 0;  ///< FNV-1a of the bytes; 0 for a folder
    bool is_dir = false;
};

/// Where `config.txt` lives. zui passes `App::user_data_dir()`; empty keeps the platform default.
void set_config_dir(std::string dir);

bool is_android();
/// Desktop: always true. Android: Host ManageStorage == Granted (Termux skipped).
bool storage_access_ok();
bool storage_access_ok(zui::Host& host);
/// Desktop: no-op. Android: Host request_permission(ManageStorage).
void request_storage_access();
void request_storage_access(zui::Host& host, zui::Host::PermissionHandler done = {});
std::string home_dir();
std::string config_dir();
std::string default_save_dir();
std::string default_browse_dir();

/// Resolve a picker / config path into a real filesystem directory for receive.
/// Returns empty string on rejection. Never returns a content:// URI.
/// Rules (hard boundary this phase):
/// 1) Absolute path under primary (/storage/emulated/0 or /sdcard) that create_dir_all can ensure — accept.
/// 2) content:// only if deterministically mapped to that primary real path (e.g. known primary: tree id);
///    otherwise reject — no DocumentFile write layer.
/// 3) Desktop / non-Android: accept any create_dir_all-able absolute (or normalized) path.
/// Callers must also require storage_access_ok() before adopting the result on Android.
std::string try_resolve_save_dir(const std::string& path_or_uri);

Config load_config();
void save_config(const Config& c);

std::vector<DirEnt> list_dir(const std::string& path);
std::string parent_dir(const std::string& path);
/// Empty path means the platform root chooser (drives / volumes).
bool is_browse_roots(const std::string& path);
std::string browse_roots_label();
std::vector<DirEnt> list_fs_roots();
bool is_fs_root_path(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);
bool path_is_dir(const std::string& path);
bool path_exists(const std::string& path);
bool create_dir_all(const std::string& path);
std::uint64_t file_size_u8(const std::string& path);
/// FNV-1a 64-bit of the file contents; 0 if it cannot be read.
std::uint64_t file_content_id_u8(const std::string& path);
bool rename_u8(const std::string& from, const std::string& to);
bool remove_u8(const std::string& path);

/// If the path exists, return "name (1).ext", "name (2).ext", ...
std::string unique_path_u8(const std::string& path);

/// Every file and folder under the selection, with the relative names the wire carries.
std::vector<ShareFile> expand_selection(const std::set<std::string>& sel);
std::string dest_join(const std::string& root, const std::string& rel);

/// Notify body formatters (Chinese copy fixed by usable-product spec §2.2).
struct IncomingOffer;
struct XferView;
std::string notify_incoming_body(const IncomingOffer& o);
std::string notify_done_body(const XferView& v);
std::string notify_fail_body(const XferView& v);


/// Build a file: URI for Host::open_uri. Empty input → empty string.
/// Windows `C:\a\b` → `file:///C:/a/b`; POSIX `/a/b` → `file:///a/b`.
std::string path_to_file_uri(std::string_view path);

std::string format_bytes(std::uint64_t n);
std::string format_rate(double bytes_per_sec);
double now_sec();
std::uint64_t random_u64();

FILE* fopen_u8(const std::string& path, const char* mode);
int file_seek(FILE* fp, std::uint64_t off);
std::string trim(std::string_view s);

}  // namespace share

#endif  // SHARE_UTIL_H
