// zmd source field and find field. The rest of the screen is driven in
// zmd_daily_test.cpp. See example/applications/zmd/README.md.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "../../example/applications/zmd/cpp/dom.h"
#include "../../example/applications/zmd/cpp/session.h"

namespace {

namespace fs = std::filesystem;

fs::path zmd_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zmd" / "index.html";
}

// "# \xe6\xa0\x87\xe9\xa2\x98\n\n\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n": one heading and one
// body line whose CJK code points are the words, so the status row reads "5 字 · 2 行".
const char* kSample = "# \xe6\xa0\x87\xe9\xa2\x98\n\n\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n";
const char* kBody = "\xe6\xad\xa3\xe6\x96\x87";  // 正文

struct ZmdRig {
    zui::test::HandlerRig rig;
    std::shared_ptr<zmd::Session> session;

    bool up() {
        session = std::make_shared<zmd::Session>(rig.app);
        if (!session->open_page(zmd_html().string())) {
            return false;
        }
        return rig.windowed() && rig.pump();
    }

    std::string get(std::string_view id, std::string_view field) {
        return rig.app.document().get(std::string(id), std::string(field));
    }
};

}  // namespace

TEST(AppInteract, ZmdSourceTypingUpdatesTheCountsRow) {
    ZmdRig r;
    ASSERT_TRUE(r.up());

    r.session->settings().set_view(zmd::Settings::View::Live);
    ASSERT_TRUE(r.rig.pump());

    ASSERT_TRUE(r.rig.type(zmd::dom::Id::source, kSample));

    EXPECT_EQ(r.get(zmd::dom::Id::source, "value"), kSample);
    EXPECT_EQ(r.get(zmd::dom::Id::counts, "text"), "5 \xe5\xad\x97 \xc2\xb7 2 \xe8\xa1\x8c");
    // A keystroke marks the document as being saved, which the sheet paints from the dot's
    // `data-state` attribute.
    EXPECT_EQ(r.get(zmd::dom::Id::save, "data-state"), "typing");
}

TEST(AppInteract, ZmdFindFieldCountsTheMatches) {
    ZmdRig r;
    ASSERT_TRUE(r.up());

    r.session->apply_source(kSample, false, true);
    r.session->find().open(false);
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(r.get(zmd::dom::Id::find, "visible"), "true");

    ASSERT_TRUE(r.rig.type(zmd::dom::Id::find_input, kBody));
    EXPECT_EQ(r.get(zmd::dom::Id::find_count, "text"), "1 / 1");

    r.session->find().close();
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(r.get(zmd::dom::Id::find, "visible"), "false");
    EXPECT_EQ(r.get(zmd::dom::Id::find_count, "text"), "");
}
