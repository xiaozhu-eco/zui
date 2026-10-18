#ifndef ZUI_CAPI_H
#define ZUI_CAPI_H

/* C ABI for zui. C++ applications keep including <zui/app.h>. C, Python, and
   Rust include this header and link zui. It is a POD wrapper around App: no
   classes, no exceptions, no std::string.
   Append fields to the structs below. Do not reorder or insert. Callers set
   struct_size to sizeof the struct they compiled against. The library reads
   only the prefix that size covers.

   Strings are UTF-8. Inputs are NUL-terminated; NULL means empty. An embedded
   NUL cannot be passed. Outputs use a caller buffer:
     - buf == NULL or cap == 0 writes nothing.
     - cap > 0 writes a NUL and at most cap-1 bytes.
     - the return value is the full length in bytes, excluding the NUL.
       A return value >= cap means the buffer was truncated.
   Model callbacks that write a string follow the same rule. A model key is
   not necessarily NUL-terminated; use key_len.

   Pointers passed into a callback (zui_event, pick results, zui_task_signal)
   are valid only until that callback returns. Do not free them and do not
   store them. Memory the library allocates (pick paths, clipboard images) is
   released with the matching zui_*_free. Do not call free() on it.

   Every zui_app_* call runs on the thread that created the app. The exception
   is zui_task_work, which runs on a worker and may call only
   zui_task_cancelled and zui_task_set_progress. Callbacks run on the creating
   thread and may re-enter the API. Do not destroy the app from a callback.
   Do not destroy a list model while set_model is using it.

   Boolean results are 0 or 1. zui_app_run returns a process exit code.
   zui_app_open_window returns a window id, or 0 when the document did not load.
   Timer, watch, and task ids are never 0 when live. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZUI_CAPI_ABI_VERSION 1u

#if defined(ZUI_CAPI_STATIC) || defined(ZUI_STATIC_DEFINE)
#  define ZUI_CAPI_API
#elif defined(_WIN32)
#  if defined(ZUI_CAPI_BUILD) || defined(zui_EXPORTS)
#    define ZUI_CAPI_API __declspec(dllexport)
#  else
#    define ZUI_CAPI_API __declspec(dllimport)
#  endif
#else
#  if defined(ZUI_CAPI_BUILD) || defined(zui_EXPORTS)
#    define ZUI_CAPI_API __attribute__((visibility("default")))
#  else
#    define ZUI_CAPI_API
#  endif
#endif

#define ZUI_GAMEPAD_AXES 4
#define ZUI_GAMEPAD_BUTTONS 17

typedef struct zui_app zui_app;
typedef struct zui_list_model zui_list_model;
typedef struct zui_task_signal zui_task_signal;

typedef enum zui_pick_status {
    ZUI_PICK_OK = 0,
    ZUI_PICK_CANCELLED = 1,
    ZUI_PICK_UNSUPPORTED = 2
} zui_pick_status;

typedef enum zui_clipboard_status {
    ZUI_CLIPBOARD_OK = 0,
    ZUI_CLIPBOARD_UNSUPPORTED = 1
} zui_clipboard_status;

typedef enum zui_permission {
    ZUI_PERMISSION_CAMERA = 0,
    ZUI_PERMISSION_MICROPHONE = 1,
    ZUI_PERMISSION_LOCATION = 2,
    ZUI_PERMISSION_NETWORK = 3,
    ZUI_PERMISSION_NETWORK_STATE = 4,
    ZUI_PERMISSION_WIFI_STATE = 5,
    ZUI_PERMISSION_CHANGE_WIFI = 6,
    ZUI_PERMISSION_MULTICAST = 7,
    ZUI_PERMISSION_VIBRATE = 8,
    ZUI_PERMISSION_NFC = 9,
    ZUI_PERMISSION_WAKE_LOCK = 10,
    ZUI_PERMISSION_BOOT_COMPLETED = 11,
    ZUI_PERMISSION_BIOMETRIC = 12,
    ZUI_PERMISSION_MODIFY_AUDIO = 13,
    ZUI_PERMISSION_FOREGROUND_SERVICE = 14,
    ZUI_PERMISSION_FOREGROUND_CAMERA = 15,
    ZUI_PERMISSION_FOREGROUND_MICROPHONE = 16,
    ZUI_PERMISSION_FOREGROUND_LOCATION = 17,
    ZUI_PERMISSION_FOREGROUND_PLAYBACK = 18,
    ZUI_PERMISSION_FOREGROUND_SYNC = 19,
    ZUI_PERMISSION_FOREGROUND_CONNECTED = 20,
    ZUI_PERMISSION_HIGH_SAMPLING_RATE = 21,
    ZUI_PERMISSION_DETECT_SCREEN_CAPTURE = 22,
    ZUI_PERMISSION_COARSE_LOCATION = 23,
    ZUI_PERMISSION_LOCATION_BACKGROUND = 24,
    ZUI_PERMISSION_STORAGE = 25,
    ZUI_PERMISSION_STORAGE_WRITE = 26,
    ZUI_PERMISSION_MEDIA_IMAGES = 27,
    ZUI_PERMISSION_MEDIA_VIDEO = 28,
    ZUI_PERMISSION_MEDIA_AUDIO = 29,
    ZUI_PERMISSION_MEDIA_VISUAL_SELECTED = 30,
    ZUI_PERMISSION_MEDIA_LOCATION = 31,
    ZUI_PERMISSION_NOTIFICATIONS = 32,
    ZUI_PERMISSION_BLUETOOTH_SCAN = 33,
    ZUI_PERMISSION_BLUETOOTH_CONNECT = 34,
    ZUI_PERMISSION_BLUETOOTH_ADVERTISE = 35,
    ZUI_PERMISSION_NEARBY_WIFI = 36,
    ZUI_PERMISSION_CONTACTS_READ = 37,
    ZUI_PERMISSION_CONTACTS_WRITE = 38,
    ZUI_PERMISSION_ACCOUNTS = 39,
    ZUI_PERMISSION_CALENDAR_READ = 40,
    ZUI_PERMISSION_CALENDAR_WRITE = 41,
    ZUI_PERMISSION_PHONE_CALL = 42,
    ZUI_PERMISSION_PHONE_STATE = 43,
    ZUI_PERMISSION_PHONE_NUMBERS = 44,
    ZUI_PERMISSION_CALL_LOG_READ = 45,
    ZUI_PERMISSION_CALL_LOG_WRITE = 46,
    ZUI_PERMISSION_ANSWER_CALLS = 47,
    ZUI_PERMISSION_SMS_SEND = 48,
    ZUI_PERMISSION_SMS_RECEIVE = 49,
    ZUI_PERMISSION_SMS_READ = 50,
    ZUI_PERMISSION_BODY_SENSORS = 51,
    ZUI_PERMISSION_BODY_SENSORS_BACKGROUND = 52,
    ZUI_PERMISSION_ACTIVITY_RECOGNITION = 53,
    ZUI_PERMISSION_MANAGE_STORAGE = 54,
    ZUI_PERMISSION_OVERLAY = 55,
    ZUI_PERMISSION_INSTALL_PACKAGES = 56,
    ZUI_PERMISSION_IGNORE_BATTERY = 57,
    ZUI_PERMISSION_EXACT_ALARM = 58,
    ZUI_PERMISSION_WRITE_SETTINGS = 59,
    ZUI_PERMISSION_FULL_SCREEN_INTENT = 60
} zui_permission;

typedef enum zui_permission_state {
    ZUI_PERMISSION_UNSUPPORTED = 0,
    ZUI_PERMISSION_NOT_REQUESTED = 1,
    ZUI_PERMISSION_GRANTED = 2,
    ZUI_PERMISSION_DENIED = 3,
    ZUI_PERMISSION_BLOCKED = 4
} zui_permission_state;

typedef enum zui_scroll_align {
    ZUI_SCROLL_START = 0,
    ZUI_SCROLL_CENTER = 1,
    ZUI_SCROLL_END = 2,
    ZUI_SCROLL_NEAREST = 3
} zui_scroll_align;

typedef enum zui_pointer_type {
    ZUI_POINTER_MOUSE = 0,
    ZUI_POINTER_PEN = 1,
    ZUI_POINTER_TOUCH = 2
} zui_pointer_type;

typedef enum zui_drop_effect {
    ZUI_DROP_EFFECT_NONE = 0,
    ZUI_DROP_EFFECT_COPY = 1,
    ZUI_DROP_EFFECT_MOVE = 2,
    ZUI_DROP_EFFECT_LINK = 3
} zui_drop_effect;

typedef enum zui_effect_allowed {
    ZUI_EFFECT_ALLOWED_UNINITIALIZED = 0,
    ZUI_EFFECT_ALLOWED_NONE = 1,
    ZUI_EFFECT_ALLOWED_COPY = 2,
    ZUI_EFFECT_ALLOWED_COPY_LINK = 3,
    ZUI_EFFECT_ALLOWED_COPY_MOVE = 4,
    ZUI_EFFECT_ALLOWED_LINK = 5,
    ZUI_EFFECT_ALLOWED_LINK_MOVE = 6,
    ZUI_EFFECT_ALLOWED_MOVE = 7,
    ZUI_EFFECT_ALLOWED_ALL = 8
} zui_effect_allowed;

typedef struct zui_rect {
    float x;
    float y;
    float width;
    float height;
} zui_rect;

typedef struct zui_key {
    const char* key;
    int ctrl;
    int shift;
    int alt;
    int meta;
    int composing;
} zui_key;

typedef struct zui_drop {
    const char* const* paths;
    size_t count;
} zui_drop;

typedef struct zui_wheel {
    float delta_x;
    float delta_y;
    int ctrl;
    int shift;
    int alt;
    int meta;
} zui_wheel;

typedef struct zui_pointer {
    float x;
    float y;
    const char* button;
    int buttons;
    int count;
    int ctrl;
    int shift;
    int alt;
    int meta;
    int pointer_id;
    int pointer_type;
    int is_primary;
    float pressure;
} zui_pointer;

typedef struct zui_drag {
    float x;
    float y;
    const char* source_id;
    const char* text;
    const char* uri;
    int effect;
    int allowed;
} zui_drag;

/* Filled by the library for one handler call. _impl is private. */
typedef struct zui_event {
    uint32_t abi_version;
    uint32_t struct_size;
    void* _impl;
    const char* type;
    const char* name;
    const char* source;
    const char* value;
    const zui_key* key;
    const zui_drop* drop;
    const zui_wheel* wheel;
    const zui_pointer* pointer;
    const zui_drag* drag;
} zui_event;

typedef struct zui_file_filter {
    uint32_t struct_size;
    const char* label;
    const char* patterns;
} zui_file_filter;

typedef struct zui_file_dialog {
    uint32_t struct_size;
    const char* title;
    const char* default_name;
    const zui_file_filter* filters;
    size_t filter_count;
    int multi;
} zui_file_dialog;

typedef struct zui_element_box {
    uint32_t struct_size;
    int laid;
    zui_rect border;
    float content_width;
    float content_height;
    float client_width;
    float client_height;
    float scroll_width;
    float scroll_height;
} zui_element_box;

typedef struct zui_gamepad_button {
    int pressed;
    float value;
} zui_gamepad_button;

typedef struct zui_gamepad {
    uint32_t struct_size;
    int connected;
    char id[256];
    float axes[ZUI_GAMEPAD_AXES];
    zui_gamepad_button buttons[ZUI_GAMEPAD_BUTTONS];
} zui_gamepad;

typedef struct zui_display {
    uint32_t struct_size;
    int known;
    char id[256];
    int index;
    int width_px;
    int height_px;
    float scale;
} zui_display;

typedef struct zui_pick_paths {
    int status;
    size_t count;
    char** paths;
} zui_pick_paths;

typedef struct zui_pick_path {
    int status;
    char* path;
} zui_pick_path;

typedef struct zui_image {
    int status;
    int width;
    int height;
    uint8_t* rgba;
    size_t bytes;
} zui_image;

/* sizeof each ABI struct, as compiled into this library. */
typedef struct zui_capi_layout {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t file_dialog;
    uint32_t file_filter;
    uint32_t element_box;
    uint32_t gamepad;
    uint32_t display;
    uint32_t list_vtable;
    uint32_t table_vtable;
    uint32_t event;
    uint32_t key;
    uint32_t pointer;
    uint32_t wheel;
    uint32_t drag;
    uint32_t drop;
    uint32_t pick_paths;
    uint32_t pick_path;
    uint32_t image;
} zui_capi_layout;

typedef void (*zui_handler)(zui_event* event, void* user);
typedef void (*zui_fn)(void* user);
typedef void (*zui_watch_fn)(const char* id, void* user);
typedef void (*zui_pick_paths_fn)(const zui_pick_paths* paths, void* user);
typedef void (*zui_pick_path_fn)(const zui_pick_path* path, void* user);
typedef void (*zui_permission_fn)(zui_permission_state state, void* user);
typedef void (*zui_task_work)(zui_task_signal* signal, void* user);
typedef void (*zui_task_done)(void* user);
typedef void (*zui_list_visit_fn)(const zui_list_model* child, void* visit_user);

typedef struct zui_list_vtable {
    uint32_t struct_size;
    size_t (*row_count)(void* user);
    size_t (*value)(void* user, size_t row, const char* key, size_t key_len, char* buf, size_t cap);
    size_t (*row_key)(void* user, size_t row, char* buf, size_t cap);
    void (*with_children)(void* user, size_t row, zui_list_visit_fn visit, void* visit_user);
} zui_list_vtable;

typedef struct zui_table_vtable {
    uint32_t struct_size;
    size_t (*row_count)(void* user);
    size_t (*column_count)(void* user);
    size_t (*column_key)(void* user, size_t column, char* buf, size_t cap);
    size_t (*cell)(void* user, size_t row, size_t column, char* buf, size_t cap);
    size_t (*cell_value)(void* user, size_t row, size_t column, const char* key, size_t key_len,
                         char* buf, size_t cap);
    size_t (*row_key)(void* user, size_t row, char* buf, size_t cap);
} zui_table_vtable;

ZUI_CAPI_API uint32_t zui_capi_abi_version(void);
ZUI_CAPI_API void zui_capi_fill_layout(zui_capi_layout* out);

ZUI_CAPI_API zui_app* zui_app_create(int argc, char** argv);
ZUI_CAPI_API void zui_app_destroy(zui_app* app);

ZUI_CAPI_API int zui_app_load(zui_app* app, const char* path);
ZUI_CAPI_API int zui_app_load_fragment(zui_app* app, const char* path);
ZUI_CAPI_API int zui_app_load_fragment_into(zui_app* app, const char* path, const char* into_id);
ZUI_CAPI_API int zui_app_replace_fragment(zui_app* app, const char* path, const char* into_id);
ZUI_CAPI_API int zui_app_unload_fragment(zui_app* app, const char* id);
ZUI_CAPI_API int zui_app_set_theme(zui_app* app, const char* name);

ZUI_CAPI_API void zui_app_bind(zui_app* app, const char* name, zui_handler fn, void* user);
ZUI_CAPI_API void zui_app_unbind(zui_app* app, const char* name);

ZUI_CAPI_API int zui_app_set(zui_app* app, const char* id, const char* field, const char* value);
ZUI_CAPI_API size_t zui_app_get(zui_app* app, const char* id, const char* field, char* buf, size_t cap);

ZUI_CAPI_API zui_list_model* zui_list_model_create(const zui_list_vtable* vtable, void* user);
ZUI_CAPI_API zui_list_model* zui_table_model_create(const zui_table_vtable* vtable, void* user);
ZUI_CAPI_API void zui_list_model_destroy(zui_list_model* model);
ZUI_CAPI_API int zui_app_set_model(zui_app* app, const char* id, const zui_list_model* model);

ZUI_CAPI_API uint64_t zui_app_watch(zui_app* app, const char* id, zui_watch_fn fn, void* user);
ZUI_CAPI_API void zui_app_unwatch(zui_app* app, uint64_t watch);

ZUI_CAPI_API int zui_app_measure(zui_app* app, const char* id, zui_element_box* out);
ZUI_CAPI_API int zui_app_add_class(zui_app* app, const char* id, const char* class_name);
ZUI_CAPI_API int zui_app_remove_class(zui_app* app, const char* id, const char* class_name);

ZUI_CAPI_API void zui_app_defer(zui_app* app, zui_fn fn, void* user);
ZUI_CAPI_API uint64_t zui_app_set_timeout(zui_app* app, zui_fn fn, void* user, int delay_ms);
ZUI_CAPI_API uint64_t zui_app_set_interval(zui_app* app, zui_fn fn, void* user, int interval_ms);
ZUI_CAPI_API void zui_app_clear_timer(zui_app* app, uint64_t timer);

ZUI_CAPI_API int zui_app_run(zui_app* app);
ZUI_CAPI_API int zui_app_pump_frame(zui_app* app);
ZUI_CAPI_API void zui_app_quit(zui_app* app);

ZUI_CAPI_API void zui_app_set_app_id(zui_app* app, const char* id);
ZUI_CAPI_API size_t zui_app_user_data_dir(zui_app* app, char* buf, size_t cap);
ZUI_CAPI_API size_t zui_app_cache_dir(zui_app* app, char* buf, size_t cap);

ZUI_CAPI_API void zui_app_pick_open(zui_app* app, const zui_file_dialog* dialog, zui_pick_paths* out);
ZUI_CAPI_API void zui_app_pick_save(zui_app* app, const zui_file_dialog* dialog, zui_pick_path* out);
ZUI_CAPI_API void zui_app_pick_folder(zui_app* app, const zui_file_dialog* dialog, zui_pick_path* out);
ZUI_CAPI_API void zui_app_pick_open_async(zui_app* app, const zui_file_dialog* dialog,
                                         zui_pick_paths_fn fn, void* user);
ZUI_CAPI_API void zui_app_pick_save_async(zui_app* app, const zui_file_dialog* dialog,
                                         zui_pick_path_fn fn, void* user);
ZUI_CAPI_API void zui_app_pick_folder_async(zui_app* app, const zui_file_dialog* dialog,
                                           zui_pick_path_fn fn, void* user);
ZUI_CAPI_API void zui_pick_paths_free(zui_pick_paths* paths);
ZUI_CAPI_API void zui_pick_path_free(zui_pick_path* path);

ZUI_CAPI_API int zui_app_permission_state(zui_app* app, zui_permission permission);
ZUI_CAPI_API void zui_app_request_permission(zui_app* app, zui_permission permission,
                                             zui_permission_fn fn, void* user);

ZUI_CAPI_API size_t zui_app_clipboard_text(zui_app* app, int* status, char* buf, size_t cap);
ZUI_CAPI_API int zui_app_set_clipboard_text(zui_app* app, const char* text);
ZUI_CAPI_API void zui_app_clipboard_image(zui_app* app, zui_image* out);
ZUI_CAPI_API int zui_app_set_clipboard_image(zui_app* app, int width, int height,
                                            const uint8_t* rgba, size_t bytes);
ZUI_CAPI_API void zui_image_free(zui_image* image);

ZUI_CAPI_API int zui_app_open_uri(zui_app* app, const char* uri);
ZUI_CAPI_API int zui_app_scroll_into_view(zui_app* app, const char* id, int align);
ZUI_CAPI_API int zui_app_focus(zui_app* app, const char* id);
ZUI_CAPI_API int zui_app_blur(zui_app* app);

ZUI_CAPI_API void zui_app_gamepad(zui_app* app, size_t index, zui_gamepad* out);
ZUI_CAPI_API size_t zui_app_gamepad_count(zui_app* app);

ZUI_CAPI_API uint64_t zui_app_start_task(zui_app* app, zui_task_work work, zui_task_done done,
                                        void* user, const char* progress_id);
ZUI_CAPI_API int zui_app_cancel_task(zui_app* app, uint64_t task);
ZUI_CAPI_API int zui_task_cancelled(const zui_task_signal* signal);
ZUI_CAPI_API void zui_task_set_progress(zui_task_signal* signal, float fraction);

ZUI_CAPI_API int zui_app_set_title(zui_app* app, const char* title);
ZUI_CAPI_API int zui_app_set_window_size(zui_app* app, int width, int height);
ZUI_CAPI_API int zui_app_set_icon(zui_app* app, int width, int height, const uint8_t* rgba,
                                 size_t bytes);
ZUI_CAPI_API int zui_app_minimize(zui_app* app);
ZUI_CAPI_API int zui_app_set_maximized(zui_app* app, int on);
ZUI_CAPI_API int zui_app_set_fullscreen(zui_app* app, int on);
ZUI_CAPI_API int zui_app_set_topmost(zui_app* app, int on);
ZUI_CAPI_API int zui_app_minimized(zui_app* app);
ZUI_CAPI_API int zui_app_maximized(zui_app* app);
ZUI_CAPI_API int zui_app_window_visible(zui_app* app);
ZUI_CAPI_API int zui_app_fullscreen(zui_app* app);
ZUI_CAPI_API int zui_app_topmost(zui_app* app);
ZUI_CAPI_API void zui_app_display(zui_app* app, zui_display* out);
ZUI_CAPI_API int zui_app_open_window(zui_app* app, const char* html_path, int modal);
ZUI_CAPI_API int zui_app_close_window(zui_app* app, int window);

ZUI_CAPI_API int zui_app_enable_diagnostics(zui_app* app, const char* log_path);
ZUI_CAPI_API void zui_app_log_line(zui_app* app, const char* line);

ZUI_CAPI_API void zui_event_prevent_default(zui_event* event);
ZUI_CAPI_API int zui_event_default_prevented(const zui_event* event);
ZUI_CAPI_API size_t zui_event_data(const zui_event* event, const char* name, char* buf, size_t cap);
ZUI_CAPI_API size_t zui_event_drag_data(const zui_event* event, const char* name, char* buf,
                                       size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* ZUI_CAPI_H */
