#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {

ZUI_PLUGIN_API void zui_plugin_view_commit_register(ExtensionRegistry& registry);

namespace {

/// Framework-layer sample. It is not Vue or React. It only writes through DocumentCommit.
class CommitView final : public ViewExtension {
public:
    const char* name() const override { return "view-commit"; }

    void mount(DocumentCommit& document) override {
        (void)document.set("plug", "text", "from-view");
        (void)document.add_class("plug", "from-view");
    }
};

}  // namespace

void zui_plugin_view_commit_register(ExtensionRegistry& registry) {
    registry.add_view(std::make_unique<CommitView>());
}

}  // namespace zui
