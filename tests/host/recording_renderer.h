#ifndef ZUI_TESTS_HOST_RECORDING_RENDERER_H
#define ZUI_TESTS_HOST_RECORDING_RENDERER_H

#include "backends/renderer.h"

#include <cstddef>
#include <vector>

namespace zui::test {

class RecordingRenderer final : public backends::IRenderer {
public:
    std::vector<std::size_t> submit_sizes;
    /// The items of the last submitted list: a gate that has to look at what the painter produced
    /// (a radius, a border style) reads this instead of re-deriving it from the box.
    std::vector<paint::DisplayItem> last_items;
    int present_count = 0;

    bool create(backends::IWindow&) override { return true; }
    void destroy() override {}
    void resize(int, int) override {}
    void begin_frame() override {}
    void submit(const paint::DisplayList& list) override {
        submit_sizes.push_back(list.items().size());
        last_items = list.items();
    }
    bool present() override {
        ++present_count;
        return true;
    }
    const char* name() const override { return "record"; }
};

}  // namespace zui::test

#endif
