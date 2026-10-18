#ifndef ZUI_TASK_DRAIN_H
#define ZUI_TASK_DRAIN_H

// Progress and main-thread completions a task plugin hands back.
// Application code does not include this header; app.h does not pull it in.
// Workers still see only TaskSignal. `done` is MainThreadWork, not TaskWork.

#include <zui/host_types.h>

#include <string>
#include <vector>

namespace zui {

struct TaskProgress {
    std::string id;
    std::string value;
};

struct TaskDrain {
    std::vector<TaskProgress> progress;
    std::vector<MainThreadWork> done;
};

}  // namespace zui

#endif
