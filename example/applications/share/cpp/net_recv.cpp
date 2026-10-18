#include "net_internal.h"

namespace share {

FILE* open_part(RecvFile& f) {
    if (f.fp) return f.fp;
    create_dir_all(parent_dir(f.part));
    bool fresh = false;
    f.fp = fopen_u8(f.part, "rb+");
    if (!f.fp) {
        f.fp = fopen_u8(f.part, "wb+");
        fresh = true;
    }
    if (f.fp) {
        setvbuf(f.fp, nullptr, _IONBF, 0);
        if (fresh) preallocate(f.fp, f.size);
    }
    return f.fp;
}

void finish_file(RecvFile& f) {
    if (f.finished) return;
    if (f.fp) {
        fflush(f.fp);
        fclose(f.fp);
        f.fp = nullptr;
    }
    if (!f.part.empty() && path_exists(f.part)) rename_u8(f.part, f.dest);
    if (!f.map_path.empty()) remove_u8(f.map_path);
    f.finished = true;
}

std::shared_ptr<RecvSession> prepare_session(uint64_t offer_id, const std::vector<ShareFile>& files) {
    auto sess = std::make_shared<RecvSession>();
    sess->offer_id = offer_id;
    std::string save;
    {
        std::lock_guard<std::mutex> g(service().mu);
        save = service().save;
    }
    create_dir_all(save);
    for (const auto& src : files) {
        auto f = std::make_shared<RecvFile>();
        f->rel = src.rel;
        f->size = src.size;
        f->content_id = src.content_id;
        f->is_dir = src.is_dir;
        f->dest = dest_join(save, src.rel);
        if (f->dest.empty()) return nullptr;
        if (f->is_dir) {
            create_dir_all(f->dest);
            f->finished = true;
            sess->files.push_back(f);
            continue;
        }
        sess->total += f->size;
        // Incomplete (.xzpart/.xzmap) resumes only when size still matches the
        // new offer. If the source was modified (size changed), drop the stale
        // partial and start clean. Finished dest with same name -> "name (N).ext".
        f->part = f->dest + ".xzpart";
        f->map_path = f->dest + ".xzmap";
        bool have_part = path_exists(f->part);
        bool have_map = path_exists(f->map_path);
        if (have_part || have_map) {
            const MapPeek meta = have_map ? peek_map(f->map_path) : MapPeek{};
            const uint64_t part_sz = have_part ? file_size_u8(f->part) : 0;
            bool stale = false;
            if (have_map && !meta.ok) stale = true;
            if (have_map && meta.ok && meta.size != f->size) stale = true;
            // Same size but different bytes (or legacy map with unknown id vs new id).
            if (have_map && meta.ok && f->content_id != 0 && meta.content_id != 0 &&
                meta.content_id != f->content_id)
                stale = true;
            if (have_map && meta.ok && meta.content_id == 0 && f->content_id != 0)
                stale = true;  // upgrade from XZM1: do not mix with new identity
            if (have_part && part_sz != f->size) stale = true;
            if (stale) {
                if (have_part) remove_u8(f->part);
                if (have_map) remove_u8(f->map_path);
                have_part = false;
                have_map = false;
            }
        }
        const bool resuming = have_part || have_map;
        if (!resuming && path_exists(f->dest)) {
            f->dest = unique_path_u8(f->dest);
            f->part = f->dest + ".xzpart";
            f->map_path = f->dest + ".xzmap";
        }
        if (f->size == 0) {
            create_dir_all(parent_dir(f->dest));
            FILE* z = fopen_u8(f->dest, "wb");
            if (z) fclose(z);
            f->finished = true;
            sess->files.push_back(f);
            continue;
        }
        uint32_t nc = nchunks_of(f->size);
        uint32_t nb = (nc + 7) / 8;
        f->bits = load_map(f->map_path, f->size, f->content_id);
        if (f->bits.size() != nb) f->bits.assign(nb, 0);
        uint64_t have = bytes_from_bits(f->size, f->bits);
        sess->got += have;
        sess->files.push_back(f);
    }
    return sess;
}

void handle_data(sock_t s) {
    uint8_t idb[8];
    if (!read_all(s, idb, 8)) {
        sock_close(s);
        return;
    }
    uint64_t offer_id = ru64(idb);
    std::shared_ptr<RecvSession> sess;
    for (int i = 0; i < 50 && service().run; i++) {
        {
            std::lock_guard<std::mutex> g(service().mu);
            auto it = service().sessions.find(offer_id);
            if (it != service().sessions.end()) sess = it->second;
        }
        if (sess) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!sess) {
        sock_close(s);
        return;
    }
    set_timeout(s, 60000);
    std::vector<char> buf(CHUNK);
    while (service().run && !service().cancel) {
        uint8_t t = 0;
        if (!read_all(s, &t, 1)) break;
        if (t == D_END) break;
        if (t != D_CHUNK) break;
        uint8_t hdr[16];
        if (!read_all(s, hdr, 16)) break;
        uint32_t idx = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        uint64_t off = ru64(hdr + 4);
        uint32_t len = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) | ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
        if (idx >= sess->files.size() || len == 0 || len > CHUNK) break;
        auto& f = *sess->files[idx];
        if (f.is_dir || f.finished || off + len > f.size) break;
        if (len != chunk_len(f.size, (uint32_t)(off / CHUNK)) || off % CHUNK != 0) break;
        if (!read_all(s, buf.data(), len)) break;
        {
            std::lock_guard<std::mutex> g(f.io_mu);
            if (!open_part(f)) break;
        }
        if (!write_at(f.fp, off, buf.data(), len)) break;
        {
            std::lock_guard<std::mutex> g(service().mu);
            service().xfer.current_file = f.rel;
        }
        bool became = false;
        {
            std::lock_guard<std::mutex> g(f.map_mu);
            uint32_t ci = (uint32_t)(off / CHUNK);
            if (bit_set(f.bits, ci)) {
                sess->got += len;
                service().xfer_done += len;
                service().xfer_session += len;
                f.unsaved++;
            }
            uint32_t nc = nchunks_of(f.size);
            became = true;
            for (uint32_t i = 0; i < nc; i++) {
                if (!bit_get(f.bits, i)) {
                    became = false;
                    break;
                }
            }
            if (became || f.unsaved >= 8 || (f.unsaved && now_sec() - f.last_map_save >= 0.25)) {
                save_map(f);
                f.unsaved = 0;
                f.last_map_save = now_sec();
            }
        }
        if (became) {
            {
                std::lock_guard<std::mutex> g(f.io_mu);
                finish_file(f);
            }
            std::lock_guard<std::mutex> lk(service().mu);
            service().xfer.finished.push_back(f.rel);
        }
    }
    for (auto& pf : sess->files) {
        std::lock_guard<std::mutex> io(pf->io_mu);
        std::lock_guard<std::mutex> g(pf->map_mu);
        if (pf->unsaved && !pf->finished) {
            save_map(*pf);
            pf->unsaved = 0;
        }
    }
    sock_close(s);
}

void handle_control(sock_t s) {
    set_timeout(s, 15000);
    std::vector<uint8_t> raw;
    if (!recv_msg(s, raw) || raw.empty() || raw[0] != M_HELLO) {
        sock_close(s);
        return;
    }
    R rh{raw.data(), raw.size()};
    uint8_t t = 0;
    uint64_t from_id = 0;
    std::string from_name;
    rh.u8(t);
    if (!rh.u64(from_id) || !rh.str(from_name)) {
        sock_close(s);
        return;
    }
    (void)from_id;
    if (!recv_msg(s, raw) || raw.empty() || raw[0] != M_OFFER) {
        sock_close(s);
        return;
    }
    R ro{raw.data(), raw.size()};
    uint64_t offer_id = 0;
    uint32_t nfiles = 0;
    ro.u8(t);
    if (!ro.u64(offer_id) || !ro.u32(nfiles) || nfiles > 200000) {
        sock_close(s);
        return;
    }
    std::vector<ShareFile> files;
    files.reserve(nfiles);
    uint64_t nbytes = 0;
    for (uint32_t i = 0; i < nfiles; i++) {
        ShareFile f;
        uint8_t flags = 0;
        if (!ro.str(f.rel) || !ro.u64(f.size) || !ro.u8(flags) || !ro.u64(f.content_id)) {
            sock_close(s);
            return;
        }
        f.is_dir = flags & 1;
        if (f.is_dir) f.content_id = 0;
        if (dest_join("x", f.rel).empty()) {
            sock_close(s);
            return;
        }
        if (!f.is_dir) nbytes += f.size;
        files.push_back(std::move(f));
    }

    {
        std::lock_guard<std::mutex> g(service().mu);
        if (xfer_busy_unlocked()) {
            W w;
            w.u8(M_REJECT);
            w.u64(offer_id);
            send_msg(s, w);
            sock_close(s);
            return;
        }
    }

    auto pend = std::make_shared<PendingIn>();
    pend->info.offer_id = offer_id;
    pend->info.from_name = from_name;
    char ip[64]{};
    sockaddr_in sa{};
    socklen_t sl = sizeof(sa);
    getpeername(s, (sockaddr*)&sa, &sl);
    inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
    pend->info.from_ip = ip;
    pend->info.nfiles = nfiles;
    pend->info.nbytes = nbytes;
    pend->files = files;
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().incoming.push_back(pend);
    }

    set_timeout(s, 300000);
    int reply = 0;
    {
        std::unique_lock<std::mutex> lk(pend->mu);
        pend->cv.wait_for(lk, std::chrono::minutes(5), [&] { return pend->reply != 0 || !service().run; });
        reply = pend->reply;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().incoming.erase(std::remove(service().incoming.begin(), service().incoming.end(), pend), service().incoming.end());
    }
    if (reply != 1) {
        W w;
        w.u8(M_REJECT);
        w.u64(offer_id);
        send_msg(s, w);
        sock_close(s);
        return;
    }

    auto sess = prepare_session(offer_id, files);
    if (!sess) {
        W w;
        w.u8(M_REJECT);
        w.u64(offer_id);
        send_msg(s, w);
        sock_close(s);
        return;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().sessions[offer_id] = sess;
        service().xfer = {};
        service().xfer.phase = XferPhase::Transferring;
        service().xfer.sending = false;
        service().xfer.peer_name = from_name;
        service().xfer.peer_ip = ip;
        service().xfer.total = sess->total;
        service().xfer.started = now_sec();
        service().xfer_done = sess->got.load();
        service().xfer_session = 0;
        service().cancel = false;
        service().active_ctrl = s;
    }

    W acc;
    acc.u8(M_ACCEPT);
    acc.u64(offer_id);
    acc.u32((uint32_t)sess->files.size());
    for (auto& f : sess->files) {
        acc.u32((uint32_t)f->bits.size());
        acc.bytes(f->bits);
    }
    if (!send_msg(s, acc)) {
        set_xfer(XferPhase::Error, "发送接受失败");
        sock_close(s);
        return;
    }

    set_timeout(s, 600000);
    bool ok = false;
    while (service().run && !service().cancel) {
        if (!recv_msg(s, raw) || raw.empty()) break;
        if (raw[0] == M_CANCEL) break;
        if (raw[0] == M_DONE) {
            auto deadline = now_sec() + 120;
            while (service().run && sess->got.load() < sess->total && now_sec() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (sess->got.load() >= sess->total) {
                for (auto& f : sess->files) {
                    std::lock_guard<std::mutex> g(f->io_mu);
                    if (!f->finished && !f->is_dir) finish_file(*f);
                }
                W done;
                done.u8(M_COMPLETE);
                done.u64(offer_id);
                send_msg(s, done);
                ok = true;
            }
            break;
        }
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().sessions.erase(offer_id);
        if (service().active_ctrl == s) service().active_ctrl = INVALID_SOCK;
        service().xfer.done = sess->got.load();
        if (ok) service().xfer.phase = XferPhase::Done;
        else if (service().xfer.phase == XferPhase::Transferring) {
            service().xfer.phase = XferPhase::Error;
            service().xfer.error = service().cancel ? "已取消" : "传输中断";
        }
    }
    sock_close(s);
}

}  // namespace share
