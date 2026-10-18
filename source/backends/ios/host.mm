#include "backends/mobile_device.h"
#include "backends/permission_policy.h"
#include "backends/posix_platform_util.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreBluetooth/CoreBluetooth.h>
#import <CoreLocation/CoreLocation.h>
#import <CoreMotion/CoreMotion.h>
#import <Contacts/Contacts.h>
#import <EventKit/EventKit.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <MediaPlayer/MediaPlayer.h>
#import <Photos/Photos.h>
#import <UserNotifications/UserNotifications.h>
#import <CoreNFC/CoreNFC.h>
#import <Network/Network.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using zui::PermissionState;

bool ios_declared(std::string_view name) {
    id raw = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ZuiDeclaredPermissions"];
    if (![raw isKindOfClass:[NSString class]]) {
        return false;
    }
    const char* utf = [(NSString*)raw UTF8String];
    if (utf == nullptr) {
        return false;
    }
    return zui::backends::permission_author_declared(utf, name);
}

using zui::backends::IosAuth;

IosAuth g_notification_auth = IosAuth::NotDetermined;
bool g_nfc_answered = false;
bool g_wifi_answered = false;
/// Biometric sheet that already finished. None until the system answers.
enum class BioAnswer { None, Success, Denied, Blocked };
BioAnswer g_bio_answer = BioAnswer::None;

IosAuth ios_read_auth(std::string_view name) {
    if (name == "camera") {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
        case AVAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case AVAuthorizationStatusDenied:
            return IosAuth::Denied;
        case AVAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case AVAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "microphone") {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case AVAuthorizationStatusDenied:
            return IosAuth::Denied;
        case AVAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case AVAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "location" || name == "coarse-location" || name == "location-background") {
        switch ([CLLocationManager authorizationStatus]) {
        case kCLAuthorizationStatusAuthorizedAlways:
            return IosAuth::Authorized;
        case kCLAuthorizationStatusAuthorizedWhenInUse:
            return IosAuth::WhenInUse;
        case kCLAuthorizationStatusDenied:
            return IosAuth::Denied;
        case kCLAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case kCLAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "media-images" || name == "media-video" || name == "media-visual-selected" ||
        name == "storage-write") {
        PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatus];
        if (@available(iOS 14, *)) {
            const PHAccessLevel level =
                name == "storage-write" ? PHAccessLevelAddOnly : PHAccessLevelReadWrite;
            status = [PHPhotoLibrary authorizationStatusForAccessLevel:level];
        }
        switch (status) {
        case PHAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case PHAuthorizationStatusLimited:
            return IosAuth::Limited;
        case PHAuthorizationStatusDenied:
            return IosAuth::Denied;
        case PHAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case PHAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "media-audio") {
        switch ([MPMediaLibrary authorizationStatus]) {
        case MPMediaLibraryAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case MPMediaLibraryAuthorizationStatusDenied:
            return IosAuth::Denied;
        case MPMediaLibraryAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case MPMediaLibraryAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "contacts-read" || name == "contacts-write") {
        switch ([CNContactStore authorizationStatusForEntityType:CNEntityTypeContacts]) {
        case CNAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case CNAuthorizationStatusDenied:
            return IosAuth::Denied;
        case CNAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case CNAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        default:
            return IosAuth::Authorized;
        }
    }
    if (name == "calendar-read" || name == "calendar-write") {
        const EKAuthorizationStatus status =
            [EKEventStore authorizationStatusForEntityType:EKEntityTypeEvent];
        if (@available(iOS 17.0, *)) {
            if (status == EKAuthorizationStatusWriteOnly) {
                return IosAuth::WriteOnly;
            }
            if (status == EKAuthorizationStatusFullAccess) {
                return IosAuth::Authorized;
            }
        }
        switch (status) {
        case EKAuthorizationStatusAuthorized:
            return IosAuth::Authorized;
        case EKAuthorizationStatusDenied:
            return IosAuth::Denied;
        case EKAuthorizationStatusRestricted:
            return IosAuth::Restricted;
        case EKAuthorizationStatusNotDetermined:
            return IosAuth::NotDetermined;
        default:
            return IosAuth::NotDetermined;
        }
    }
    if (name == "bluetooth-scan" || name == "bluetooth-connect" || name == "bluetooth-advertise") {
        if (@available(iOS 13.1, *)) {
            switch (CBManager.authorization) {
            case CBManagerAuthorizationAllowedAlways:
                return IosAuth::Authorized;
            case CBManagerAuthorizationDenied:
                return IosAuth::Denied;
            case CBManagerAuthorizationRestricted:
                return IosAuth::Restricted;
            case CBManagerAuthorizationNotDetermined:
                return IosAuth::NotDetermined;
            }
        }
    }
    if (name == "activity-recognition") {
        if (@available(iOS 11.0, *)) {
            switch ([CMMotionActivityManager authorizationStatus]) {
            case CMAuthorizationStatusAuthorized:
                return IosAuth::Authorized;
            case CMAuthorizationStatusDenied:
                return IosAuth::Denied;
            case CMAuthorizationStatusRestricted:
                return IosAuth::Restricted;
            case CMAuthorizationStatusNotDetermined:
                return IosAuth::NotDetermined;
            }
        }
    }
    if (name == "notifications") {
        return g_notification_auth;
    }
    if (name == "nfc" && g_nfc_answered) {
        return IosAuth::Denied;
    }
    if (name == "nearby-wifi" && g_wifi_answered) {
        return IosAuth::Denied;
    }
    return IosAuth::NotDetermined;
}

zui::backends::IosPermissionFacts ios_facts(std::string_view name) {
    zui::backends::IosPermissionFacts facts;
    facts.declared = ios_declared(name);
    facts.status = ios_read_auth(name);
    facts.hardware = true;
    if (name == "biometric") {
        LAContext* context = [[LAContext alloc] init];
        NSError* error = nil;
        facts.hardware = [context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                                              error:&error];
        // Lockout makes canEvaluatePolicy fail. The recorded answer is still a
        // finished result, not proof that the device has no biometrics.
        if (g_bio_answer != BioAnswer::None) {
            facts.hardware = true;
        }
        facts.evaluated = g_bio_answer == BioAnswer::Success;
        if (g_bio_answer == BioAnswer::Denied) {
            facts.status = IosAuth::Denied;
        } else if (g_bio_answer == BioAnswer::Blocked) {
            facts.status = IosAuth::Restricted;
        }
    }
    if (name == "activity-recognition") {
        facts.hardware = [CMMotionActivityManager isActivityAvailable];
    }
    return facts;
}

PermissionState ios_permission_state(std::string_view name) {
    return zui::backends::ios_permission_choice(name, ios_facts(name), false).state;
}

std::string g_perm_pending;
bool g_perm_settings = false;
CLLocationManager* g_location = nil;
CBCentralManager* g_bluetooth = nil;
id g_perm_active_observer = nil;

void ios_complete_named(const std::string& name) {
    const PermissionState state = ios_permission_state(name);
    zui::backends::mobile_complete_permission(name, state);
}

void ios_note_prompt_answer() {
    if (g_perm_pending == "nfc") {
        g_nfc_answered = true;
    }
    if (g_perm_pending == "nearby-wifi") {
        g_wifi_answered = true;
    }
}

void ios_finish_pending() {
    if (g_perm_pending.empty()) {
        return;
    }
    ios_note_prompt_answer();
    const std::string name = g_perm_pending;
    g_perm_pending.clear();
    g_perm_settings = false;
    ios_complete_named(name);
}

@interface ZuiPermissionRelay : NSObject <CLLocationManagerDelegate, CBCentralManagerDelegate,
                                         NFCNDEFReaderSessionDelegate>
@end

@implementation ZuiPermissionRelay
- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
    (void)manager;
    if ([CLLocationManager authorizationStatus] == kCLAuthorizationStatusNotDetermined) {
        return;
    }
    ios_finish_pending();
}
- (void)locationManager:(CLLocationManager*)manager
    didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    (void)manager;
    if (status == kCLAuthorizationStatusNotDetermined) {
        return;
    }
    ios_finish_pending();
}
- (void)centralManagerDidUpdateState:(CBCentralManager*)manager {
    (void)manager;
    if (@available(iOS 13.1, *)) {
        if (CBManager.authorization == CBManagerAuthorizationNotDetermined) {
            return;
        }
    }
    ios_finish_pending();
}
- (void)readerSession:(NFCNDEFReaderSession*)session didDetectNDEFs:(NSArray<NFCNDEFMessage*>*)messages {
    (void)session;
    (void)messages;
    // A successful scan is not an authorization-status query.
    ios_finish_pending();
}
- (void)readerSession:(NFCNDEFReaderSession*)session didInvalidateWithError:(NSError*)error {
    (void)session;
    (void)error;
    ios_finish_pending();
}
@end

ZuiPermissionRelay* ios_relay() {
    static ZuiPermissionRelay* relay = [[ZuiPermissionRelay alloc] init];
    return relay;
}

void ios_open_settings(const std::string& name) {
    g_perm_pending = name;
    g_perm_settings = true;
    if (g_perm_active_observer == nil) {
        g_perm_active_observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIApplicationDidBecomeActiveNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification*) {
                        if (g_perm_settings) {
                            ios_finish_pending();
                        }
                    }];
    }
    NSURL* url = [NSURL URLWithString:UIApplicationOpenSettingsURLString];
    [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
}

nw_connection_t g_local_prompt = nullptr;

void ios_start_local_network_prompt() {
    if (g_local_prompt != nullptr) {
        nw_connection_cancel(g_local_prompt);
        nw_release(g_local_prompt);
        g_local_prompt = nullptr;
    }
    // A private-network address, not loopback: loopback does not raise the local-network prompt.
    nw_endpoint_t endpoint = nw_endpoint_create_host("192.168.0.1", "9");
    nw_parameters_t params = nw_parameters_create();
    nw_connection_t connection = nw_connection_create(endpoint, params);
    nw_release(endpoint);
    nw_release(params);
    g_local_prompt = connection;
    nw_connection_set_queue(connection, dispatch_get_main_queue());
    nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t state, nw_error_t) {
        if (state == nw_connection_state_ready || state == nw_connection_state_failed ||
            state == nw_connection_state_cancelled) {
            ios_finish_pending();
        }
    });
    nw_connection_start(connection);
}

void ios_begin_permission(std::string_view name) {
    const std::string owned(name);
    const zui::backends::IosPermissionChoice choice =
        zui::backends::ios_permission_choice(name, ios_facts(name), true);
    if (choice.evaluate) {
        LAContext* context = [[LAContext alloc] init];
        [context evaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                localizedReason:@"Use fingerprint or face unlock"
                          reply:^(BOOL success, NSError* error) {
                              const NSInteger code = error == nil ? 0 : error.code;
                              const bool cancel = code == LAErrorUserCancel || code == LAErrorAppCancel ||
                                                  code == LAErrorSystemCancel;
                              dispatch_async(dispatch_get_main_queue(), ^{
                                  if (success) {
                                      g_bio_answer = BioAnswer::Success;
                                  } else {
                                      g_bio_answer = cancel ? BioAnswer::Denied : BioAnswer::Blocked;
                                  }
                                  const auto answered =
                                      zui::backends::ios_permission_choice(owned, ios_facts(owned), false);
                                  zui::backends::mobile_complete_permission(owned, answered.state);
                              });
                          }];
        return;
    }
    if (!choice.prompt) {
        if (choice.state == PermissionState::Blocked) {
            ios_open_settings(owned);
            return;
        }
        zui::backends::mobile_complete_permission(owned, choice.state);
        return;
    }
    if (!g_perm_pending.empty()) {
        zui::backends::mobile_complete_permission(owned, choice.state);
        return;
    }

    auto done = [owned](BOOL granted) {
        (void)granted;
        dispatch_async(dispatch_get_main_queue(), ^{
            ios_complete_named(owned);
        });
    };

    if (name == "camera") {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL ok) {
            done(ok);
        }];
        return;
    }
    if (name == "microphone") {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL ok) {
            done(ok);
        }];
        return;
    }
    if (name == "location" || name == "coarse-location" || name == "location-background") {
        if (g_location == nil) {
            g_location = [[CLLocationManager alloc] init];
            g_location.delegate = ios_relay();
        }
        g_perm_pending = owned;
        if (name == "location-background") {
            [g_location requestAlwaysAuthorization];
        } else {
            [g_location requestWhenInUseAuthorization];
        }
        return;
    }
    if (name == "media-images" || name == "media-video" || name == "media-visual-selected" ||
        name == "storage-write") {
        if (@available(iOS 14, *)) {
            const PHAccessLevel level =
                name == "storage-write" ? PHAccessLevelAddOnly : PHAccessLevelReadWrite;
            [PHPhotoLibrary requestAuthorizationForAccessLevel:level
                                                       handler:^(PHAuthorizationStatus) {
                                                           done(YES);
                                                       }];
        } else {
            [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus) {
                done(YES);
            }];
        }
        return;
    }
    if (name == "media-audio") {
        [MPMediaLibrary requestAuthorization:^(MPMediaLibraryAuthorizationStatus) {
            done(YES);
        }];
        return;
    }
    if (name == "contacts-read" || name == "contacts-write") {
        CNContactStore* store = [[CNContactStore alloc] init];
        [store requestAccessForEntityType:CNEntityTypeContacts
                        completionHandler:^(BOOL ok, NSError*) {
                            done(ok);
                        }];
        return;
    }
    if (name == "calendar-read" || name == "calendar-write") {
        EKEventStore* store = [[EKEventStore alloc] init];
        if (@available(iOS 17.0, *)) {
            void (^finish)(BOOL, NSError*) = ^(BOOL ok, NSError*) {
                done(ok);
            };
            if (name == "calendar-read") {
                [store requestFullAccessToEventsWithCompletion:finish];
            } else {
                [store requestWriteOnlyAccessToEventsWithCompletion:finish];
            }
        } else {
            [store requestAccessToEntityType:EKEntityTypeEvent
                                  completion:^(BOOL ok, NSError*) {
                                      done(ok);
                                  }];
        }
        return;
    }
    if (name == "bluetooth-scan" || name == "bluetooth-connect" || name == "bluetooth-advertise") {
        g_perm_pending = owned;
        g_bluetooth = [[CBCentralManager alloc] initWithDelegate:ios_relay()
                                                           queue:dispatch_get_main_queue()
                                                         options:@{
                                                             CBCentralManagerOptionShowPowerAlertKey :
                                                                 @NO
                                                         }];
        return;
    }
    if (name == "notifications") {
        UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                                 UNAuthorizationOptionSound |
                                                 UNAuthorizationOptionBadge)
                              completionHandler:^(BOOL ok, NSError*) {
                                  g_notification_auth = ok ? IosAuth::Authorized : IosAuth::Denied;
                                  done(ok);
                              }];
        return;
    }
    if (name == "activity-recognition") {
        CMMotionActivityManager* motion = [[CMMotionActivityManager alloc] init];
        NSDate* now_date = [NSDate date];
        [motion queryActivityStartingFromDate:now_date
                                       toDate:now_date
                                      toQueue:[NSOperationQueue mainQueue]
                                  withHandler:^(NSArray<CMMotionActivity*>*, NSError*) {
                                      done(YES);
                                  }];
        return;
    }
    if (name == "nfc") {
        if (@available(iOS 11.0, *)) {
            if ([NFCNDEFReaderSession readingAvailable]) {
                g_perm_pending = owned;
                NFCNDEFReaderSession* session =
                    [[NFCNDEFReaderSession alloc] initWithDelegate:ios_relay()
                                                             queue:dispatch_get_main_queue()
                                          invalidateAfterFirstRead:YES];
                [session beginSession];
                return;
            }
        }
        zui::backends::mobile_complete_permission(owned, PermissionState::Unsupported);
        return;
    }
    if (name == "nearby-wifi") {
        g_perm_pending = owned;
        ios_start_local_network_prompt();
        return;
    }
    zui::backends::mobile_complete_permission(owned, choice.state);
}

using zui::ClipboardStatus;
using zui::ClipboardText;
using zui::ColorSchemePreference;
using zui::FileDialogOptions;
using zui::HostMediaFacts;
using zui::PickPath;
using zui::PickPaths;
using zui::PickStatus;
using zui::PointerAccuracy;
using zui::backends::mobile_complete_pick_folder;
using zui::backends::mobile_complete_pick_open;
using zui::backends::mobile_complete_pick_save;
using zui::backends::posix_util::effective_app_id;
using zui::backends::posix_util::uri_scheme_allowed;

float g_keyboard_bottom = 0.f;
bool g_keyboard_installed = false;

std::vector<std::string> extensions_of(const FileDialogOptions& opt) {
    std::vector<std::string> exts;
    for (const auto& filter : opt.filters) {
        std::size_t start = 0;
        const std::string& patterns = filter.patterns;
        while (start <= patterns.size()) {
            const std::size_t semi = patterns.find(';', start);
            std::string part = patterns.substr(
                start, semi == std::string::npos ? std::string::npos : semi - start);
            if (!part.empty() && part != "*") {
                exts.push_back(std::move(part));
            }
            if (semi == std::string::npos) {
                break;
            }
            start = semi + 1;
        }
    }
    return exts;
}

NSArray<UTType*>* content_types(const std::vector<std::string>& exts, bool folders) {
    if (folders) {
        return @[ UTTypeFolder ];
    }
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const std::string& ext : exts) {
        NSString* name = [NSString stringWithUTF8String:ext.c_str()];
        UTType* type = [UTType typeWithFilenameExtension:name];
        if (type != nil) {
            [types addObject:type];
        }
    }
    if (types.count == 0) {
        [types addObject:UTTypeData];
    }
    return types;
}

UIViewController* top_controller(UIViewController* controller) {
    if (controller == nil) {
        return nil;
    }
    if (controller.presentedViewController != nil) {
        return top_controller(controller.presentedViewController);
    }
    return controller;
}

UIViewController* presenter_for(void* owner) {
    UIWindow* window = nil;
    if (owner != nullptr) {
        id object = (id)owner;
        if ([object isKindOfClass:[UIWindow class]]) {
            window = (UIWindow*)object;
        }
    }
    UIApplication* app = [UIApplication sharedApplication];
    if (window == nil && app != nil) {
        if (@available(iOS 13.0, *)) {
            for (UIScene* scene in app.connectedScenes) {
                if (![scene isKindOfClass:[UIWindowScene class]]) {
                    continue;
                }
                UIWindowScene* window_scene = (UIWindowScene*)scene;
                for (UIWindow* candidate in window_scene.windows) {
                    if (candidate.isKeyWindow) {
                        window = candidate;
                        break;
                    }
                }
                if (window == nil && window_scene.windows.count > 0) {
                    window = window_scene.windows.firstObject;
                }
                if (window != nil) {
                    break;
                }
            }
        }
        if (window == nil) {
            window = app.keyWindow;
        }
    }
    if (window == nil) {
        return nil;
    }
    return top_controller(window.rootViewController);
}

}  // namespace

@interface ZuiDocumentDelegate : NSObject <UIDocumentPickerDelegate> {
@public
    int kind;
    BOOL multi;
    BOOL silent;
    void* owner;
    UIDocumentPickerViewController* picker;
    std::string title;
    std::string defaultName;
    std::vector<std::string> extensions;
}
@end

static ZuiDocumentDelegate* g_delegate = nil;

using zui::PickPath;
using zui::PickPaths;
using zui::PickStatus;
using zui::backends::mobile_complete_pick_folder;
using zui::backends::mobile_complete_pick_open;
using zui::backends::mobile_complete_pick_save;

@implementation ZuiDocumentDelegate

- (void)finishOpen:(PickPaths)result {
    if (!silent) {
        mobile_complete_pick_open(std::move(result));
    }
    [self releaseSelf];
}

- (void)finishOne:(PickPath)result {
    if (!silent) {
        if (kind == 1) {
            mobile_complete_pick_save(std::move(result));
        } else {
            mobile_complete_pick_folder(std::move(result));
        }
    }
    [self releaseSelf];
}

- (void)releaseSelf {
    if (g_delegate == self) {
        g_delegate = nil;
    }
    if (picker != nil) {
        picker.delegate = nil;
        [picker release];
        picker = nil;
    }
    [self release];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    if (kind == 0) {
        PickPaths result;
        result.status = PickStatus::Cancelled;
        for (NSURL* url in urls) {
            const BOOL scoped = [url startAccessingSecurityScopedResource];
            (void)scoped;
            if (url.path != nil) {
                result.paths.emplace_back(url.path.UTF8String);
            }
        }
        if (!result.paths.empty()) {
            result.status = PickStatus::Ok;
        }
        [self finishOpen:std::move(result)];
        return;
    }
    PickPath result;
    result.status = PickStatus::Cancelled;
    NSURL* url = urls.firstObject;
    if (url != nil) {
        const BOOL scoped = [url startAccessingSecurityScopedResource];
        (void)scoped;
        if (url.path != nil) {
            result.status = PickStatus::Ok;
            result.path = url.path.UTF8String;
        }
    }
    [self finishOne:std::move(result)];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    if (kind == 0) {
        PickPaths cancelled;
        cancelled.status = PickStatus::Cancelled;
        [self finishOpen:std::move(cancelled)];
    } else {
        PickPath cancelled;
        cancelled.status = PickStatus::Cancelled;
        [self finishOne:std::move(cancelled)];
    }
}

- (void)present {
    UIViewController* host = presenter_for(owner);
    if (host == nil) {
        // No scene yet (headless test). Leave the App handler pending.
        silent = YES;
        [self releaseSelf];
        return;
    }
    NSArray<UTType*>* types = content_types(extensions, kind == 2);
    UIDocumentPickerViewController* panel = nil;
    if (kind == 1) {
        NSString* name = defaultName.empty()
                             ? @"Untitled"
                             : [NSString stringWithUTF8String:defaultName.c_str()];
        name = name.lastPathComponent;
        if (name.length == 0) {
            name = @"Untitled";
        }
        NSString* tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
        [[NSFileManager defaultManager] createFileAtPath:tmp contents:[NSData data] attributes:nil];
        NSURL* source = [NSURL fileURLWithPath:tmp];
        panel = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ source ] asCopy:YES];
    } else {
        const BOOL copy_in = (kind == 0) ? YES : NO;
        panel = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types
                                                                            asCopy:copy_in];
        panel.allowsMultipleSelection = (kind == 0 && multi);
    }
    if (panel == nil) {
        silent = YES;
        [self releaseSelf];
        return;
    }
    if (!title.empty()) {
        panel.title = [NSString stringWithUTF8String:title.c_str()];
    }
    panel.delegate = self;
    picker = panel;
    [host presentViewController:panel animated:YES completion:nil];
}

@end

namespace {

void replace_delegate() {
    if (g_delegate == nil) {
        return;
    }
    g_delegate->silent = YES;
    if (g_delegate->picker != nil) {
        [g_delegate->picker dismissViewControllerAnimated:NO completion:nil];
    }
    [g_delegate releaseSelf];
}

void ios_present(int kind, void* owner, const FileDialogOptions& opt) {
    replace_delegate();
    ZuiDocumentDelegate* delegate = [[ZuiDocumentDelegate alloc] init];
    delegate->kind = kind;
    delegate->multi = opt.multi ? YES : NO;
    delegate->silent = NO;
    delegate->owner = owner;
    delegate->picker = nil;
    delegate->title = opt.title;
    delegate->defaultName = opt.default_name;
    delegate->extensions = extensions_of(opt);
    g_delegate = delegate;
    if ([NSThread isMainThread]) {
        [delegate present];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [delegate present];
        });
    }
}

std::string ios_sandbox_dir(NSSearchPathDirectory which, std::string_view app_id) {
    @autoreleasepool {
        const std::string id = effective_app_id(app_id);
        NSArray<NSString*>* paths =
            NSSearchPathForDirectoriesInDomains(which, NSUserDomainMask, YES);
        NSString* base = paths.count > 0 ? paths[0] : NSTemporaryDirectory();
        NSString* leaf = [NSString stringWithUTF8String:id.c_str()];
        NSString* dir = [[base stringByAppendingPathComponent:@"zui"] stringByAppendingPathComponent:leaf];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        if (dir.UTF8String == nullptr) {
            return {};
        }
        return std::string(dir.UTF8String);
    }
}

void install_keyboard_observer() {
    if (g_keyboard_installed) {
        return;
    }
    g_keyboard_installed = true;
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    NSOperationQueue* queue = [NSOperationQueue mainQueue];
    [center addObserverForName:UIKeyboardWillChangeFrameNotification
                        object:nil
                         queue:queue
                    usingBlock:^(NSNotification* note) {
                        CGRect frame = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
                        CGRect screen = [UIScreen mainScreen].bounds;
                        CGFloat overlap = CGRectGetMaxY(screen) - CGRectGetMinY(frame);
                        if (overlap < 0.f) {
                            overlap = 0.f;
                        }
                        if (overlap > screen.size.height) {
                            overlap = 0.f;
                        }
                        g_keyboard_bottom = static_cast<float>(overlap);
                    }];
    [center addObserverForName:UIKeyboardWillHideNotification
                        object:nil
                         queue:queue
                    usingBlock:^(NSNotification*) {
                        g_keyboard_bottom = 0.f;
                    }];
}

class IosPlatform final : public zui::backends::IPlatform {
public:
    zui::PickPaths pick_open(zui::NativeWindowId, const FileDialogOptions&) override {
        PickPaths injected;
        if (zui::backends::mobile_consume_injected_open(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_save(zui::NativeWindowId, const FileDialogOptions&) override {
        PickPath injected;
        if (zui::backends::mobile_consume_injected_save(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_folder(zui::NativeWindowId, const FileDialogOptions&) override {
        PickPath injected;
        if (zui::backends::mobile_consume_injected_folder(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    bool picks_are_async() const override { return true; }

    void begin_pick_open(zui::NativeWindowId owner, const FileDialogOptions& opt) override {
        ios_present(0, owner, opt);
    }

    void begin_pick_save(zui::NativeWindowId owner, const FileDialogOptions& opt) override {
        ios_present(1, owner, opt);
    }

    void begin_pick_folder(zui::NativeWindowId owner, const FileDialogOptions& opt) override {
        ios_present(2, owner, opt);
    }

    bool open_uri(std::string_view uri) override {
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
        @autoreleasepool {
            NSString* text = [NSString stringWithUTF8String:std::string(uri).c_str()];
            NSURL* url = [NSURL URLWithString:text];
            if (url == nil) {
                return false;
            }
            UIApplication* app = [UIApplication sharedApplication];
            if (app == nil || ![app canOpenURL:url]) {
                return false;
            }
            [app openURL:url options:@{} completionHandler:nil];
            return true;
        }
    }

    std::string user_data_dir(std::string_view app_id) const override {
        return ios_sandbox_dir(NSApplicationSupportDirectory, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return ios_sandbox_dir(NSCachesDirectory, app_id);
    }

    PermissionState permission_state(std::string_view name) const override {
        return ios_permission_state(name);
    }

    bool permissions_are_async() const override { return true; }

    void begin_permission_request(std::string_view name) override {
        ios_begin_permission(name);
    }
};

}  // namespace

namespace zui {
namespace backends {

std::unique_ptr<IPlatform> create_ios_platform_device() {
    return std::make_unique<IosPlatform>();
}

bool device_apply_host_media(HostMediaFacts& facts) {
    facts.hover = false;
    facts.any_hover = false;
    facts.pointer = PointerAccuracy::Coarse;
    facts.any_pointer = PointerAccuracy::Coarse;
    facts.reduced_motion = UIAccessibilityIsReduceMotionEnabled() ? true : false;
    facts.color_scheme = ColorSchemePreference::Light;
    UIScreen* screen = [UIScreen mainScreen];
    if (screen != nil) {
        const UIUserInterfaceStyle style = screen.traitCollection.userInterfaceStyle;
        if (style == UIUserInterfaceStyleDark) {
            facts.color_scheme = ColorSchemePreference::Dark;
        } else if (style == UIUserInterfaceStyleLight) {
            facts.color_scheme = ColorSchemePreference::Light;
        } else {
            facts.color_scheme = ColorSchemePreference::NoPreference;
        }
    }
    return true;
}

float device_keyboard_bottom() {
    install_keyboard_observer();
    return g_keyboard_bottom;
}

void device_install_keyboard_observer() { install_keyboard_observer(); }

ClipboardText device_clipboard_text() {
    ClipboardText out;
    @autoreleasepool {
        UIPasteboard* board = [UIPasteboard generalPasteboard];
        if (board == nil) {
            return out;
        }
        out.status = ClipboardStatus::Ok;
        if (board.string != nil) {
            out.text = board.string.UTF8String;
        }
    }
    return out;
}

bool device_set_clipboard_text(std::string_view text) {
    @autoreleasepool {
        UIPasteboard* board = [UIPasteboard generalPasteboard];
        if (board == nil) {
            return false;
        }
        board.string = [NSString stringWithUTF8String:std::string(text).c_str()];
        return true;
    }
}

}  // namespace backends
}  // namespace zui
