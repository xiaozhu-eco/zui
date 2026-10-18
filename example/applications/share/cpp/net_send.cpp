#include "net_internal.h"

namespace share {

void sender_worker(Peer peer, uint64_t offer_id, const std::vector<ShareFile>* files,
                          const std::vector<Job>* jobs, std::vector<std::atomic<int>>* remaining,
                          size_t job_lo, size_t job_hi) {
    sock_t s = tcp_connect(peer.ip, peer.port, 5000);
    if (s == INVALID_SOCK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        s = tcp_connect(peer.ip, peer.port, 5000);
    }
    if (s == INVALID_SOCK) {
        service().cancel = true;
        set_xfer(XferPhase::Error, "数据连接失败");
        return;
    }
    set_timeout(s, 60000);
    if (!write_all(s, "XZD1", 4)) {
        sock_close(s);
        return;
    }
    uint8_t idb[8];
    wu64(idb, offer_id);
    if (!write_all(s, idb, 8)) {
        sock_close(s);
        return;
    }
    std::vector<char> buf(17 + CHUNK);
    FILE* fp = nullptr;
    int cur = -1;
    for (size_t i = job_lo; i < job_hi && !service().cancel; i++) {
        const Job& job = (*jobs)[i];
        if (job.file_index != cur) {
            if (fp) fclose(fp);
            fp = fopen_u8((*files)[job.file_index].abs, "rb");
            cur = job.file_index;
            if (!fp) {
                service().cancel = true;
                set_xfer(XferPhase::Error, "读取文件失败");
                break;
            }
            setvbuf(fp, nullptr, _IONBF, 0);
            std::lock_guard<std::mutex> g(service().mu);
            service().xfer.current_file = (*files)[job.file_index].rel;
        }
        if (!read_at(fp, job.off, buf.data() + 17, job.len)) {
            service().cancel = true;
            set_xfer(XferPhase::Error, "读取文件失败");
            break;
        }
        buf[0] = (char)D_CHUNK;
        buf[1] = (char)job.file_index;
        buf[2] = (char)(job.file_index >> 8);
        buf[3] = (char)(job.file_index >> 16);
        buf[4] = (char)(job.file_index >> 24);
        wu64((uint8_t*)buf.data() + 5, job.off);
        buf[13] = (char)job.len;
        buf[14] = (char)(job.len >> 8);
        buf[15] = (char)(job.len >> 16);
        buf[16] = (char)(job.len >> 24);
        if (!write_all(s, buf.data(), 17 + job.len)) {
            service().cancel = true;
            set_xfer(XferPhase::Error, "发送中断");
            break;
        }
        service().xfer_done += job.len;
        service().xfer_session += job.len;
        if (remaining != nullptr && (*remaining)[job.file_index].fetch_sub(1) == 1) {
            std::lock_guard<std::mutex> g(service().mu);
            service().xfer.finished.push_back((*files)[job.file_index].rel);
        }
    }
    uint8_t end = D_END;
    write_all(s, &end, 1);
    if (fp) fclose(fp);
    sock_close(s);
}

void sender_fn(Peer peer, std::vector<ShareFile> files) {
    sock_t s = tcp_connect(peer.ip, peer.port, 5000);
    if (s == INVALID_SOCK) {
        set_xfer(XferPhase::Error, "连不上对方");
        return;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().active_ctrl = s;
    }
    set_timeout(s, 300000);
    if (!write_all(s, "XZC1", 4)) {
        set_xfer(XferPhase::Error, "连接断开");
        sock_close(s);
        return;
    }
    W hello;
    hello.u8(M_HELLO);
    hello.u64(service().id);
    hello.str(service().name);
    W offer;
    offer.u8(M_OFFER);
    uint64_t offer_id = random_u64();
    offer.u64(offer_id);
    offer.u32((uint32_t)files.size());
    uint64_t total = 0;
    for (auto& f : files) {
        offer.str(f.rel);
        offer.u64(f.size);
        offer.u8(f.is_dir ? 1 : 0);
        offer.u64(f.is_dir ? 0 : f.content_id);
        if (!f.is_dir) total += f.size;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().xfer.total = total;
        service().xfer.started = now_sec();
    }
    if (!send_msg(s, hello) || !send_msg(s, offer)) {
        set_xfer(XferPhase::Error, "发送请求失败");
        sock_close(s);
        return;
    }
    std::vector<uint8_t> raw;
    if (!recv_msg(s, raw) || raw.empty()) {
        set_xfer(service().cancel ? XferPhase::Error : XferPhase::Error, service().cancel ? "已取消" : "等待超时");
        sock_close(s);
        return;
    }
    if (raw[0] == M_REJECT) {
        set_xfer(XferPhase::Rejected, "对方拒绝了");
        sock_close(s);
        return;
    }
    if (raw[0] != M_ACCEPT) {
        set_xfer(XferPhase::Error, "协议错误");
        sock_close(s);
        return;
    }
    R ra{raw.data(), raw.size()};
    uint8_t tt = 0;
    uint64_t oid = 0;
    uint32_t nf = 0;
    ra.u8(tt);
    ra.u64(oid);
    ra.u32(nf);
    if (nf != files.size()) {
        set_xfer(XferPhase::Error, "文件列表不匹配");
        sock_close(s);
        return;
    }
    std::vector<Job> jobs;
    uint64_t already = 0;
    for (uint32_t i = 0; i < nf; i++) {
        uint32_t nb = 0;
        if (!ra.u32(nb) || !ra.left(nb)) {
            set_xfer(XferPhase::Error, "协议错误");
            sock_close(s);
            return;
        }
        std::vector<uint8_t> bits(ra.p + ra.i, ra.p + ra.i + nb);
        ra.i += nb;
        if (files[i].is_dir || files[i].size == 0) continue;
        uint32_t nc = nchunks_of(files[i].size);
        for (uint32_t c = 0; c < nc; c++) {
            if (bit_get(bits, c)) already += chunk_len(files[i].size, c);
            else {
                Job j;
                j.file_index = (int)i;
                j.chunk_index = c;
                j.off = (uint64_t)c * CHUNK;
                j.len = chunk_len(files[i].size, c);
                jobs.push_back(j);
            }
        }
    }
    service().xfer_done = already;
    service().xfer_session = 0;
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().xfer.phase = XferPhase::Transferring;
        service().xfer.done = already;
    }

    int nw = (int)std::min((size_t)NWORKERS, jobs.size());
    // One countdown per file so a worker that sends a file's last chunk can report it finished.
    std::vector<std::atomic<int>> remaining(files.size());
    for (size_t i = 0; i < files.size(); ++i) remaining[i].store(0);
    for (const Job& j : jobs) remaining[(size_t)j.file_index].fetch_add(1);
    std::vector<std::thread> ws;
    for (int i = 0; i < nw; i++) {
        size_t lo = jobs.size() * (size_t)i / (size_t)nw;
        size_t hi = jobs.size() * (size_t)(i + 1) / (size_t)nw;
        if (lo >= hi) continue;
        ws.emplace_back(sender_worker, peer, offer_id, &files, &jobs, &remaining, lo, hi);
    }
    for (auto& th : ws) th.join();

    if (service().cancel) {
        W c;
        c.u8(M_CANCEL);
        c.u64(offer_id);
        send_msg(s, c);
        if (service().xfer.phase == XferPhase::Transferring) set_xfer(XferPhase::Error, "已取消");
        sock_close(s);
        std::lock_guard<std::mutex> g(service().mu);
        if (service().active_ctrl == s) service().active_ctrl = INVALID_SOCK;
        return;
    }
    W done;
    done.u8(M_DONE);
    done.u64(offer_id);
    set_timeout(s, 120000);
    if (!send_msg(s, done) || !recv_msg(s, raw) || raw.empty() || raw[0] != M_COMPLETE) {
        set_xfer(XferPhase::Error, "对方未确认完成");
    } else {
        std::lock_guard<std::mutex> g(service().mu);
        service().xfer.phase = XferPhase::Done;
        service().xfer.done = service().xfer.total;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        if (service().active_ctrl == s) service().active_ctrl = INVALID_SOCK;
    }
    sock_close(s);
}

}  // namespace share
