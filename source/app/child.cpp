#include <zui/app.h>

#include "app/impl.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/engine.h"
#include "layout/viewport.h"

#include <algorithm>
#include <string>
#include <utility>

namespace zui {

App::Impl::OwnedWindow* App::Impl::modal_child() {
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if ((*it)->modal && (*it)->interaction != nullptr) {
            return it->get();
        }
    }
    return nullptr;
}

void App::Impl::dispatch_to(OwnedWindow* page, Event& event) {
    const std::string name(event.name());
    if (page != nullptr) {
        const auto it = page->handlers.find(name);
        if (it != page->handlers.end()) {
            it->second(event);
            return;
        }
    }
    const auto it = handlers.find(name);
    if (it != handlers.end()) {
        it->second(event);
    }
}

void App::Impl::drop_document_page(OwnedWindow* page) {
    if (page == nullptr) {
        return;
    }
    timer_q_.drop_page(page);
    deferred.erase(std::remove_if(deferred.begin(), deferred.end(),
                                  [page](const DeferredCall& call) { return call.page == page; }),
                   deferred.end());
    watches.erase(std::remove_if(watches.begin(), watches.end(),
                                 [page](const Watch& watch) { return watch.page == page; }),
                  watches.end());
    page->handlers.clear();
}

ChildDocument Window::open_document(std::string_view html_path, bool modal) {
    App::Impl* session = impl_->session;
    auto owned = std::make_unique<App::Impl::OwnedWindow>();
    std::string error;
    if (!html::load_file(html_path, owned->document, error)) {
        return {};
    }
    if (!owned->styles.load_sheets(owned->document, error)) {
        return {};
    }
    owned->styles.cascade(owned->document, 800.f, 600.f);
    owned->layouter = std::make_unique<layout::LayoutEngine>(owned->metrics);
    owned->layouter->layout(owned->document, layout::Viewport{800.f, 600.f});
    owned->layouter->sync_world();
    App::Impl::OwnedWindow* self = owned.get();
    input::InteractionHost host;
    host.document = &owned->document;
    host.layout = owned->layouter.get();
    host.metrics = &owned->metrics;
    host.hovered = &owned->hovered;
    host.focused = &owned->focused;
    host.scroll_drag = &owned->scroll_drag;
    host.dispatch = [session, self](Event& event) { session->dispatch_to(self, event); };
    host.mark_geometry = [session] { session->geometry_dirty = true; };
    host.open_uri = [session](std::string_view uri) {
        session->ensure_platform();
        return session->platform != nullptr && session->platform->open_uri(uri);
    };
    owned->interaction = std::make_unique<input::Interaction>(std::move(host));
    owned->modal = modal;
    owned->id = session->next_child_id++;
    auto state = std::unique_ptr<Document::Impl>(new Document::Impl);
    state->session = session;
    state->page = self;
    owned->facade.reset(new Document(std::move(state)));
    session->children.push_back(std::move(owned));
    return ChildDocument(session->children.back()->facade.get());
}

bool Window::close_document(ChildDocument doc) {
    if (!doc) {
        return false;
    }
    App::Impl* session = impl_->session;
    const auto it = std::find_if(session->children.begin(), session->children.end(),
                                 [&doc](const std::unique_ptr<App::Impl::OwnedWindow>& owned) {
                                     return owned != nullptr && owned->facade.get() == doc.get();
                                 });
    if (it == session->children.end()) {
        return false;
    }
    App::Impl::OwnedWindow* child = it->get();
    html::Node* body = child->document.root != nullptr
                           ? html::find_tag(child->document.root.get(), "body")
                           : nullptr;
    if (body != nullptr && child->interaction != nullptr &&
        child->interaction->fire(body, "beforeunload")) {
        return false;
    }
    session->drop_document_page(child);
    session->children.erase(it);
    return true;
}

}  // namespace zui
