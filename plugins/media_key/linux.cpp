#include "ext/slots.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
    unsigned long sym = 0;
    unsigned code = 0;
    plugin::Handler handler;
};

struct Job {
    unsigned long sym = 0;
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
int g_x_error = 0;

int on_x_error(void*, void*) {
    g_x_error = 1;
    return 0;
}

unsigned event_keycode(const unsigned char* bytes) {
    const std::size_t offset = sizeof(void*) == 8 ? 84 : 52;
    return *reinterpret_cast<const unsigned*>(bytes + offset);
}

void grab_one(void* display, void* grab_fn, unsigned long root, unsigned code) {
    using GrabFn = int (*)(void*, int, unsigned, unsigned long, int, int, int);
    auto grab = reinterpret_cast<GrabFn>(grab_fn);
    const unsigned locks[] = {0u, 2u, 16u, 18u};
    for (unsigned lock : locks) {
        grab(display, static_cast<int>(code), lock, root, 0, 1, 1);
    }
}

// Grab and the event read share this thread's Display. An XGrabKey is delivered
// only to the client that grabbed, so a second XOpenDisplay never sees the key.
void media_loop() {
    using OpenFn = void* (*)(const char*);
    using InitFn = int (*)();
    using SymFn = unsigned (*)(void*, unsigned long);
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
    auto grab = lib != nullptr ? dlsym(lib, "XGrabKey") : nullptr;
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
    const bool ready = display != nullptr && keycode_of != nullptr && grab != nullptr && root_of != nullptr &&
                       pending != nullptr && next != nullptr;
    const unsigned long root = ready ? root_of(display) : 0;
    if (ready && set_error != nullptr) {
        set_error(on_x_error);
    }
    const std::string unavailable = lib == nullptr ? "libX11.so.6 is not available"
                                    : display == nullptr ? "X11 display is not available"
                                                         : "X11 is not available";
    unsigned char event[256];
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (Job* job : g_jobs) {
                if (!ready) {
                    job->reason = unavailable;
                    job->finished = true;
                    continue;
                }
                const unsigned code = keycode_of(display, job->sym);
                if (code == 0) {
                    job->reason = "X11 has no keycode for this media key";
                    job->finished = true;
                    continue;
                }
                bool replaced = false;
                for (Binding& binding : g_bindings) {
                    if (binding.sym == job->sym) {
                        binding.handler = std::move(job->handler);
                        binding.code = code;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    g_x_error = 0;
                    grab_one(display, grab, root, code);
                    if (sync != nullptr) {
                        sync(display, 0);
                    }
                    if (g_x_error != 0) {
                        job->os_error = static_cast<unsigned>(g_x_error);
                        job->reason = "XGrabKey failed";
                        job->finished = true;
                        continue;
                    }
                    g_bindings.push_back(Binding{job->sym, code, std::move(job->handler)});
                }
                job->ok = true;
                job->finished = true;
            }
            g_jobs.clear();
            g_cv.notify_all();
        }
        if (!ready) {
            usleep(20000);
            continue;
        }
        if (flush != nullptr) {
            flush(display);
        }
        while (pending(display) > 0) {
            next(display, event);
            if (*reinterpret_cast<int*>(event) != 2) {
                continue;
            }
            const unsigned code = event_keycode(event);
            plugin::Handler handler;
            for (const Binding& binding : g_bindings) {
                if (binding.code == code) {
                    handler = binding.handler;
                    break;
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
        std::thread(media_loop).detach();
    }
}

class LinuxMedia final : public MediaKeyService {
public:
    plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) override {
        const std::string_view which = plugin::media_key_name(action);
        plugin::OsResult result;
        const unsigned long sym = which == "play" ? 0x1008FF14 : which == "next" ? 0x1008FF17 : which == "previous"
                                                                                                ? 0x1008FF16
                                                                                                : 0;
        result.title.assign(which.begin(), which.end());
        if (sym == 0) {
            result.title.clear();
            result.body = "media key is not play, next, or previous";
            return result;
        }
        if (!on_fire) {
            result.body = "media-key handler is empty";
            return result;
        }
        Job job;
        job.sym = sym;
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
            result.body = "X11 did not answer for " + result.title;
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

void register_linux_media_key(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_media_key(std::make_unique<LinuxMedia>());
#else
    (void)registry;
#endif
}

}  // namespace zui
