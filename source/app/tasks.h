#ifndef ZUI_APP_TASKS_H
#define ZUI_APP_TASKS_H

#include <zui/host_types.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace zui {

class App;
class TaskService;

/// Background tasks. Workers see only `TaskSignal`. Completions and scalar
/// progress are delivered on the main thread.
class TaskRegistry {
public:
    struct ProgressWrite {
        std::string id;
        std::string value;
    };

    struct Drain {
        std::vector<ProgressWrite> progress;
        std::vector<MainThreadWork> done;
    };

    TaskRegistry();
    ~TaskRegistry();

    TaskRegistry(const TaskRegistry&) = delete;
    TaskRegistry& operator=(const TaskRegistry&) = delete;
    TaskRegistry(TaskRegistry&&) = delete;
    TaskRegistry& operator=(TaskRegistry&&) = delete;

    TaskId start(TaskWork work, MainThreadWork on_done, std::string_view progress_id);
    /// Sets cancel, waits for the worker, and drops the completion.
    bool cancel(TaskId id);
    /// Main thread: progress strings and completions that were not cancelled.
    Drain drain();
    /// Cancel every task and join. Completions are not run.
    void shutdown();

private:
    TaskService* runtime();

    std::unique_ptr<TaskService> runtime_;
};

}  // namespace zui

#endif
