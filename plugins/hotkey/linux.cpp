#include "ext/slots.h"
#include "plugins/hotkey/chord.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace zui {
namespace {

#if defined(__linux__) && !defined(__ANDROID__)

struct Binding {
    hotkey_chord::Chord chord;
    plugin::Handler handler;
    unsigned keycode = 0;
};

struct Job {
    hotkey_chord::Chord chord;
    plugin::Handler handler;
    bool finished = false;
    bool ok = false;
    unsigned os_error = 0;
    std::string reason;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::vector<Job*> g_jobs;
std::vector<Binding> g_bindings;
std::atomic<bool> g_started{false};

unsigned event_u32(const unsigned char* bytes, bool keycode) {
    const bool wide = sizeof(void*) == 8;
    const std::size_t offset = keycode ? (wide ? 84 : 52) : (wide ? 80 : 48);
    return *reinterpret_cast<const unsigned*>(bytes + offset);
}

int x_error_flag = 0;

int on_x_error(void*, void*) {
    x_error_flag = 1;
    return 0;
}

void grab_loop() {
    using OpenFn = void* (*)(const char*);
    using InitFn = int (*)();
    using SymFn = unsigned (*)(void*, unsigned long);
    using GrabFn = int (*)(void*, int, unsigned, unsigned long, int, int, int);
    using RootFn = unsigned long (*)(void*);
    using PendingFn = int (*)(void*);
    using NextFn = int (*)(void*, void*);
    using FlushFn = int (*)(void*);
    using SyncFn = int (*)(void*, int);
    using HandlerFn = void* (*)(int (*)(void*, void*));
    void* lib = dlopen("libX11.so.6", RTLD_LAZY);
    auto open_display = lib != nullptr ? reinterpret_cast<OpenFn>(dlsym(lib, "XOpenDisplay")) : nullptr;
    auto init_threads = lib != nullptr ? reinterpret_cast<InitFn>(dlsym(lib, "XInitThreads")) : nullptr;
    auto keycode_of = lib != nullptr ? reinterpret_cast<SymFn>(dlsym(lib, "XKeysymToKeycode")) : nullptr;
    auto grab = lib != nullptr ? reinterpret_cast<GrabFn>(dlsym(lib, "XGrabKey")) : nullptr;
    auto root_of = lib != nullptr ? reinterpret_cast<RootFn>(dlsym(lib, "XDefaultRootWindow")) : nullptr;
    auto pending = lib != nullptr ? reinterpret_cast<PendingFn>(dlsym(lib, "XPending")) : nullptr;
    auto next = lib != nullptr ? reinterpret_cast<NextFn>(dlsym(lib, "XNextEvent")) : nullptr;
    auto flush = lib != nullptr ? reinterpret_cast<FlushFn>(dlsym(lib, "XFlush")) : nullptr;
    auto sync = lib != nullptr ? reinterpret_cast<SyncFn>(dlsym(lib, "XSync")) : nullptr;
    auto set_error = lib != nullptr ? reinterpret_cast<HandlerFn>(dlsym(lib, "XSetErrorHandler")) : nullptr;
    if (init_threads != nullptr) {
        init_threads();
    }
    void* display = open_display != nullptr ? open_display(nullptr) : nullptr;
    if (display == nullptr || keycode_of == nullptr || grab == nullptr || root_of == nullptr || pending == nullptr ||
        next == nullptr) {
        const std::string reason = lib == nullptr ? "libX11 is not available" : "X11 is not available";
        while (true) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                for (Job* job : g_jobs) {
                    job->reason = reason;
                    job->finished = true;
                }
                g_jobs.clear();
                g_cv.notify_all();
            }
            usleep(20000);
        }
    }
    const unsigned long root = root_of(display);
    if (set_error != nullptr) {
        set_error(on_x_error);
    }
    unsigned char event[256];
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (Job* job : g_jobs) {
                const unsigned code = keycode_of(display, job->chord.linux_keysym);
                if (code == 0) {
                    job->reason = "X11 has no keycode for " + job->chord.label;
                    job->finished = true;
                    continue;
                }
                bool replaced = false;
                for (Binding& binding : g_bindings) {
                    if (hotkey_chord::same(binding.chord, job->chord)) {
                        binding.handler = std::move(job->handler);
                        binding.keycode = code;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    x_error_flag = 0;
                    const unsigned locks[] = {0u, 2u, 16u, 18u};
                    for (unsigned lock_bits : locks) {
                        grab(display, static_cast<int>(code), job->chord.linux_mods | lock_bits, root, 0, 1, 1);
                    }
                    if (sync != nullptr) {
                        sync(display, 0);
                    }
                    if (x_error_flag != 0) {
                        job->os_error = static_cast<unsigned>(x_error_flag);
                        job->reason = "XGrabKey failed for " + job->chord.label;
                        job->finished = true;
                        continue;
                    }
                    g_bindings.push_back(Binding{job->chord, std::move(job->handler), code});
                }
                job->ok = true;
                job->finished = true;
            }
            g_jobs.clear();
            g_cv.notify_all();
        }
        if (flush != nullptr) {
            flush(display);
        }
        while (pending(display) > 0) {
            next(display, event);
            if (*reinterpret_cast<int*>(event) != 2) {
                continue;
            }
            const unsigned code = event_u32(event, true);
            const unsigned state = event_u32(event, false) & ~(2u | 16u);
            plugin::Handler handler;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                for (const Binding& binding : g_bindings) {
                    if (binding.keycode == code && binding.chord.linux_mods == state) {
                        handler = binding.handler;
                        break;
                    }
                }
            }
            if (handler) {
                handler();
            }
        }
        usleep(20000);
    }
}

void ensure_thread() {
    if (!g_started.exchange(true)) {
        std::thread(grab_loop).detach();
    }
}

class LinuxHotkey final : public HotkeyService {
public:
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) override {
        plugin::OsResult result;
        const hotkey_chord::Chord chord = hotkey_chord::encode(key);
        result.title = chord.label;
        if (!chord.linux) {
            result.body = chord.reason.empty() ? "Linux has no keysym for this hotkey" : chord.reason;
            return result;
        }
        if (!on_fire) {
            result.body = "hotkey handler is empty";
            return result;
        }
        Job job;
        job.chord = chord;
        job.handler = std::move(on_fire);
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_jobs.push_back(&job);
        }
        ensure_thread();
        std::unique_lock<std::mutex> lock(g_mu);
        g_cv.wait_for(lock, std::chrono::seconds(2), [&] { return job.finished; });
        if (!job.finished) {
            g_jobs.erase(std::remove(g_jobs.begin(), g_jobs.end(), &job), g_jobs.end());
            result.body = "X11 did not answer for " + chord.label;
            return result;
        }
        result.ok = job.ok;
        result.os_error = job.os_error;
        result.body = job.reason;
        return result;
    }
};

#endif

}  // namespace

void register_linux_hotkey(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_hotkey(std::make_unique<LinuxHotkey>());
#else
    (void)registry;
#endif
}

}  // namespace zui
