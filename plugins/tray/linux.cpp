#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/tray/item.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace zui {
namespace {

#if defined(__linux__) && !defined(__ANDROID__)

int g_badge = 0;
bool g_badge_set = false;
plugin::Handler g_activate;
std::mutex g_mu;
std::string g_tip;
std::string g_id;
bool g_service_started = false;

bool gdbus(const std::string& cmd) {
    std::string out;
    int code = -1;
    return backends::posix_util::run_command_capture(cmd, out, code);
}

struct DBusFns {
    void* (*error_init)(void*) = nullptr;
    int (*error_is_set)(void*) = nullptr;
    void (*error_free)(void*) = nullptr;
    void* (*bus_get)(int, void*) = nullptr;
    int (*request_name)(void*, const char*, unsigned, void*) = nullptr;
    int (*register_path)(void*, const char*, const void*, void*) = nullptr;
    void* (*new_call)(const char*, const char*, const char*, const char*) = nullptr;
    int (*append_args)(void*, int, ...) = nullptr;
    void* (*send_block)(void*, void*, int, void*) = nullptr;
    void* (*new_return)(void*) = nullptr;
    int (*send)(void*, void*, unsigned*) = nullptr;
    void (*flush)(void*) = nullptr;
    void (*unref_msg)(void*) = nullptr;
    const char* (*iface)(void*) = nullptr;
    const char* (*member)(void*) = nullptr;
    int (*read_write)(void*, int) = nullptr;
    void (*iter_init_append)(void*, void*) = nullptr;
    int (*iter_open)(void*, int, const char*, void*) = nullptr;
    int (*iter_basic)(void*, int, const void*) = nullptr;
    int (*iter_close)(void*, void*) = nullptr;
    bool loaded = false;
};

DBusFns g_dbus;

bool load_dbus() {
    if (g_dbus.loaded) {
        return g_dbus.bus_get != nullptr;
    }
    g_dbus.loaded = true;
    void* lib = dlopen("libdbus-1.so.3", RTLD_LAZY);
    if (lib == nullptr) {
        lib = dlopen("libdbus-1.so", RTLD_LAZY);
    }
    if (lib == nullptr) {
        return false;
    }
    g_dbus.error_init = reinterpret_cast<void* (*)(void*)>(dlsym(lib, "dbus_error_init"));
    g_dbus.error_is_set = reinterpret_cast<int (*)(void*)>(dlsym(lib, "dbus_error_is_set"));
    g_dbus.error_free = reinterpret_cast<void (*)(void*)>(dlsym(lib, "dbus_error_free"));
    g_dbus.bus_get = reinterpret_cast<void* (*)(int, void*)>(dlsym(lib, "dbus_bus_get"));
    g_dbus.request_name =
        reinterpret_cast<int (*)(void*, const char*, unsigned, void*)>(dlsym(lib, "dbus_bus_request_name"));
    g_dbus.register_path = reinterpret_cast<int (*)(void*, const char*, const void*, void*)>(
        dlsym(lib, "dbus_connection_register_object_path"));
    g_dbus.new_call = reinterpret_cast<void* (*)(const char*, const char*, const char*, const char*)>(
        dlsym(lib, "dbus_message_new_method_call"));
    g_dbus.append_args = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "dbus_message_append_args"));
    g_dbus.send_block = reinterpret_cast<void* (*)(void*, void*, int, void*)>(
        dlsym(lib, "dbus_connection_send_with_reply_and_block"));
    g_dbus.new_return = reinterpret_cast<void* (*)(void*)>(dlsym(lib, "dbus_message_new_method_return"));
    g_dbus.send = reinterpret_cast<int (*)(void*, void*, unsigned*)>(dlsym(lib, "dbus_connection_send"));
    g_dbus.flush = reinterpret_cast<void (*)(void*)>(dlsym(lib, "dbus_connection_flush"));
    g_dbus.unref_msg = reinterpret_cast<void (*)(void*)>(dlsym(lib, "dbus_message_unref"));
    g_dbus.iface = reinterpret_cast<const char* (*)(void*)>(dlsym(lib, "dbus_message_get_interface"));
    g_dbus.member = reinterpret_cast<const char* (*)(void*)>(dlsym(lib, "dbus_message_get_member"));
    g_dbus.read_write =
        reinterpret_cast<int (*)(void*, int)>(dlsym(lib, "dbus_connection_read_write_dispatch"));
    g_dbus.iter_init_append =
        reinterpret_cast<void (*)(void*, void*)>(dlsym(lib, "dbus_message_iter_init_append"));
    g_dbus.iter_open =
        reinterpret_cast<int (*)(void*, int, const char*, void*)>(dlsym(lib, "dbus_message_iter_open_container"));
    g_dbus.iter_basic =
        reinterpret_cast<int (*)(void*, int, const void*)>(dlsym(lib, "dbus_message_iter_append_basic"));
    g_dbus.iter_close =
        reinterpret_cast<int (*)(void*, void*)>(dlsym(lib, "dbus_message_iter_close_container"));
    return g_dbus.bus_get != nullptr && g_dbus.register_path != nullptr && g_dbus.new_call != nullptr &&
           g_dbus.read_write != nullptr && g_dbus.member != nullptr;
}

void reply_empty(void* conn, void* msg) {
    if (g_dbus.new_return == nullptr || g_dbus.send == nullptr) {
        return;
    }
    void* reply = g_dbus.new_return(msg);
    if (reply == nullptr) {
        return;
    }
    g_dbus.send(conn, reply, nullptr);
    if (g_dbus.flush != nullptr) {
        g_dbus.flush(conn);
    }
    if (g_dbus.unref_msg != nullptr) {
        g_dbus.unref_msg(reply);
    }
}

void reply_string(void* conn, void* msg, const char* value) {
    if (g_dbus.new_return == nullptr || g_dbus.iter_init_append == nullptr || g_dbus.iter_open == nullptr ||
        g_dbus.iter_basic == nullptr || g_dbus.iter_close == nullptr) {
        reply_empty(conn, msg);
        return;
    }
    void* reply = g_dbus.new_return(msg);
    if (reply == nullptr) {
        return;
    }
    alignas(8) unsigned char iter[256];
    alignas(8) unsigned char sub[256];
    std::memset(iter, 0, sizeof(iter));
    std::memset(sub, 0, sizeof(sub));
    g_dbus.iter_init_append(reply, iter);
    if (g_dbus.iter_open(iter, static_cast<int>('v'), "s", sub)) {
        g_dbus.iter_basic(sub, static_cast<int>('s'), &value);
        g_dbus.iter_close(iter, sub);
    }
    g_dbus.send(conn, reply, nullptr);
    if (g_dbus.flush != nullptr) {
        g_dbus.flush(conn);
    }
    if (g_dbus.unref_msg != nullptr) {
        g_dbus.unref_msg(reply);
    }
}

int sni_message(void* conn, void* msg, void*) {
    const char* member = g_dbus.member != nullptr ? g_dbus.member(msg) : nullptr;
    if (member == nullptr) {
        return 1;
    }
    if (std::strcmp(member, "Activate") == 0 || std::strcmp(member, "SecondaryActivate") == 0 ||
        std::strcmp(member, "ContextMenu") == 0) {
        plugin::Handler handler;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            handler = g_activate;
        }
        if (handler) {
            handler();
        }
        reply_empty(conn, msg);
        return 0;
    }
    if (std::strcmp(member, "Get") == 0) {
        std::string tip;
        std::string id;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            tip = g_tip;
            id = g_id;
        }
        const char* iface = g_dbus.iface != nullptr ? g_dbus.iface(msg) : nullptr;
        const char* value = "zui";
        if (iface != nullptr && std::strcmp(iface, "org.freedesktop.DBus.Properties") == 0) {
            value = tip.c_str();
        }
        if (id.empty()) {
            value = tip.empty() ? "zui" : tip.c_str();
        }
        reply_string(conn, msg, tip.empty() ? id.c_str() : tip.c_str());
        (void)value;
        return 0;
    }
    if (std::strcmp(member, "Scroll") == 0) {
        reply_empty(conn, msg);
        return 0;
    }
    return 1;
}

void sni_unregistered(void*, void*) {}

struct VTable {
    void (*unregister_function)(void*, void*);
    int (*message_function)(void*, void*, void*);
    void (*pad1)(void*);
    void (*pad2)(void*);
    void (*pad3)(void*);
    void (*pad4)(void*);
};

void service_loop(const std::shared_ptr<std::string>& failure, const std::shared_ptr<std::atomic<bool>>& ready) {
    auto fail = [&](const char* why) {
        *failure = why;
        ready->store(true);
    };
    alignas(16) unsigned char err[128];
    std::memset(err, 0, sizeof(err));
    if (g_dbus.error_init != nullptr) {
        g_dbus.error_init(err);
    }
    void* conn = g_dbus.bus_get(0, err);
    if (conn == nullptr) {
        fail("session bus is not available");
        return;
    }
    const int owner = g_dbus.request_name != nullptr ? g_dbus.request_name(conn, "org.zui.StatusNotifier", 4u, err) : 0;
    if (owner != 1 && owner != 4) {
        fail("cannot own org.zui.StatusNotifier");
        return;
    }
    VTable table{};
    table.unregister_function = sni_unregistered;
    table.message_function = sni_message;
    if (!g_dbus.register_path(conn, "/StatusNotifierItem", &table, nullptr)) {
        fail("cannot export the status notifier item");
        return;
    }
    void* call = g_dbus.new_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                 "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    if (call == nullptr) {
        fail("cannot build the status notifier registration");
        return;
    }
    const char* name = "org.zui.StatusNotifier";
    if (g_dbus.append_args == nullptr || !g_dbus.append_args(call, static_cast<int>('s'), &name, 0)) {
        if (g_dbus.unref_msg != nullptr) {
            g_dbus.unref_msg(call);
        }
        fail("cannot describe the status notifier registration");
        return;
    }
    void* reply = g_dbus.send_block != nullptr ? g_dbus.send_block(conn, call, 2000, err) : nullptr;
    if (g_dbus.unref_msg != nullptr) {
        g_dbus.unref_msg(call);
    }
    if (reply == nullptr) {
        fail("status notifier watcher is not available");
        return;
    }
    if (g_dbus.unref_msg != nullptr) {
        g_dbus.unref_msg(reply);
    }
    *failure = {};
    ready->store(true);
    while (g_dbus.read_write(conn, 50)) {
    }
}

std::string start_service() {
    if (!load_dbus()) {
        return "libdbus is not available";
    }
    auto failure = std::make_shared<std::string>("status notifier did not start");
    auto ready = std::make_shared<std::atomic<bool>>(false);
    std::thread([failure, ready] { service_loop(failure, ready); }).detach();
    for (int i = 0; i < 100 && !ready->load(); ++i) {
        usleep(20000);
    }
    if (!ready->load()) {
        return "status notifier did not start";
    }
    return *failure;
}

class LinuxTray final : public TrayService {
public:
    plugin::OsResult item(std::string_view tip, plugin::Handler on_activate) override {
        plugin::OsResult result;
        const tray_detail::Item prepared = tray_detail::prepare(tip);
        result.title = prepared.tip;
        if (!prepared.ready) {
            result.body = prepared.reason;
            return result;
        }
        if (!on_activate) {
            result.body = "tray activate handler is empty";
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_activate = std::move(on_activate);
            g_tip = prepared.tip;
            g_id = prepared.id;
        }
        if (!g_service_started) {
            const std::string failure = start_service();
            if (!failure.empty()) {
                result.body = failure;
                return result;
            }
            g_service_started = true;
        }
        result.ok = true;
        return result;
    }

    plugin::OsResult badge(int count) override {
        plugin::OsResult result;
        const std::string visible = count > 0 ? "true" : "false";
        const std::string cmd =
            "gdbus call --session --dest com.canonical.Unity.LauncherEntry --object-path "
            "/com/canonical/unity/launcherentry/zui --method com.canonical.Unity.LauncherEntry.Update "
            "application://zui.desktop \"{'count': <int64 " +
            std::to_string(count) + ">, 'count-visible': <" + visible + ">}\"";
        result.ok = gdbus(cmd);
        if (result.ok) {
            g_badge = count;
            g_badge_set = true;
            result.title = std::to_string(count);
        } else {
            result.body = "Unity launcher entry is not available";
        }
        return result;
    }

    int badge_count() const override { return g_badge_set ? g_badge : 0; }
};

#endif

}  // namespace

void register_linux_tray(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_tray(std::make_unique<LinuxTray>());
#else
    (void)registry;
#endif
}

}  // namespace zui
