#include "bench_detail.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#endif

namespace share {
void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  zui_share bench [size]              loopback self-check (POSIX only, 128M default)\n"
                 "  zui_share bench recv DIR [PORTFILE] wait for a transfer, print/write our port\n"
                 "  zui_share bench send FILE           offer to the first discovered peer\n"
                 "  zui_share bench sendto IP PORT FILE offer to an explicit address\n"
                 "  zui_share bench gen FILE SIZE       write a verifiable payload\n"
                 "  zui_share bench check FILE SIZE     verify a payload written by `gen`\n"
                 "  zui_share bench partial DIR SRC REL FRACTION\n"
                 "                                      stage a half-received .xzpart/.xzmap pair\n");
}

std::uint64_t parse_size(const char* text) {
    char* end = nullptr;
    const double n = std::strtod(text, &end);
    if (end == text || n <= 0) {
        return 0;
    }
    while (*end == ' ') {
        ++end;
    }
    std::uint64_t mul = 1;
    if (*end == 'G' || *end == 'g') {
        mul = 1024ull * 1024ull * 1024ull;
    } else if (*end == 'M' || *end == 'm') {
        mul = 1024ull * 1024ull;
    } else if (*end == 'K' || *end == 'k') {
        mul = 1024ull;
    } else if (*end != '\0' && *end != 'B' && *end != 'b') {
        return 0;
    }
    if (n * static_cast<double>(mul) > 16.0 * 1024.0 * 1024.0 * 1024.0) {
        return 0;
    }
    return static_cast<std::uint64_t>(n * static_cast<double>(mul));
}

bool write_payload(const std::string& path, std::uint64_t size) {
    FILE* fp = fopen_u8(path, "wb");
    if (fp == nullptr) {
        return false;
    }
    std::vector<unsigned char> buffer(1024 * 1024);
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::uint32_t n = static_cast<std::uint32_t>((std::min)(buffer.size(), size - offset));
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint64_t x = offset + i;
            buffer[i] = static_cast<unsigned char>(x * 131u + 17u);
        }
        if (std::fwrite(buffer.data(), 1, n, fp) != n) {
            std::fclose(fp);
            return false;
        }
        offset += n;
    }
    std::fclose(fp);
    return true;
}

bool verify_payload(const std::string& path, std::uint64_t size) {
    if (file_size_u8(path) != size) {
        return false;
    }
    FILE* fp = fopen_u8(path, "rb");
    if (fp == nullptr) {
        return false;
    }
    std::vector<unsigned char> buffer(1024 * 1024);
    std::uint64_t offset = 0;
    bool ok = true;
    while (ok && offset < size) {
        const std::uint32_t n = static_cast<std::uint32_t>((std::min)(buffer.size(), size - offset));
        if (std::fread(buffer.data(), 1, n, fp) != n) {
            ok = false;
            break;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint64_t x = offset + i;
            if (buffer[i] != static_cast<unsigned char>(x * 131u + 17u)) {
                ok = false;
                break;
            }
        }
        offset += n;
    }
    std::fclose(fp);
    return ok;
}

/// Watches `net_xfer()` until the transfer leaves the active phases. Prints progress to stderr.
int wait_xfer(const char* side) {
    double started = 0;
    std::uint64_t last_done = 0;
    double last_change = now_sec();
    for (;;) {
        const XferView x = net_xfer();
        if (x.phase == XferPhase::Transferring) {
            if (started == 0) {
                started = now_sec();
            }
            if (x.done != last_done) {
                last_done = x.done;
                last_change = now_sec();
            } else if (now_sec() - last_change > 30.0) {
                std::fprintf(stderr, "%s: timed out (no progress for 30s)\n", side);
                net_cancel();
                return 1;
            }
            double elapsed = now_sec() - started;
            if (elapsed < 0.05) {
                elapsed = 0.05;
            }
            std::fprintf(stderr, "\r%s  %s / %s  %s   ", side, format_bytes(x.done).c_str(),
                         format_bytes(x.total).c_str(),
                         format_rate(static_cast<double>(x.session_bytes) / elapsed).c_str());
            std::fflush(stderr);
        }
        if (x.phase == XferPhase::Done) {
            double elapsed = now_sec() - (started > 0 ? started : x.started);
            if (elapsed < 0.001) {
                elapsed = 0.001;
            }
            const std::uint64_t bytes = x.session_bytes != 0 ? x.session_bytes : x.total;
            std::fprintf(stderr, "\n");
            std::printf("%s done  %s  %.2f s  %s\n", side, format_bytes(bytes).c_str(), elapsed,
                        format_rate(static_cast<double>(bytes) / elapsed).c_str());
            std::fflush(stdout);
            return 0;
        }
        if (x.phase == XferPhase::Error || x.phase == XferPhase::Rejected) {
            std::fprintf(stderr, "\n%s failed: %s\n", side,
                         x.error.empty() ? "unknown" : x.error.c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int run_recv(const std::string& dir, const char* port_file) {
    create_dir_all(dir);
    if (!net_start(random_u64(), "bench-recv", dir)) {
        std::fprintf(stderr, "cannot listen (is another instance running?)\n");
        return 1;
    }
    if (port_file != nullptr) {
        FILE* out = fopen_u8(port_file, "w");
        if (out != nullptr) {
            std::fprintf(out, "%u\n", static_cast<unsigned>(net_tcp_port()));
            std::fclose(out);
        }
    }
    std::printf("recv ready  port %u  dir %s\n", static_cast<unsigned>(net_tcp_port()), dir.c_str());
    std::fflush(stdout);
    for (;;) {
        const std::vector<IncomingOffer> offers = net_incoming();
        if (!offers.empty()) {
            std::printf("offer from %s (%s), accepting\n", offers[0].from_name.c_str(),
                        format_bytes(offers[0].nbytes).c_str());
            std::fflush(stdout);
            net_respond(offers[0].offer_id, true);
            const int rc = wait_xfer("recv");
            net_stop();
            return rc;
        }
        const XferView x = net_xfer();
        if (x.phase == XferPhase::Transferring || x.phase == XferPhase::Done ||
            x.phase == XferPhase::Error) {
            const int rc = wait_xfer("recv");
            net_stop();
            return rc;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

Peer wait_any_peer(double timeout_sec) {
    const double start = now_sec();
    while (now_sec() - start < timeout_sec) {
        const std::vector<Peer> peers = net_peers();
        if (!peers.empty()) {
            return peers[0];
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return {};
}

int run_send_to(const Peer& peer, const std::string& path) {
    ShareFile file;
    file.rel = "bench.bin";
    file.abs = path;
    file.size = file_size_u8(path);
    file.content_id = file_content_id_u8(path);
    if (file.size == 0) {
        std::fprintf(stderr, "missing or empty file: %s\n", path.c_str());
        return 1;
    }
    std::printf("send %s (%s) -> %s %s:%u\n", path.c_str(), format_bytes(file.size).c_str(),
                peer.name.c_str(), peer.ip.c_str(), static_cast<unsigned>(peer.port));
    std::fflush(stdout);
    if (!net_offer(peer, {file})) {
        std::fprintf(stderr, "a transfer is already running\n");
        return 1;
    }
    return wait_xfer("send");
}

int start_sender(const std::string& dir) {
    create_dir_all(dir);
    if (!net_start(random_u64(), "bench-send", dir)) {
        std::fprintf(stderr, "cannot listen (is another instance running?)\n");
        return 1;
    }
    return 0;
}

/// Builds the on-disk state a receiver would have after @p fraction of a transfer: an `.xzpart`
/// of the exact final size holding the first chunks, and an `.xzmap` (XZM2) marking them done.
/// A following `recv` + `sendto` must then resume instead of starting over.
int run_partial(const std::string& dir, const std::string& src, const std::string& rel,
                double fraction) {
    const std::uint64_t size = file_size_u8(src);
    if (size == 0) {
        std::fprintf(stderr, "missing or empty source: %s\n", src.c_str());
        return 1;
    }
    const std::uint64_t content_id = file_content_id_u8(src);
    create_dir_all(dir);
    const std::string dest = dest_join(dir, rel);
    if (dest.empty()) {
        std::fprintf(stderr, "bad relative name: %s\n", rel.c_str());
        return 1;
    }
    const std::string part = dest + ".xzpart";
    const std::string map = dest + ".xzmap";
    remove_u8(part);
    remove_u8(map);
    remove_u8(dest);

    constexpr std::uint64_t kChunk = 1024 * 1024;
    const std::uint32_t chunks = static_cast<std::uint32_t>((size + kChunk - 1) / kChunk);
    const std::uint32_t done = static_cast<std::uint32_t>(fraction * chunks);
    const std::uint32_t map_bytes = (chunks + 7) / 8;

    FILE* in = fopen_u8(src, "rb");
    FILE* out = fopen_u8(part, "wb+");
    if (in == nullptr || out == nullptr) {
        if (in != nullptr) std::fclose(in);
        if (out != nullptr) std::fclose(out);
        std::fprintf(stderr, "cannot stage %s\n", part.c_str());
        return 1;
    }
#ifdef _WIN32
    if (_chsize_s(_fileno(out), static_cast<__int64>(size)) != 0) {
        std::fprintf(stderr, "cannot size %s\n", part.c_str());
    }
#else
    if (ftruncate(fileno(out), static_cast<off_t>(size)) != 0) {
        std::fprintf(stderr, "cannot size %s\n", part.c_str());
    }
#endif
    std::vector<char> buffer(static_cast<std::size_t>(kChunk));
    for (std::uint32_t i = 0; i < done; ++i) {
        const std::uint32_t len = static_cast<std::uint32_t>((std::min)(kChunk, size - i * kChunk));
        if (std::fread(buffer.data(), 1, len, in) != len) {
            break;
        }
        if (std::fwrite(buffer.data(), 1, len, out) != len) {
            break;
        }
    }
    std::fclose(in);
    std::fclose(out);

    FILE* mf = fopen_u8(map, "wb");
    if (mf == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", map.c_str());
        return 1;
    }
    std::vector<unsigned char> bits(map_bytes, 0);
    for (std::uint32_t i = 0; i < done; ++i) {
        bits[i / 8] |= static_cast<unsigned char>(1u << (i % 8));
    }
    unsigned char header[20];
    for (int k = 0; k < 8; ++k) {
        header[k] = static_cast<unsigned char>(size >> (8 * k));
        header[8 + k] = static_cast<unsigned char>(content_id >> (8 * k));
    }
    header[16] = static_cast<unsigned char>(map_bytes);
    header[17] = static_cast<unsigned char>(map_bytes >> 8);
    header[18] = static_cast<unsigned char>(map_bytes >> 16);
    header[19] = static_cast<unsigned char>(map_bytes >> 24);
    std::fwrite("XZM2", 1, 4, mf);
    std::fwrite(header, 1, sizeof(header), mf);
    std::fwrite(bits.data(), 1, bits.size(), mf);
    std::fclose(mf);
    std::printf("staged %s: %u/%u chunks (%s of %s)\n", part.c_str(), done, chunks,
                format_bytes(static_cast<std::uint64_t>(done) * kChunk > size
                                 ? size
                                 : static_cast<std::uint64_t>(done) * kChunk)
                    .c_str(),
                format_bytes(size).c_str());
    return 0;
}

}  // namespace share
