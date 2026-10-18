#include "ext/plugin_export.h"
#include "ext/slots.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zui {
namespace {

std::string format_progress(float fraction) {
    if (!(fraction >= 0.f)) {
        fraction = 0.f;
    }
    if (fraction > 1.f) {
        fraction = 1.f;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(fraction));
    return buf;
}

struct Slot {
    std::atomic<bool> cancel{false};
    std::atomic<float> progress{-1.f};
    std::string progress_id;
    std::string published;
    MainThreadWork on_done;
    std::thread thread;
    bool worker_done = false;
    bool drop_completion = false;
    bool completion_queued = false;
    bool finished_on_main = false;
};

class Pool final : public TaskService {
public:
    ~Pool() override { shutdown(); }

    TaskId start(TaskWork work, MainThreadWork on_done, std::string_view progress_id) override {
        if (!work) {
            return 0;
        }
        auto slot = std::make_shared<Slot>();
        slot->progress_id.assign(progress_id.begin(), progress_id.end());
        slot->on_done = std::move(on_done);
        TaskId id = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            id = next_++;
            if (next_ == 0) {
                next_ = 1;
            }
            slots_.emplace(id, slot);
        }
        slot->thread = std::thread([this, slot, work = std::move(work)]() mutable {
            const TaskSignal signal(&slot->cancel, &slot->progress);
            work(signal);
            std::lock_guard<std::mutex> lock(mu_);
            slot->worker_done = true;
            if (!slot->cancel.load(std::memory_order_acquire) && !slot->drop_completion) {
                slot->completion_queued = true;
            }
        });
        return id;
    }

    bool cancel(TaskId id) override {
        if (id == 0) {
            return false;
        }
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = slots_.find(id);
            if (it == slots_.end()) {
                return false;
            }
            Slot& slot = *it->second;
            if (slot.finished_on_main) {
                return false;
            }
            slot.cancel.store(true, std::memory_order_release);
            slot.drop_completion = true;
            slot.completion_queued = false;
            slot.on_done = {};
            if (slot.thread.joinable()) {
                worker = std::move(slot.thread);
            }
        }
        if (worker.joinable()) {
            worker.join();
        }
        return true;
    }

    TaskDrain drain() override {
        TaskDrain out;
        std::vector<std::thread> joining;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto& entry : slots_) {
                Slot& slot = *entry.second;
                const float fraction = slot.progress.load(std::memory_order_acquire);
                if (fraction >= 0.f && !slot.progress_id.empty()) {
                    std::string text = format_progress(fraction);
                    if (text != slot.published) {
                        slot.published = text;
                        out.progress.push_back(TaskProgress{slot.progress_id, std::move(text)});
                    }
                }
                if (slot.completion_queued && !slot.drop_completion) {
                    slot.completion_queued = false;
                    slot.finished_on_main = true;
                    if (slot.on_done) {
                        out.done.push_back(std::move(slot.on_done));
                    }
                }
                if (slot.worker_done && slot.thread.joinable()) {
                    joining.push_back(std::move(slot.thread));
                }
            }
        }
        for (std::thread& thread : joining) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto it = slots_.begin(); it != slots_.end();) {
                const Slot& slot = *it->second;
                const bool settled = slot.worker_done && !slot.thread.joinable() &&
                                     (slot.finished_on_main || slot.drop_completion);
                if (settled) {
                    it = slots_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        return out;
    }

    void shutdown() override {
        std::vector<std::thread> threads;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto& entry : slots_) {
                Slot& slot = *entry.second;
                slot.cancel.store(true, std::memory_order_release);
                slot.drop_completion = true;
                slot.completion_queued = false;
                slot.on_done = {};
                if (slot.thread.joinable()) {
                    threads.push_back(std::move(slot.thread));
                }
            }
            slots_.clear();
        }
        for (std::thread& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

private:
    std::mutex mu_;
    TaskId next_ = 1;
    std::map<TaskId, std::shared_ptr<Slot>> slots_;
};

class PoolFactory final : public TaskServiceFactory {
public:
    std::unique_ptr<TaskService> create() override { return std::make_unique<Pool>(); }
};

}  // namespace

void zui_plugin_tasks_register(ExtensionRegistry& registry) {
    registry.add_task_factory(std::make_unique<PoolFactory>());
}

}  // namespace zui
