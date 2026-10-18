#include "image/svg/parser/parser.h"
#include "image/svg/scene.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// Same 4×4 rect markup as ImageDriver.SvgDriverReportsBackendAndRefusesEncode.
const char* kRect =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
    "<rect width=\"4\" height=\"4\" fill=\"#ff0000\"/>"
    "</svg>";

}  // namespace

// Links zui_image only. Parser::parse and Scene::build must resolve without
// the image plugin.
TEST(SvgParserInternal, RectParsesAndBuildsOneShape) {
    zui::image::svg::Document document;
    std::string error;
    ASSERT_TRUE(zui::image::svg::Parser{}.parse(kRect, document, error)) << error;
    EXPECT_EQ(document.width, 4.f);
    EXPECT_EQ(document.height, 4.f);
    EXPECT_EQ(document.shape_count(), 1);

    zui::image::svg::Scene scene;
    ASSERT_TRUE(scene.build(document, {}, error)) << error;
    EXPECT_EQ(scene.shapes.size(), 1u);
}
