// The real renderer (Vulkan, or the GL fallback when Vulkan cannot be created) must
// keep unchanged quads on the device. A pure translation of one batch is a viewport
// move, not another upload. A scrolled page does not rebuild boxes. Headless
// RecordingRenderer does not count.
#include "app/frame_session.h"

#include <gtest/gtest.h>

#include <iostream>

TEST(AppInteract, GpuRetainsUnchangedAndScroll) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const zui::frame::Access::GpuRetainReport report = zui::frame::Access::gpu_retain_probe();
    if (!report.available) {
        std::cout << "GPU_UNAVAILABLE\n";
        GTEST_SKIP() << "GPU device was not created";
    }
    std::cout << "GPU_BACKEND " << report.backend << "\n"
              << "upload_first " << report.upload_first << "\n"
              << "upload_same " << report.upload_same << "\n"
              << "upload_scroll " << report.upload_scroll << "\n"
              << "page_ok " << (report.page_ok ? 1 : 0) << "\n"
              << "page_same_uploads " << report.page_same_uploads << "\n"
              << "page_scroll_uploads " << report.page_scroll_uploads << "\n"
              << "page_boxes " << report.page_boxes << "\n"
              << "page_laid " << report.page_laid << "\n"
              << "page_visited " << report.page_visited << "\n"
              << "scroll_y " << report.scroll_y << "\n"
              << "filter_effects " << report.filter_effects << "\n";

    EXPECT_GT(report.upload_first, 0u);
    EXPECT_EQ(report.upload_same, 0u) << "unchanged quads were uploaded again";
    EXPECT_EQ(report.upload_scroll, 0u) << "a pure translation uploaded the quads";
    EXPECT_GT(report.filter_effects, 0u) << "soft shadow / blend was not a GPU effect";
    EXPECT_TRUE(report.page_ok);
    EXPECT_EQ(report.page_same_uploads, 0u) << "unchanged page was uploaded again";
    EXPECT_EQ(report.page_scroll_uploads, 0u) << "scroll uploaded the page";
    EXPECT_EQ(report.page_boxes, 0u);
    EXPECT_EQ(report.page_laid, 0u);
    EXPECT_EQ(report.page_visited, 0u);
    EXPECT_GT(report.scroll_y, 1.f);
}
