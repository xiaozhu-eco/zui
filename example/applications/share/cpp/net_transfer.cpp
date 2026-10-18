#include "net_internal.h"

namespace share {

std::vector<IncomingOffer> Transfer::incoming() const {
    std::lock_guard<std::mutex> g(service().mu);
    std::vector<IncomingOffer> out;
    out.reserve(service().incoming.size());
    for (auto& p : service().incoming) out.push_back(p->info);
    return out;
}

bool Transfer::offer(const Peer& peer, std::vector<ShareFile> files) {
    bool staged = false;
    {
        std::lock_guard<std::mutex> g(service().mu);
        if (xfer_busy_unlocked()) return false;
        service().cancel = false;
        service().xfer = {};
        service().xfer.phase = XferPhase::WaitingAccept;
        service().xfer.sending = true;
        service().xfer.peer_name = peer.name;
        service().xfer.peer_ip = peer.ip;
        service().xfer.started = now_sec();
        service().xfer_done = 0;
        service().xfer_session = 0;
        staged = peer.port == 0;
    }
    if (staged) return true;
    std::thread(sender_fn, peer, std::move(files)).detach();
    return true;
}

void Transfer::respond(uint64_t offer_id, bool accept) {
    std::lock_guard<std::mutex> g(service().mu);
    for (auto it = service().incoming.begin(); it != service().incoming.end(); ++it) {
        if ((*it)->info.offer_id != offer_id) continue;
        {
            std::lock_guard<std::mutex> lk((*it)->mu);
            (*it)->reply = accept ? 1 : -1;
            (*it)->cv.notify_all();
        }
        // The listener erases too, after it wakes. A staged offer has no listener, so drop it
        // here or the next poll opens the sheet again.
        service().incoming.erase(it);
        return;
    }
}

void Transfer::cancel() {
    service().cancel = true;
    std::lock_guard<std::mutex> g(service().mu);
    if (service().active_ctrl != INVALID_SOCK) {
        shutdown(service().active_ctrl, SHUT_RDWR);
        return;
    }
    // No socket is in the transfer. Drop the snapshot so a staged screen does not bounce back
    // on the next poll. A live thread sets Error itself and must still see service().xfer.
    service().xfer = {};
    service().xfer_done = 0;
    service().xfer_session = 0;
}

void Transfer::stage_incoming(IncomingOffer offer) {
    std::lock_guard<std::mutex> g(service().mu);
    service().incoming.clear();
    auto pend = std::make_shared<PendingIn>();
    pend->info = std::move(offer);
    service().incoming.push_back(std::move(pend));
}


}  // namespace share
