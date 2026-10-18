#ifndef ZUI_EXT_VIEW_H
#define ZUI_EXT_VIEW_H

// Document write channel for a framework-layer plugin.

#include <zui/event.h>
#include <zui/model.h>

#include <functional>
#include <memory>
#include <string_view>

namespace zui {

/// The write channel a framework-layer plugin receives.
class DocumentCommit {
public:
    virtual ~DocumentCommit() = default;
    virtual bool set(std::string_view id, std::string_view field, std::string_view value) = 0;
    virtual bool add_class(std::string_view id, std::string_view name) = 0;
    virtual bool remove_class(std::string_view id, std::string_view name) = 0;
    virtual void bind(std::string_view name, std::function<void(Event&)> handler) = 0;
    virtual bool set_model(std::string_view id, const ListModel& model) = 0;
};

class ViewExtension {
public:
    virtual ~ViewExtension() = default;
    virtual const char* name() const = 0;
    virtual void mount(DocumentCommit& document) = 0;

    class Slot;
};

class ViewExtension::Slot {
public:
    virtual ~Slot() = default;
    virtual void add(std::unique_ptr<ViewExtension> extension) = 0;
};

}  // namespace zui

#endif
