#include "net_internal.h"

namespace share {

void send_beacons(sock_t udp, const std::vector<uint8_t>& pkt) {
    auto send1 = [&](in_addr addr) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(UDP_PORT);
        a.sin_addr = addr;
        sendto(udp, (const char*)pkt.data(), (int)pkt.size(), 0, (sockaddr*)&a, sizeof(a));
    };
    in_addr any{};
    inet_pton(AF_INET, "255.255.255.255", &any);
    send1(any);
    inet_pton(AF_INET, MCAST, &any);
    send1(any);
#ifdef HAS_IFADDRS
    ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (auto* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
            if ((p->ifa_flags & IFF_BROADCAST) && p->ifa_broadaddr) {
                send1(((sockaddr_in*)p->ifa_broadaddr)->sin_addr);
            }
        }
        freeifaddrs(ifa);
    }
#endif
}

void disc_thread() {
    sock_t udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCK) return;
    int yes = 1;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    setsockopt(udp, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(UDP_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp, (sockaddr*)&a, sizeof(a)) != 0) {
        sock_close(udp);
        return;
    }
    ip_mreq m{};
    inet_pton(AF_INET, MCAST, &m.imr_multiaddr);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&m, sizeof(m));
    int ttl = 1;
    setsockopt(udp, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));
    set_timeout(udp, 200);

    double last_send = 0;
    while (service().run) {
        double t = now_sec();
        if (t - last_send >= 1.0) {
            std::string name;
            uint16_t port = 0;
            uint64_t id = 0;
            {
                std::lock_guard<std::mutex> g(service().mu);
                name = service().name;
                port = service().tcp_port;
                id = service().id;
            }
            if (name.size() > 64) name.resize(64);
            std::vector<uint8_t> pkt;
            pkt.insert(pkt.end(), {'X', 'Z', 'S', 'H', 1});
            pkt.push_back((uint8_t)port);
            pkt.push_back((uint8_t)(port >> 8));
            uint8_t idb[8];
            wu64(idb, id);
            pkt.insert(pkt.end(), idb, idb + 8);
            pkt.push_back((uint8_t)name.size());
            pkt.insert(pkt.end(), name.begin(), name.end());
            send_beacons(udp, pkt);
            last_send = t;
            std::lock_guard<std::mutex> g(service().mu);
            service().peers.erase(std::remove_if(service().peers.begin(), service().peers.end(),
                                         [t](const PeerEx& p) { return t - p.last > 6.0; }),
                          service().peers.end());
        }
        uint8_t buf[512];
        sockaddr_in src{};
        socklen_t sl = sizeof(src);
        int n = (int)recvfrom(udp, (char*)buf, sizeof(buf), 0, (sockaddr*)&src, &sl);
        if (n < 16 || memcmp(buf, "XZSH", 4) != 0 || buf[4] != 1) continue;
        uint8_t nl = buf[15];
        if (16 + (int)nl > n) continue;
        uint64_t id = ru64(buf + 7);
        if (id == service().id) continue;
        uint16_t port = ru16(buf + 5);
        std::string name((char*)buf + 16, (char*)buf + 16 + nl);
        char ip[64]{};
        inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
        double now = now_sec();
        std::lock_guard<std::mutex> g(service().mu);
        bool found = false;
        for (auto& p : service().peers) {
            if (p.id == id) {
                p.name = name;
                p.ip = ip;
                p.port = port;
                p.last = now;
                found = true;
                break;
            }
        }
        if (!found) {
            PeerEx p;
            p.id = id;
            p.name = name;
            p.ip = ip;
            p.port = port;
            p.last = now;
            service().peers.push_back(std::move(p));
        }
    }
    sock_close(udp);
}

bool Discovery::start(uint64_t my_id, std::string name, std::string save_dir) {
    {
        std::lock_guard<std::mutex> g(service().mu);
        service().id = my_id;
        service().name = std::move(name);
        service().save = std::move(save_dir);
        // A previous run's net_stop leaves service().cancel set. The next listen must be able to finish
        // a transfer, and it must not inherit staged peers, offers, or a snapshot.
        service().cancel = false;
        if (service().run) return service().listen != INVALID_SOCK;
        service().peers.clear();
        service().incoming.clear();
        if (service().active_ctrl == INVALID_SOCK) {
            service().xfer = {};
            service().xfer_done = 0;
            service().xfer_session = 0;
        }
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    uint16_t port = TCP_PORT0;
    sock_t ls = bind_listen(port);
    if (ls == INVALID_SOCK) return false;
    service().listen = ls;
    service().tcp_port = port;
    service().run = true;
    service().disc_th = std::thread(disc_thread);
    service().listen_th = std::thread(listen_thread);
    return true;
}

void Discovery::stop() {
    if (!service().run.exchange(false)) return;
    service().cancel = true;
    if (service().listen != INVALID_SOCK) {
        sock_close(service().listen);
        service().listen = INVALID_SOCK;
    }
    {
        std::lock_guard<std::mutex> g(service().mu);
        if (service().active_ctrl != INVALID_SOCK) shutdown(service().active_ctrl, SHUT_RDWR);
        for (auto& p : service().incoming) {
            std::lock_guard<std::mutex> lk(p->mu);
            if (p->reply == 0) p->reply = -1;
            p->cv.notify_all();
        }
    }
    if (service().disc_th.joinable()) service().disc_th.join();
    if (service().listen_th.joinable()) service().listen_th.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

uint16_t Discovery::tcp_port() const { return service().tcp_port; }

void Discovery::set_name(std::string name) {
    std::lock_guard<std::mutex> g(service().mu);
    service().name = std::move(name);
}

std::vector<Peer> Discovery::peers() const {
    std::lock_guard<std::mutex> g(service().mu);
    std::vector<Peer> out;
    out.reserve(service().peers.size());
    for (auto& p : service().peers) out.push_back(p);
    return out;
}

void Discovery::stage(Peer peer) {
    std::lock_guard<std::mutex> g(service().mu);
    const std::uint64_t id = peer.id;
    service().peers.erase(std::remove_if(service().peers.begin(), service().peers.end(),
                                 [&](const PeerEx& row) { return row.id == id || row.port == 0; }),
                  service().peers.end());
    PeerEx row;
    static_cast<Peer&>(row) = std::move(peer);
    row.last = now_sec();
    service().peers.push_back(std::move(row));
}


}  // namespace share
