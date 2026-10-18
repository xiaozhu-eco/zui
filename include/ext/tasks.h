#ifndef ZUI_EXT_TASKS_H
#define ZUI_EXT_TASKS_H

// Task pool a plugin supplies. Host::start_task keeps one instance from the factory.
// This header does not declare cursor, media-query, or native-window types.

#include <zui/host_types.h>
#include <zui/task_drain.h>

#include <memory>
#include <string_view>

namespace zui {

class TaskService {
public:
    virtual ~TaskService() = default;
    virtual TaskId start(TaskWork work, MainThreadWork on_done, std::string_view progress_id) = 0;
    virtual bool cancel(TaskId id) = 0;
    virtual TaskDrain drain() = 0;
    virtual void shutdown() = 0;
};

class TaskServiceFactory {
public:
    virtual ~TaskServiceFactory() = default;
    virtual std::unique_ptr<TaskService> create() = 0;
};

}  // namespace zui

#endif
