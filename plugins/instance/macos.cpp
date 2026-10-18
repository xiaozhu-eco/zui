#include "ext/slots.h"
#include "plugins/instance/endpoint.h"
#include "plugins/instance/unix_claim.h"

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

std::string home_socket(std::string_view identity) {
    const char* home = std::getenv("HOME");
    return instance_detail::macos_socket_path(home != nullptr ? home : "", identity);
}

class MacInstance final : public InstanceService {
public:
    bool claim(std::string_view identity) override {
        path_ = home_socket(identity);
        if (!claim_.claim(path_)) {
            return false;
        }
        identity_.assign(identity.begin(), identity.end());
        return true;
    }

    bool signal_primary(std::string_view identity, std::string_view args) override {
        return claim_.signal(home_socket(identity), args);
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

void register_macos_instance(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_instance(std::make_unique<MacInstance>());
#else
    (void)registry;
#endif
}

}  // namespace zui
