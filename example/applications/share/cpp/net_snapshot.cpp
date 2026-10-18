#include "net_internal.h"

namespace share {

bool xfer_busy_unlocked() {
    auto p = service().xfer.phase;
    return p == XferPhase::WaitingAccept || p == XferPhase::Transferring;
}

void set_xfer(XferPhase phase, const std::string& err) {
    std::lock_guard<std::mutex> g(service().mu);
    service().xfer.phase = phase;
    if (!err.empty()) service().xfer.error = err;
}

uint64_t bytes_from_bits(uint64_t size, const std::vector<uint8_t>& bits) {
    uint64_t w = 0;
    uint32_t nc = nchunks_of(size);
    for (uint32_t i = 0; i < nc; i++) {
        if (bit_get(bits, i)) w += chunk_len(size, i);
    }
    return w;
}

void save_map(const RecvFile& f) {
    FILE* fp = fopen_u8(f.map_path, "wb");
    if (!fp) return;
    uint32_t nb = (uint32_t)f.bits.size();
    // XZM2: size + content_id + bitset length
    fwrite("XZM2", 1, 4, fp);
    uint8_t tmp[20];
    wu64(tmp, f.size);
    wu64(tmp + 8, f.content_id);
    tmp[16] = (uint8_t)nb;
    tmp[17] = (uint8_t)(nb >> 8);
    tmp[18] = (uint8_t)(nb >> 16);
    tmp[19] = (uint8_t)(nb >> 24);
    fwrite(tmp, 1, 20, fp);
    if (nb) fwrite(f.bits.data(), 1, nb, fp);
    fclose(fp);
}

MapPeek peek_map(const std::string& path) {
    MapPeek m;
    FILE* fp = fopen_u8(path, "rb");
    if (!fp) return m;
    char mag[4];
    if (fread(mag, 1, 4, fp) != 4) {
        fclose(fp);
        return m;
    }
    if (memcmp(mag, "XZM2", 4) == 0) {
        uint8_t tmp[20];
        if (fread(tmp, 1, 20, fp) == 20) {
            m.size = ru64(tmp);
            m.content_id = ru64(tmp + 8);
            m.ok = true;
        }
    } else if (memcmp(mag, "XZM1", 4) == 0) {
        uint8_t tmp[12];
        if (fread(tmp, 1, 12, fp) == 12) {
            m.size = ru64(tmp);
            m.content_id = 0;  // legacy: unknown identity
            m.ok = true;
        }
    }
    fclose(fp);
    return m;
}

std::vector<uint8_t> load_map(const std::string& path, uint64_t size, uint64_t content_id) {
    uint32_t nc = nchunks_of(size);
    uint32_t nb = (nc + 7) / 8;
    std::vector<uint8_t> bits(nb, 0);
    FILE* fp = fopen_u8(path, "rb");
    if (!fp) return bits;
    char mag[4];
    if (fread(mag, 1, 4, fp) != 4) {
        fclose(fp);
        return bits;
    }
    uint64_t sz = 0, cid = 0;
    uint32_t n = 0;
    bool ok = false;
    if (memcmp(mag, "XZM2", 4) == 0) {
        uint8_t tmp[20];
        ok = fread(tmp, 1, 20, fp) == 20;
        if (ok) {
            sz = ru64(tmp);
            cid = ru64(tmp + 8);
            n = (uint32_t)tmp[16] | ((uint32_t)tmp[17] << 8) | ((uint32_t)tmp[18] << 16) | ((uint32_t)tmp[19] << 24);
        }
    } else if (memcmp(mag, "XZM1", 4) == 0) {
        uint8_t tmp[12];
        ok = fread(tmp, 1, 12, fp) == 12;
        if (ok) {
            sz = ru64(tmp);
            cid = 0;
            n = (uint32_t)tmp[8] | ((uint32_t)tmp[9] << 8) | ((uint32_t)tmp[10] << 16) | ((uint32_t)tmp[11] << 24);
        }
    }
    // Resume only when size matches; content_id must match when both sides know it.
    bool id_ok = (cid == 0 || content_id == 0 || cid == content_id);
    if (ok && sz == size && id_ok && n == nb && nb) fread(bits.data(), 1, nb, fp);
    fclose(fp);
    return bits;
}
XferView Snapshot::current() const {
    std::lock_guard<std::mutex> g(service().mu);
    XferView v = service().xfer;
    v.done = service().xfer_done.load();
    v.session_bytes = service().xfer_session.load();
    return v;
}

void Snapshot::clear() {
    std::lock_guard<std::mutex> g(service().mu);
    if (xfer_busy_unlocked()) return;
    service().xfer = {};
    service().xfer_done = 0;
    service().xfer_session = 0;
}

void Snapshot::stage(const XferView& view) {
    std::lock_guard<std::mutex> g(service().mu);
    service().xfer = view;
    service().xfer_done = view.done;
    service().xfer_session = view.session_bytes;
}

void Snapshot::set_save_dir(std::string dir) {
    std::lock_guard<std::mutex> g(service().mu);
    service().save = std::move(dir);
}


}  // namespace share
