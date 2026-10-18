#ifndef SHARE_NET_H
#define SHARE_NET_H

#include "util.h"

#include <cstdint>
#include <string>
#include <vector>

namespace share {

/// One device seen on the LAN. `id` is the peer's random identity, not its address.
struct Peer {
    std::uint64_t id = 0;
    std::string name;
    std::string ip;
    std::uint16_t port = 0;
};

enum class XferPhase { Idle, WaitingAccept, Transferring, Done, Rejected, Error };

/// What the transfer screens read. Everything is a snapshot; poll `net_xfer()` again.
struct XferView {
    XferPhase phase = XferPhase::Idle;
    bool sending = false;
    std::string peer_name;
    std::string peer_ip;
    std::uint64_t total = 0;
    std::uint64_t done = 0;
    std::uint64_t session_bytes = 0;  ///< bytes moved this run, for the speed readout
    double started = 0;
    std::string error;
    /// The file being written (receiver) or sent (sender) right now.
    std::string current_file;
    /// Files already complete, in the order they finished.
    std::vector<std::string> finished;
};

/// A share a peer is offering us, waiting for accept / reject.
struct IncomingOffer {
    std::uint64_t offer_id = 0;
    std::string from_name;
    std::string from_ip;
    std::uint32_t nfiles = 0;
    std::uint64_t nbytes = 0;
};

/// Starts the discovery + listener threads. False when the TCP port cannot be bound (another
/// instance is already running).
bool net_start(std::uint64_t my_id, std::string name, std::string save_dir);
std::uint16_t net_tcp_port();
void net_stop();
void net_set_name(std::string name);
void net_set_save_dir(std::string dir);

std::vector<Peer> net_peers();
std::vector<IncomingOffer> net_incoming();
XferView net_xfer();

/// Offers @p files to @p peer. False when a transfer is already running.
bool net_offer(const Peer& peer, std::vector<ShareFile> files);
void net_respond(std::uint64_t offer_id, bool accept);
void net_clear_xfer();
void net_cancel();

/// Publish one peer without discovery. `port == 0` is a staged peer: `net_offer` records the
/// wait and does not open a socket. Replaces the same id and drops earlier port-0 leftovers.
void net_stage_peer(Peer peer);
/// One incoming offer and no listener thread. Replaces whatever was staged.
void net_stage_incoming(IncomingOffer offer);
/// A transfer snapshot the screens read. Does not open a socket.
void net_stage_xfer(const XferView& view);

}  // namespace share

#endif  // SHARE_NET_H
