#ifndef SHARE_BENCH_DETAIL_H
#define SHARE_BENCH_DETAIL_H

#include "net.h"
#include "util.h"

#include <string>

namespace share {

void usage();
std::uint64_t parse_size(const char* text);
bool write_payload(const std::string& path, std::uint64_t size);
bool verify_payload(const std::string& path, std::uint64_t size);
int wait_xfer(const char* side);
int run_recv(const std::string& dir, const char* port_file);
Peer wait_any_peer(double timeout_sec);
int run_send_to(const Peer& peer, const std::string& path);
int start_sender(const std::string& dir);
int run_partial(const std::string& dir, const std::string& src, const std::string& rel, double fraction);

}  // namespace share

#endif
