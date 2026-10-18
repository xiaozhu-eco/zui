#include "ext/slots.h"
#include "plugins/instance/endpoint.h"
#include "plugins/win32_msg.h"

#include <string>

namespace zui {
namespace {

#if defined(_WIN32)

HANDLE g_mutex = nullptr;
HANDLE g_pipe = nullptr;
std::string g_identity;

class Win32Instance final : public InstanceService {
public:
    bool claim(std::string_view identity) override {
        const std::wstring name = plugin_detail::widen(instance_detail::win32_mutex_name(identity));
        g_mutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (g_mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
            return false;
        }
        const std::wstring pipe = plugin_detail::widen(instance_detail::win32_pipe_name(identity));
        g_pipe = CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_INBOUND,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 4096, 4096, 0, nullptr);
        if (g_pipe == nullptr || g_pipe == INVALID_HANDLE_VALUE) {
            return false;
        }
        g_identity.assign(identity.begin(), identity.end());
        return true;
    }

    bool signal_primary(std::string_view identity, std::string_view args) override {
        const std::wstring pipe = plugin_detail::widen(instance_detail::win32_pipe_name(identity));
        if (!WaitNamedPipeW(pipe.c_str(), 2000)) {
            return false;
        }
        HANDLE handle = CreateFileW(pipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD wrote = 0;
        const BOOL ok = WriteFile(handle, args.data(), static_cast<DWORD>(args.size()), &wrote, nullptr);
        CloseHandle(handle);
        return ok && wrote == args.size();
    }

    std::string take_args(std::string_view identity) override {
        if (g_identity.empty() || identity != g_identity || g_pipe == nullptr || g_pipe == INVALID_HANDLE_VALUE) {
            return {};
        }
        char buffer[4096];
        DWORD read = 0;
        if (!ReadFile(g_pipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
            return {};
        }
        return std::string(buffer, buffer + read);
    }
};

#endif

}  // namespace

void register_win32_instance(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_instance(std::make_unique<Win32Instance>());
#else
    (void)registry;
#endif
}

}  // namespace zui
