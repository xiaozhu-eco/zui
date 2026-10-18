// An extension header plus the application headers it includes.

#include <zui/app.h>

#include <ext/notify.h>

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

namespace {

class StubNotify final : public zui::NotifyService {
public:
    zui::plugin::OsResult notify(std::string_view title, std::string_view body) override {
        zui::plugin::OsResult result;
        result.ok = true;
        result.os_error = 0;
        result.title.assign(title.begin(), title.end());
        result.body.assign(body.begin(), body.end());
        return result;
    }
};

}  // namespace

TEST(AppEntryExt, NotifyStubReturnsTheSessionResult) {
    static_assert(std::is_same_v<decltype(std::declval<zui::HostServices>().notify("", "")),
                                 decltype(std::declval<StubNotify&>().notify("", ""))>);
    StubNotify stub;
    const zui::plugin::OsResult result = stub.notify("A", "B");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.os_error, 0u);
    EXPECT_EQ(result.title, "A");
    EXPECT_EQ(result.body, "B");

    zui::App app(0, nullptr);
    const zui::plugin::OsResult live = app.host().services().notify("A", "B");
    static_assert(std::is_same_v<decltype(live), decltype(result)>);
#if defined(_WIN32)
    EXPECT_TRUE(live.ok);
    EXPECT_EQ(live.title, "A");
    EXPECT_EQ(live.body, "B");
#else
    EXPECT_FALSE(live.ok);
    EXPECT_TRUE(live.title.empty());
#endif
}
