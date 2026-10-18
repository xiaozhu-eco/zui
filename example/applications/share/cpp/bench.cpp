#include "bench.h"

#include "bench_detail.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace share {

/// One bench verb. `bench_main` picks the object from argv; the verb owns the
/// argument check and the call into the shared transfer helpers.
class BenchCommand {
public:
    virtual ~BenchCommand() = default;
    virtual int run(int argc, char** argv) = 0;
};

class RecvCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        const std::string dir = argc >= 3 ? argv[2] : join_path(default_save_dir(), "bench");
        return run_recv(dir, argc >= 4 ? argv[3] : nullptr);
    }
};

class SendCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        if (argc < 3) {
            usage();
            return 1;
        }
        const std::string tmp = join_path(home_dir(), ".zui-share-bench-send");
        if (start_sender(tmp) != 0) {
            return 1;
        }
        std::printf("waiting for a peer...\n");
        std::fflush(stdout);
        const Peer peer = wait_any_peer(8.0);
        if (peer.ip.empty()) {
            std::fprintf(stderr, "no peer discovered in 8s (run `recv` first)\n");
            net_stop();
            return 1;
        }
        const int rc = run_send_to(peer, argv[2]);
        net_stop();
        return rc;
    }
};

class SendToCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        if (argc < 5) {
            usage();
            return 1;
        }
        if (start_sender(join_path(home_dir(), ".zui-share-bench-send")) != 0) {
            return 1;
        }
        Peer peer;
        peer.name = "bench-peer";
        peer.ip = argv[2];
        peer.port = static_cast<std::uint16_t>(std::atoi(argv[3]));
        const int rc = run_send_to(peer, argv[4]);
        net_stop();
        return rc;
    }
};

class GenCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        if (argc < 4) {
            usage();
            return 1;
        }
        const std::uint64_t size = parse_size(argv[3]);
        if (size == 0 || !write_payload(argv[2], size)) {
            std::fprintf(stderr, "cannot write %s\n", argv[2]);
            return 1;
        }
        std::printf("wrote %s (%s)\n", argv[2], format_bytes(size).c_str());
        return 0;
    }
};

class CheckCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        if (argc < 4) {
            usage();
            return 1;
        }
        const std::uint64_t size = parse_size(argv[3]);
        if (size == 0 || !verify_payload(argv[2], size)) {
            std::fprintf(stderr, "verify failed: %s\n", argv[2]);
            return 1;
        }
        std::printf("verify ok: %s (%s)\n", argv[2], format_bytes(size).c_str());
        return 0;
    }
};

class PartialCommand final : public BenchCommand {
public:
    int run(int argc, char** argv) override {
        if (argc < 6) {
            usage();
            return 1;
        }
        return run_partial(argv[2], argv[3], argv[4], std::atof(argv[5]));
    }
};

int bench_main(int argc, char** argv) {
    const char* verb = argc >= 2 ? argv[1] : nullptr;
    if (verb == nullptr || std::strcmp(verb, "-h") == 0 || std::strcmp(verb, "--help") == 0) {
        usage();
        return verb == nullptr ? 1 : 0;
    }
    std::unique_ptr<BenchCommand> command;
    if (std::strcmp(verb, "recv") == 0) {
        command = std::make_unique<RecvCommand>();
    } else if (std::strcmp(verb, "send") == 0) {
        command = std::make_unique<SendCommand>();
    } else if (std::strcmp(verb, "sendto") == 0) {
        command = std::make_unique<SendToCommand>();
    } else if (std::strcmp(verb, "gen") == 0) {
        command = std::make_unique<GenCommand>();
    } else if (std::strcmp(verb, "check") == 0) {
        command = std::make_unique<CheckCommand>();
    } else if (std::strcmp(verb, "partial") == 0) {
        command = std::make_unique<PartialCommand>();
    }
    if (!command) {
        usage();
        return 1;
    }
    return command->run(argc, argv);
}

}  // namespace share
