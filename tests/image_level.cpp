#include <gtest/gtest.h>

#include "backends/image_textures.h"

// A toolbar bitmap is point-sampled up to the draw size. That size still has to
// fit the backend's texture edge. image_level(64, 64, 3000, 3000, 2048) used to
// return the draw size and drop the max_edge reduction.
TEST(ImageLevel, PointSampleDrawStaysInsideMaxEdge) {
    const zui::backends::ImageLevel huge =
        zui::backends::image_level(64, 64, 3000.f, 3000.f, 2048);
    EXPECT_TRUE(huge.point);
    EXPECT_EQ(huge.width, 2048);
    EXPECT_EQ(huge.height, 2048);

    const zui::backends::ImageLevel wide =
        zui::backends::image_level(64, 64, 3000.f, 100.f, 2048);
    EXPECT_TRUE(wide.point);
    EXPECT_EQ(wide.width, 2048);
    EXPECT_EQ(wide.height, 100);

    const zui::backends::ImageLevel tight =
        zui::backends::image_level(64, 64, 3000.f, 3000.f, 32);
    EXPECT_TRUE(tight.point);
    EXPECT_LE(tight.width, 32);
    EXPECT_LE(tight.height, 32);

    const zui::backends::ImageLevel icon =
        zui::backends::image_level(32, 32, 48.f, 48.f, 2048);
    EXPECT_TRUE(icon.point);
    EXPECT_EQ(icon.width, 48);
    EXPECT_EQ(icon.height, 48);
}
