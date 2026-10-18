#include "app/tasks.h"

#include "ext/slots.h"
#include "app/impl.h"

#include <utility>

namespace zui {

TaskRegistry::TaskRegistry() = default;

TaskService* TaskRegistry::runtime() {
    if (runtime_ == nullptr) {
        if (TaskServiceFactory* factory = ExtensionRegistry::instance().task_factory()) {
            runtime_ = factory->create();
        }
    }
    return runtime_.get();
}

TaskRegistry::~TaskRegistry() {
    shutdown();
    runtime_.reset();
}

void TaskRegistry::shutdown() {
    if (runtime_ != nullptr) {
        runtime_->shutdown();
    }
}

TaskId TaskRegistry::start(TaskWork work, MainThreadWork on_done, std::string_view progress_id) {
    TaskService* service = runtime();
    if (service == nullptr) {
        return 0;
    }
    return service->start(std::move(work), std::move(on_done), progress_id);
}

bool TaskRegistry::cancel(TaskId id) {
    if (runtime_ == nullptr) {
        return false;
    }
    return runtime_->cancel(id);
}

TaskRegistry::Drain TaskRegistry::drain() {
    Drain out;
    if (runtime_ == nullptr) {
        return out;
    }
    TaskDrain drained = runtime_->drain();
    out.progress.reserve(drained.progress.size());
    for (TaskProgress& write : drained.progress) {
        out.progress.push_back(ProgressWrite{std::move(write.id), std::move(write.value)});
    }
    out.done = std::move(drained.done);
    return out;
}

void App::Impl::apply_task_results(App& app) {
    TaskRegistry::Drain drained = tasks_.drain();
    for (const TaskRegistry::ProgressWrite& write : drained.progress) {
        (void)app.document().set(write.id, "value", write.value);
    }
    for (MainThreadWork& done : drained.done) {
        if (done) {
            done();
        }
    }
}

TaskId Host::start_task(TaskWork work, MainThreadWork on_done, std::string_view progress_id) {
    return impl_->session->tasks_.start(std::move(work), std::move(on_done), progress_id);
}

bool Host::cancel_task(TaskId id) {
    return impl_->session->tasks_.cancel(id);
}

}  // namespace zui
