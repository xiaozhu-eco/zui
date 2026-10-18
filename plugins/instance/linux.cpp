#include "ext/slots.h"
#include "plugins/instance/endpoint.h"
#include "plugins/instance/unix_claim.h"

#include <cstdlib>
#include <string>

namespace zui {
namespace {

#if defined(__linux__) && !defined(__ANDROID__)

std::string runtime_socket(std::string_view identity) {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return instance_detail::linux_socket_path(runtime != nullptr ? runtime : "", identity);
}

class LinuxInstance final : public InstanceService {
public:
    bool claim(std::string_view identity) override {
        path_ = runtime_socket(identity);
        if (!claim_.claim(path_)) {
            return false;
        }
        identity_.assign(identity.begin(), identity.end());
        return true;
    }

    bool signal_primary(std::string_view identity, std::string_view args) override {
        return claim_.signal(runtime_socket(identity), args);
    }

    std::string take_args(std::string_view identity) override {
        if (identity_.empty() || identity != identity_) {
            return {};
        }
        return claim_.take();
    }

private:
    instance_detail::UnixClaim claim_;
    std::string path_;
    std::string identity_;
};

#endif

}  // namespace

void register_linux_instance(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_instance(std::make_unique<LinuxInstance>());
#else
    (void)registry;
#endif
}

}  // namespace zui
