#include "net_internal.h"

namespace share {

void handle_client(sock_t s) {
    uint8_t mag[4];
    set_timeout(s, 15000);
    tune_sock(s, true);
    if (!read_all(s, mag, 4)) {
        sock_close(s);
        return;
    }
    if (memcmp(mag, "XZC1", 4) == 0) handle_control(s);
    else if (memcmp(mag, "XZD1", 4) == 0) handle_data(s);
    else sock_close(s);
}

void listen_thread() {
    while (service().run) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(service().listen, &r);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int n = select((int)service().listen + 1, &r, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(service().listen, &r)) {
            sockaddr_in src{};
            socklen_t sl = sizeof(src);
            sock_t c = accept(service().listen, (sockaddr*)&src, &sl);
            if (c != INVALID_SOCK) std::thread(handle_client, c).detach();
        }
    }
}

}  // namespace share
