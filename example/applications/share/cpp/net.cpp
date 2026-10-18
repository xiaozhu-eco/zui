#include "net_internal.h"

namespace share {

bool net_start(std::uint64_t my_id, std::string name, std::string save_dir) {
    return service().discovery.start(my_id, std::move(name), std::move(save_dir));
}

std::uint16_t net_tcp_port() { return service().discovery.tcp_port(); }

void net_stop() { service().discovery.stop(); }

void net_set_name(std::string name) { service().discovery.set_name(std::move(name)); }

void net_set_save_dir(std::string dir) { service().snapshot.set_save_dir(std::move(dir)); }

std::vector<Peer> net_peers() { return service().discovery.peers(); }

std::vector<IncomingOffer> net_incoming() { return service().transfer.incoming(); }

XferView net_xfer() { return service().snapshot.current(); }

bool net_offer(const Peer& peer, std::vector<ShareFile> files) {
    return service().transfer.offer(peer, std::move(files));
}

void net_respond(std::uint64_t offer_id, bool accept) {
    service().transfer.respond(offer_id, accept);
}

void net_clear_xfer() { service().snapshot.clear(); }

void net_cancel() { service().transfer.cancel(); }

void net_stage_peer(Peer peer) { service().discovery.stage(std::move(peer)); }

void net_stage_incoming(IncomingOffer offer) {
    service().transfer.stage_incoming(std::move(offer));
}

void net_stage_xfer(const XferView& view) { service().snapshot.stage(view); }

}  // namespace share
