#include "backends/permission_policy.h"

#include <gtest/gtest.h>

#include <iostream>
#include <string_view>

namespace {

zui::backends::PermissionAsk ask(std::string_view name, bool declared, int api,
                                 zui::PermissionState current) {
    return zui::backends::permission_ask(name, declared, api, current);
}

zui::backends::IosPermissionChoice ios(std::string_view name, bool declared,
                                       zui::backends::IosAuth status, bool hardware, bool evaluated,
                                       bool requesting) {
    zui::backends::IosPermissionFacts facts;
    facts.declared = declared;
    facts.status = status;
    facts.hardware = hardware;
    facts.evaluated = evaluated;
    return zui::backends::ios_permission_choice(name, facts, requesting);
}

const char* state_name(zui::PermissionState state) {
    switch (state) {
    case zui::PermissionState::Unsupported:
        return "Unsupported";
    case zui::PermissionState::NotRequested:
        return "NotRequested";
    case zui::PermissionState::Granted:
        return "Granted";
    case zui::PermissionState::Denied:
        return "Denied";
    case zui::PermissionState::Blocked:
        return "Blocked";
    }
    return "?";
}

void show(const char* label, const zui::backends::IosPermissionChoice& choice) {
    std::cout << label << " state=" << state_name(choice.state) << " prompt=" << choice.prompt
              << " evaluate=" << choice.evaluate << "\n";
}

}  // namespace

TEST(PermissionPolicy, AskRoutes) {
    using zui::PermissionState;
    using zui::backends::PermissionAskKind;

    const auto not_yet = ask("camera", true, 33, PermissionState::NotRequested);
    EXPECT_EQ(not_yet.kind, PermissionAskKind::Dialog);
    const auto denied = ask("camera", true, 33, PermissionState::Denied);
    EXPECT_EQ(denied.kind, PermissionAskKind::Dialog);

    const auto blocked = ask("camera", true, 33, PermissionState::Blocked);
    EXPECT_EQ(blocked.kind, PermissionAskKind::Settings);

    const auto storage_old = ask("manage-storage", true, 29, PermissionState::NotRequested);
    EXPECT_EQ(storage_old.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(storage_old.state, PermissionState::Unsupported);
    const auto storage_new = ask("manage-storage", true, 30, PermissionState::NotRequested);
    EXPECT_EQ(storage_new.kind, PermissionAskKind::Settings);

    const auto install_old = ask("install-packages", true, 25, PermissionState::NotRequested);
    EXPECT_EQ(install_old.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(install_old.state, PermissionState::Granted);
    const auto alarm_old = ask("exact-alarm", true, 30, PermissionState::NotRequested);
    EXPECT_EQ(alarm_old.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(alarm_old.state, PermissionState::Granted);
    const auto full_old = ask("full-screen-intent", true, 33, PermissionState::NotRequested);
    EXPECT_EQ(full_old.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(full_old.state, PermissionState::Granted);

    const auto overlay = ask("overlay", true, 33, PermissionState::NotRequested);
    EXPECT_EQ(overlay.kind, PermissionAskKind::Settings);
    const auto network = ask("network", true, 33, PermissionState::NotRequested);
    EXPECT_EQ(network.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(network.state, PermissionState::Granted);

    const auto missing = ask("camera", false, 33, PermissionState::NotRequested);
    EXPECT_EQ(missing.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(missing.state, PermissionState::Unsupported);

    const auto granted = ask("camera", true, 33, PermissionState::Granted);
    EXPECT_EQ(granted.kind, PermissionAskKind::Immediate);
    EXPECT_EQ(granted.state, PermissionState::Granted);
}

TEST(PermissionPolicy, IosChoice) {
    using zui::PermissionState;
    using zui::backends::IosAuth;

    const auto notes_missing = ios("notifications", false, IosAuth::NotDetermined, true, false, true);
    EXPECT_EQ(notes_missing.state, PermissionState::Unsupported);
    EXPECT_FALSE(notes_missing.prompt);

    const auto notes_ask = ios("notifications", true, IosAuth::NotDetermined, true, false, true);
    EXPECT_EQ(notes_ask.state, PermissionState::NotRequested);
    EXPECT_TRUE(notes_ask.prompt);
    const auto notes_yes = ios("notifications", true, IosAuth::Authorized, true, false, false);
    EXPECT_EQ(notes_yes.state, PermissionState::Granted);
    EXPECT_FALSE(notes_yes.prompt);
    const auto notes_no = ios("notifications", true, IosAuth::Denied, true, false, false);
    EXPECT_EQ(notes_no.state, PermissionState::Blocked);

    const auto bio_none = ios("biometric", true, IosAuth::NotDetermined, false, false, true);
    EXPECT_EQ(bio_none.state, PermissionState::Unsupported);
    EXPECT_FALSE(bio_none.evaluate);
    EXPECT_NE(bio_none.state, PermissionState::Granted);
    const auto bio_hw = ios("biometric", true, IosAuth::NotDetermined, true, false, true);
    EXPECT_NE(bio_hw.state, PermissionState::Granted);
    EXPECT_TRUE(bio_hw.evaluate);
    const auto bio_done = ios("biometric", true, IosAuth::NotDetermined, true, true, false);
    show("biometric success", bio_done);
    EXPECT_EQ(bio_done.state, PermissionState::Granted);
    EXPECT_FALSE(bio_done.evaluate);
    const auto bio_denied = ios("biometric", true, IosAuth::Denied, true, false, false);
    show("biometric denied", bio_denied);
    EXPECT_EQ(bio_denied.state, PermissionState::Denied);
    EXPECT_NE(bio_denied.state, PermissionState::NotRequested);
    EXPECT_FALSE(bio_denied.evaluate);
    const auto bio_blocked = ios("biometric", true, IosAuth::Restricted, true, false, true);
    show("biometric blocked", bio_blocked);
    EXPECT_EQ(bio_blocked.state, PermissionState::Blocked);
    EXPECT_FALSE(bio_blocked.evaluate);
    const auto bio_lockout = ios("biometric", true, IosAuth::Restricted, false, false, true);
    show("biometric lockout no hardware", bio_lockout);
    EXPECT_EQ(bio_lockout.state, PermissionState::Blocked);
    EXPECT_FALSE(bio_lockout.evaluate);

    const auto nfc_missing = ios("nfc", false, IosAuth::NotDetermined, true, false, true);
    EXPECT_EQ(nfc_missing.state, PermissionState::Unsupported);
    EXPECT_FALSE(nfc_missing.prompt);
    const auto nfc_ask = ios("nfc", true, IosAuth::NotDetermined, true, false, true);
    EXPECT_TRUE(nfc_ask.prompt);
    EXPECT_NE(nfc_ask.state, PermissionState::Granted);
    const auto nfc_done = ios("nfc", true, IosAuth::Denied, true, false, false);
    EXPECT_NE(nfc_done.state, PermissionState::NotRequested);
    EXPECT_NE(nfc_done.state, PermissionState::Granted);
    const auto wifi_done = ios("nearby-wifi", true, IosAuth::Denied, true, false, true);
    EXPECT_NE(wifi_done.state, PermissionState::NotRequested);
    EXPECT_NE(wifi_done.state, PermissionState::Granted);
    EXPECT_TRUE(wifi_done.prompt);
    const auto wifi_missing = ios("nearby-wifi", false, IosAuth::NotDetermined, true, false, true);
    EXPECT_EQ(wifi_missing.state, PermissionState::Unsupported);
    EXPECT_FALSE(wifi_missing.prompt);
    const auto wifi_ask = ios("nearby-wifi", true, IosAuth::NotDetermined, true, false, true);
    EXPECT_TRUE(wifi_ask.prompt);
    EXPECT_NE(wifi_ask.state, PermissionState::Granted);

    const auto camera_denied = ios("camera", true, IosAuth::Denied, true, false, false);
    EXPECT_EQ(camera_denied.state, PermissionState::Blocked);
    const auto camera_restricted = ios("camera", true, IosAuth::Restricted, true, false, false);
    EXPECT_EQ(camera_restricted.state, PermissionState::Blocked);

    const auto cal_read = ios("calendar-read", true, IosAuth::WriteOnly, true, false, true);
    show("calendar-read write-only", cal_read);
    EXPECT_NE(cal_read.state, PermissionState::Granted);
    EXPECT_TRUE(cal_read.prompt);
    const auto cal_write = ios("calendar-write", true, IosAuth::WriteOnly, true, false, false);
    show("calendar-write write-only", cal_write);
    EXPECT_EQ(cal_write.state, PermissionState::Granted);

    const std::string_view background_only = "location-background";
    EXPECT_TRUE(zui::backends::permission_author_declared(background_only, "location-background"));
    EXPECT_FALSE(zui::backends::permission_author_declared(background_only, "location"));
    EXPECT_FALSE(zui::backends::permission_author_declared(background_only, "coarse-location"));
    const auto when_in_use =
        ios("location", zui::backends::permission_author_declared(background_only, "location"),
            IosAuth::Authorized, true, false, true);
    show("author list location-background / location", when_in_use);
    EXPECT_EQ(when_in_use.state, PermissionState::Unsupported);
    EXPECT_FALSE(when_in_use.prompt);
    const auto coarse =
        ios("coarse-location",
            zui::backends::permission_author_declared(background_only, "coarse-location"),
            IosAuth::Authorized, true, false, true);
    show("author list location-background / coarse-location", coarse);
    EXPECT_EQ(coarse.state, PermissionState::Unsupported);
    EXPECT_FALSE(coarse.prompt);
    const std::string_view images_only = "media-images";
    EXPECT_TRUE(zui::backends::permission_author_declared(images_only, "media-images"));
    EXPECT_FALSE(zui::backends::permission_author_declared(images_only, "storage-write"));
    const auto add_only =
        ios("storage-write", zui::backends::permission_author_declared(images_only, "storage-write"),
            IosAuth::Authorized, true, false, true);
    show("author list media-images / storage-write", add_only);
    EXPECT_EQ(add_only.state, PermissionState::Unsupported);
    EXPECT_FALSE(add_only.prompt);

    const auto network = ios("network", true, IosAuth::Authorized, true, false, true);
    EXPECT_EQ(network.state, PermissionState::Unsupported);
    EXPECT_FALSE(network.prompt);
    const auto storage = ios("storage", true, IosAuth::Authorized, true, false, true);
    EXPECT_EQ(storage.state, PermissionState::Unsupported);
}
