#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef SHARE_NET_INTERNAL_H
#define SHARE_NET_INTERNAL_H

#include <cstdio>
#include "net.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t INVALID_SOCK = INVALID_SOCKET;
#define SHUT_RDWR SD_BOTH
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <ifaddrs.h>
#define HAS_IFADDRS 1
#endif
using sock_t = int;
static const sock_t INVALID_SOCK = -1;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

namespace share {

inline const uint16_t UDP_PORT = 37210;
inline const uint16_t TCP_PORT0 = 37211;
inline const char* MCAST = "239.255.87.21";
inline const uint32_t CHUNK = 1024 * 1024;
inline const int NWORKERS = 8;

enum : uint8_t {
    M_HELLO = 1,
    M_OFFER = 2,
    M_ACCEPT = 3,
    M_REJECT = 4,
    M_DONE = 5,
    M_COMPLETE = 6,
    M_CANCEL = 7
};

enum : uint8_t { D_CHUNK = 1, D_END = 2 };

struct W {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) {
        b.push_back((uint8_t)v);
        b.push_back((uint8_t)(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; i++) b.push_back((uint8_t)(v >> (8 * i)));
    }
    void str(const std::string& s) {
        uint16_t n = (uint16_t)std::min(s.size(), (size_t)0xffff);
        u16(n);
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
    void bytes(const std::vector<uint8_t>& x) { b.insert(b.end(), x.begin(), x.end()); }
};

struct R {
    const uint8_t* p;
    size_t n;
    size_t i = 0;
    bool left(size_t k) const { return i + k <= n; }
    bool u8(uint8_t& o) {
        if (!left(1)) return false;
        o = p[i++];
        return true;
    }
    bool u16(uint16_t& o) {
        if (!left(2)) return false;
        o = (uint16_t)p[i] | ((uint16_t)p[i + 1] << 8);
        i += 2;
        return true;
    }
    bool u32(uint32_t& o) {
        if (!left(4)) return false;
        o = 0;
        for (int k = 0; k < 4; k++) o |= (uint32_t)p[i++] << (8 * k);
        return true;
    }
    bool u64(uint64_t& o) {
        if (!left(8)) return false;
        o = 0;
        for (int k = 0; k < 8; k++) o |= (uint64_t)p[i++] << (8 * k);
        return true;
    }
    bool str(std::string& o) {
        uint16_t n = 0;
        if (!u16(n) || !left(n)) return false;
        o.assign((const char*)p + i, (const char*)p + i + n);
        i += n;
        return true;
    }
};

inline uint16_t ru16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline uint64_t ru64(const uint8_t* p) {
    uint64_t o = 0;
    for (int k = 0; k < 8; k++) o |= (uint64_t)p[k] << (8 * k);
    return o;
}
inline void wu64(uint8_t* p, uint64_t v) {
    for (int k = 0; k < 8; k++) p[k] = (uint8_t)(v >> (8 * k));
}

inline uint32_t nchunks_of(uint64_t size) {
    if (!size) return 0;
    return (uint32_t)((size + CHUNK - 1) / CHUNK);
}
inline uint32_t chunk_len(uint64_t size, uint32_t i) {
    uint64_t off = (uint64_t)i * CHUNK;
    if (off >= size) return 0;
    return (uint32_t)std::min<uint64_t>(CHUNK, size - off);
}
inline bool bit_get(const std::vector<uint8_t>& b, uint32_t i) {
    if (i / 8 >= b.size()) return false;
    return (b[i / 8] >> (i % 8)) & 1;
}
inline bool bit_set(std::vector<uint8_t>& b, uint32_t i) {
    if (i / 8 >= b.size()) return false;
    uint8_t m = (uint8_t)(1u << (i % 8));
    if (b[i / 8] & m) return false;
    b[i / 8] |= m;
    return true;
}

inline void sock_close(sock_t s) {
    if (s == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

inline void set_block(sock_t s, bool block) {
#ifdef _WIN32
    u_long m = block ? 0 : 1;
    ioctlsocket(s, FIONBIO, &m);
#else
    int f = fcntl(s, F_GETFL, 0);
    if (f < 0) return;
    fcntl(s, F_SETFL, block ? (f & ~O_NONBLOCK) : (f | O_NONBLOCK));
#endif
}

inline void set_timeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&t, sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&t, sizeof(t));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

inline void tune_sock(sock_t s, bool nodelay) {
    int buf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&buf, sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buf, sizeof(buf));
    if (nodelay) {
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
    }
#ifdef __APPLE__
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

inline bool write_all(sock_t s, const void* p, size_t n) {
    const char* b = (const char*)p;
    size_t o = 0;
    while (o < n) {
        int k = (int)send(s, b + o, (int)(n - o), MSG_NOSIGNAL);
        if (k <= 0) return false;
        o += (size_t)k;
    }
    return true;
}

inline bool read_all(sock_t s, void* p, size_t n) {
    char* b = (char*)p;
    size_t o = 0;
    while (o < n) {
        int k = (int)recv(s, b + o, (int)(n - o), 0);
        if (k <= 0) return false;
        o += (size_t)k;
    }
    return true;
}

inline bool read_at(FILE* fp, uint64_t off, void* p, size_t n) {
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(fp));
    if (h == INVALID_HANDLE_VALUE) return false;
    char* b = (char*)p;
    size_t o = 0;
    while (o < n) {
        OVERLAPPED ov{};
        uint64_t pos = off + o;
        ov.Offset = (DWORD)pos;
        ov.OffsetHigh = (DWORD)(pos >> 32);
        DWORD w = 0;
        DWORD want = (DWORD)std::min(n - o, (size_t)1 << 30);
        if (!ReadFile(h, b + o, want, &w, &ov) || w == 0) return false;
        o += (size_t)w;
    }
    return true;
#else
    char* b = (char*)p;
    size_t o = 0;
    while (o < n) {
        ssize_t k = pread(fileno(fp), b + o, n - o, (off_t)(off + o));
        if (k <= 0) return false;
        o += (size_t)k;
    }
    return true;
#endif
}

inline bool write_at(FILE* fp, uint64_t off, const void* p, size_t n) {
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(fp));
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* b = (const char*)p;
    size_t o = 0;
    while (o < n) {
        OVERLAPPED ov{};
        uint64_t pos = off + o;
        ov.Offset = (DWORD)pos;
        ov.OffsetHigh = (DWORD)(pos >> 32);
        DWORD w = 0;
        DWORD want = (DWORD)std::min(n - o, (size_t)1 << 30);
        if (!WriteFile(h, b + o, want, &w, &ov) || w == 0) return false;
        o += (size_t)w;
    }
    return true;
#else
    const char* b = (const char*)p;
    size_t o = 0;
    while (o < n) {
        ssize_t k = pwrite(fileno(fp), b + o, n - o, (off_t)(off + o));
        if (k <= 0) return false;
        o += (size_t)k;
    }
    return true;
#endif
}

inline void preallocate(FILE* fp, uint64_t size) {
    if (!fp || !size) return;
#ifdef _WIN32
    if (_chsize_s(_fileno(fp), (__int64)size) != 0)
        std::fprintf(stderr, "[net] preallocate %llu failed\n", (unsigned long long)size);
#elif defined(__APPLE__)
    ftruncate(fileno(fp), (off_t)size);
#else
    int fd = fileno(fp);
    if (posix_fallocate(fd, 0, (off_t)size) != 0) ftruncate(fd, (off_t)size);
#endif
}

inline bool send_msg(sock_t s, const W& w) {
    uint32_t n = (uint32_t)w.b.size();
    uint8_t h[4];
    for (int i = 0; i < 4; i++) h[i] = (uint8_t)(n >> (8 * i));
    return write_all(s, h, 4) && (n == 0 || write_all(s, w.b.data(), n));
}

inline bool recv_msg(sock_t s, std::vector<uint8_t>& out) {
    uint8_t h[4];
    if (!read_all(s, h, 4)) return false;
    uint32_t n = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
    if (n > 16u * 1024u * 1024u) return false;
    out.resize(n);
    return n == 0 || read_all(s, out.data(), n);
}

inline bool connect_timeout(sock_t s, sockaddr_in* a, int ms) {
    set_block(s, false);
    int r = connect(s, (sockaddr*)a, sizeof(*a));
    if (r == 0) {
        set_block(s, true);
        return true;
    }
#ifdef _WIN32
    if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
#else
    if (errno != EINPROGRESS) return false;
#endif
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    r = select((int)s + 1, nullptr, &w, nullptr, &tv);
    if (r <= 0) return false;
    int err = 0;
    socklen_t l = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &l);
    set_block(s, true);
    return err == 0;
}

inline sock_t tcp_connect(const std::string& ip, uint16_t port, int timeout_ms) {
    sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCK) return INVALID_SOCK;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
        sock_close(s);
        return INVALID_SOCK;
    }
    tune_sock(s, true);
    if (!connect_timeout(s, &a, timeout_ms)) {
        sock_close(s);
        return INVALID_SOCK;
    }
    return s;
}

inline sock_t bind_listen(uint16_t& port) {
    sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCK) return INVALID_SOCK;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    for (int i = 0; i < 20; i++) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)(TCP_PORT0 + i));
        if (bind(s, (sockaddr*)&a, sizeof(a)) == 0) {
            port = (uint16_t)(TCP_PORT0 + i);
            if (listen(s, 128) != 0) break;
            return s;
        }
    }
    sock_close(s);
    return INVALID_SOCK;
}

struct PeerEx : Peer {
    double last = 0;
};

struct RecvFile {
    std::string rel;
    uint64_t size = 0;
    uint64_t content_id = 0;
    bool is_dir = false;
    std::string dest;
    std::string part;
    std::string map_path;
    std::vector<uint8_t> bits;
    FILE* fp = nullptr;
    std::mutex io_mu;
    std::mutex map_mu;
    bool finished = false;
    uint32_t unsaved = 0;
    double last_map_save = 0;
};

struct RecvSession {
    uint64_t offer_id = 0;
    std::vector<std::shared_ptr<RecvFile>> files;
    std::atomic<uint64_t> got{0};
    uint64_t total = 0;
};

struct PendingIn {
    IncomingOffer info;
    std::vector<ShareFile> files;
    std::mutex mu;
    std::condition_variable cv;
    int reply = 0;
};

struct Job {
    int file_index = 0;
    uint32_t chunk_index = 0;
    uint64_t off = 0;
    uint32_t len = 0;
};

struct MapPeek {
    uint64_t size = 0;
    uint64_t content_id = 0;
    bool ok = false;
};

class Discovery {
public:
    bool start(uint64_t my_id, std::string name, std::string save_dir);
    void stop();
    uint16_t tcp_port() const;
    void set_name(std::string name);
    std::vector<Peer> peers() const;
    void stage(Peer peer);
};

class Transfer {
public:
    bool offer(const Peer& peer, std::vector<ShareFile> files);
    void respond(uint64_t offer_id, bool accept);
    void cancel();
    std::vector<IncomingOffer> incoming() const;
    void stage_incoming(IncomingOffer offer);
};

class Snapshot {
public:
    XferView current() const;
    void clear();
    void stage(const XferView& view);
    void set_save_dir(std::string dir);
};

/// Process-wide LAN service. The instance is a function-local static so staged tests and a
/// running listener share one socket table without a file-scope global.
struct Lan {
    std::mutex mu;
    std::atomic<bool> run{false};
    std::atomic<bool> cancel{false};
    uint64_t id = 0;
    std::string name;
    std::string save;
    uint16_t tcp_port = TCP_PORT0;
    sock_t listen = INVALID_SOCK;
    sock_t active_ctrl = INVALID_SOCK;
    std::thread disc_th;
    std::thread listen_th;
    std::vector<PeerEx> peers;
    std::vector<std::shared_ptr<PendingIn>> incoming;
    std::map<uint64_t, std::shared_ptr<RecvSession>> sessions;
    XferView xfer;
    std::atomic<uint64_t> xfer_done{0};
    std::atomic<uint64_t> xfer_session{0};
    Discovery discovery;
    Transfer transfer;
    Snapshot snapshot;
};

inline Lan& service() {
    static Lan lan;
    return lan;
}

bool xfer_busy_unlocked();
void set_xfer(XferPhase phase, const std::string& err = {});
uint64_t bytes_from_bits(uint64_t size, const std::vector<uint8_t>& bits);
void save_map(const RecvFile& f);
MapPeek peek_map(const std::string& path);
std::vector<uint8_t> load_map(const std::string& path, uint64_t size, uint64_t content_id);
void send_beacons(sock_t udp, const std::vector<uint8_t>& pkt);
void disc_thread();
FILE* open_part(RecvFile& f);
void finish_file(RecvFile& f);
std::shared_ptr<RecvSession> prepare_session(uint64_t offer_id, const std::vector<ShareFile>& files);
void handle_data(sock_t s);
void handle_control(sock_t s);
void handle_client(sock_t s);
void listen_thread();
void sender_worker(Peer peer, uint64_t offer_id, const std::vector<ShareFile>* files,
                   const std::vector<Job>* jobs, std::vector<std::atomic<int>>* remaining,
                   size_t job_lo, size_t job_hi);
void sender_fn(Peer peer, std::vector<ShareFile> files);

}  // namespace share

#endif
