#ifndef ZUI_PLUGIN_INSTANCE_UNIX_CLAIM_H
#define ZUI_PLUGIN_INSTANCE_UNIX_CLAIM_H

// Unix-domain socket plus a non-blocking flock. Each platform file chooses the
// path and owns the service object. This header is empty on Windows.

#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace zui {
namespace instance_detail {

#if !defined(_WIN32)

class UnixClaim {
public:
    UnixClaim() = default;
    UnixClaim(const UnixClaim&) = delete;
    UnixClaim& operator=(const UnixClaim&) = delete;
    ~UnixClaim() { release(); }

    bool claim(const std::string& socket_path) {
        release();
        if (socket_path.empty()) {
            return false;
        }
        sockaddr_un probe{};
        if (socket_path.size() + 1 > sizeof(probe.sun_path)) {
            return false;
        }
        const std::string::size_type slash = socket_path.find_last_of('/');
        if (slash != std::string::npos && slash > 0) {
            mkdir_p(socket_path.substr(0, slash));
        }
        const std::string lock_path = socket_path + ".lock";
        lock_fd_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
        if (lock_fd_ < 0) {
            return false;
        }
        if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            ::close(lock_fd_);
            lock_fd_ = -1;
            return false;
        }
        ::unlink(socket_path.c_str());
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listen_fd_, 4) != 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        const int flags = ::fcntl(listen_fd_, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
        }
        return true;
    }

    bool signal(const std::string& socket_path, std::string_view args) const {
        if (socket_path.empty()) {
            return false;
        }
        sockaddr_un probe{};
        if (socket_path.size() + 1 > sizeof(probe.sun_path)) {
            return false;
        }
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return false;
        }
        std::size_t off = 0;
        while (off < args.size()) {
            const ssize_t n = ::write(fd, args.data() + off, args.size() - off);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ::close(fd);
                return false;
            }
            off += static_cast<std::size_t>(n);
        }
        ::close(fd);
        return true;
    }

    std::string take() const {
        if (listen_fd_ < 0) {
            return {};
        }
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            return {};
        }
        std::string out;
        char buffer[4096];
        for (;;) {
            const ssize_t n = ::read(client, buffer, sizeof(buffer));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (n == 0) {
                break;
            }
            out.append(buffer, buffer + n);
        }
        ::close(client);
        return out;
    }

private:
    static void mkdir_p(const std::string& path) {
        std::string cur;
        cur.reserve(path.size());
        for (char c : path) {
            cur.push_back(c);
            if (c == '/' && cur.size() > 1) {
                ::mkdir(cur.c_str(), 0700);
            }
        }
        if (!cur.empty() && cur.back() != '/') {
            ::mkdir(cur.c_str(), 0700);
        }
    }

    void release() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (lock_fd_ >= 0) {
            ::flock(lock_fd_, LOCK_UN);
            ::close(lock_fd_);
            lock_fd_ = -1;
        }
    }

    int lock_fd_ = -1;
    int listen_fd_ = -1;
};

#endif

}  // namespace instance_detail
}  // namespace zui

#endif
