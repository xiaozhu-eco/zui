//! Raw FFI for the zui C ABI.
//!
//! `bindings/c/include/zui_capi.h` is the source of truth. Field order matches
//! that header. This crate loads `zui.dll` / `libzui.so` / `libzui.dylib` at
//! runtime and does not link an import library.

#![allow(non_camel_case_types)]

use std::ffi::CStr;
use std::os::raw::{c_char, c_int, c_void};
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

pub const ABI_VERSION: u32 = 1;

#[repr(C)]
pub struct zui_app {
    _private: [u8; 0],
}
#[repr(C)]
pub struct zui_list_model {
    _private: [u8; 0],
}
#[repr(C)]
pub struct zui_task_signal {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_rect {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_key {
    pub key: *const c_char,
    pub ctrl: c_int,
    pub shift: c_int,
    pub alt: c_int,
    pub meta: c_int,
    pub composing: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_drop {
    pub paths: *const *const c_char,
    pub count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_wheel {
    pub delta_x: f32,
    pub delta_y: f32,
    pub ctrl: c_int,
    pub shift: c_int,
    pub alt: c_int,
    pub meta: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_pointer {
    pub x: f32,
    pub y: f32,
    pub button: *const c_char,
    pub buttons: c_int,
    pub count: c_int,
    pub ctrl: c_int,
    pub shift: c_int,
    pub alt: c_int,
    pub meta: c_int,
    pub pointer_id: c_int,
    pub pointer_type: c_int,
    pub is_primary: c_int,
    pub pressure: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_drag {
    pub x: f32,
    pub y: f32,
    pub source_id: *const c_char,
    pub text: *const c_char,
    pub uri: *const c_char,
    pub effect: c_int,
    pub allowed: c_int,
}

/// C field `type` is `type_name` here (`type` is a Rust keyword). Order is unchanged.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_event {
    pub abi_version: u32,
    pub struct_size: u32,
    pub _impl: *mut c_void,
    pub type_name: *const c_char,
    pub name: *const c_char,
    pub source: *const c_char,
    pub value: *const c_char,
    pub key: *const zui_key,
    pub drop: *const zui_drop,
    pub wheel: *const zui_wheel,
    pub pointer: *const zui_pointer,
    pub drag: *const zui_drag,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_file_filter {
    pub struct_size: u32,
    pub label: *const c_char,
    pub patterns: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_file_dialog {
    pub struct_size: u32,
    pub title: *const c_char,
    pub default_name: *const c_char,
    pub filters: *const zui_file_filter,
    pub filter_count: usize,
    pub multi: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_element_box {
    pub struct_size: u32,
    pub laid: c_int,
    pub border: zui_rect,
    pub content_width: f32,
    pub content_height: f32,
    pub client_width: f32,
    pub client_height: f32,
    pub scroll_width: f32,
    pub scroll_height: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_gamepad_button {
    pub pressed: c_int,
    pub value: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_gamepad {
    pub struct_size: u32,
    pub connected: c_int,
    pub id: [c_char; 256],
    pub axes: [f32; 4],
    pub buttons: [zui_gamepad_button; 17],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_display {
    pub struct_size: u32,
    pub known: c_int,
    pub id: [c_char; 256],
    pub index: c_int,
    pub width_px: c_int,
    pub height_px: c_int,
    pub scale: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_pick_paths {
    pub status: c_int,
    pub count: usize,
    pub paths: *mut *mut c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_pick_path {
    pub status: c_int,
    pub path: *mut c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_image {
    pub status: c_int,
    pub width: c_int,
    pub height: c_int,
    pub rgba: *mut u8,
    pub bytes: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_capi_layout {
    pub struct_size: u32,
    pub abi_version: u32,
    pub file_dialog: u32,
    pub file_filter: u32,
    pub element_box: u32,
    pub gamepad: u32,
    pub display: u32,
    pub list_vtable: u32,
    pub table_vtable: u32,
    pub event: u32,
    pub key: u32,
    pub pointer: u32,
    pub wheel: u32,
    pub drag: u32,
    pub drop: u32,
    pub pick_paths: u32,
    pub pick_path: u32,
    pub image: u32,
}

pub type zui_handler = Option<unsafe extern "C" fn(*mut zui_event, *mut c_void)>;
pub type zui_fn = Option<unsafe extern "C" fn(*mut c_void)>;
pub type zui_watch_fn = Option<unsafe extern "C" fn(*const c_char, *mut c_void)>;
pub type zui_pick_paths_fn = Option<unsafe extern "C" fn(*const zui_pick_paths, *mut c_void)>;
pub type zui_pick_path_fn = Option<unsafe extern "C" fn(*const zui_pick_path, *mut c_void)>;
pub type zui_task_work = Option<unsafe extern "C" fn(*mut zui_task_signal, *mut c_void)>;
pub type zui_task_done = Option<unsafe extern "C" fn(*mut c_void)>;
pub type zui_list_visit_fn = Option<unsafe extern "C" fn(*const zui_list_model, *mut c_void)>;
pub type zui_row_count_fn = Option<unsafe extern "C" fn(*mut c_void) -> usize>;
pub type zui_value_fn = Option<
    unsafe extern "C" fn(*mut c_void, usize, *const c_char, usize, *mut c_char, usize) -> usize,
>;
pub type zui_row_key_fn = Option<unsafe extern "C" fn(*mut c_void, usize, *mut c_char, usize) -> usize>;
pub type zui_with_children_fn =
    Option<unsafe extern "C" fn(*mut c_void, usize, zui_list_visit_fn, *mut c_void)>;
pub type zui_column_count_fn = Option<unsafe extern "C" fn(*mut c_void) -> usize>;
pub type zui_column_key_fn = Option<unsafe extern "C" fn(*mut c_void, usize, *mut c_char, usize) -> usize>;
pub type zui_cell_fn = Option<unsafe extern "C" fn(*mut c_void, usize, usize, *mut c_char, usize) -> usize>;
pub type zui_cell_value_fn = Option<
    unsafe extern "C" fn(*mut c_void, usize, usize, *const c_char, usize, *mut c_char, usize) -> usize,
>;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_list_vtable {
    pub struct_size: u32,
    pub row_count: zui_row_count_fn,
    pub value: zui_value_fn,
    pub row_key: zui_row_key_fn,
    pub with_children: zui_with_children_fn,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct zui_table_vtable {
    pub struct_size: u32,
    pub row_count: zui_row_count_fn,
    pub column_count: zui_column_count_fn,
    pub column_key: zui_column_key_fn,
    pub cell: zui_cell_fn,
    pub cell_value: zui_cell_value_fn,
    pub row_key: zui_row_key_fn,
}

pub struct Api {
    pub zui_capi_abi_version: unsafe extern "C" fn() -> u32,
    pub zui_capi_fill_layout: unsafe extern "C" fn(*mut zui_capi_layout),
    pub zui_app_create: unsafe extern "C" fn(c_int, *mut *mut c_char) -> *mut zui_app,
    pub zui_app_destroy: unsafe extern "C" fn(*mut zui_app),
    pub zui_app_load: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_load_fragment: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_load_fragment_into: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char) -> c_int,
    pub zui_app_replace_fragment: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char) -> c_int,
    pub zui_app_unload_fragment: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_set_theme: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_bind: unsafe extern "C" fn(*mut zui_app, *const c_char, zui_handler, *mut c_void),
    pub zui_app_unbind: unsafe extern "C" fn(*mut zui_app, *const c_char),
    pub zui_app_set: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char, *const c_char) -> c_int,
    pub zui_app_get: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char, *mut c_char, usize) -> usize,
    pub zui_list_model_create: unsafe extern "C" fn(*const zui_list_vtable, *mut c_void) -> *mut zui_list_model,
    pub zui_table_model_create: unsafe extern "C" fn(*const zui_table_vtable, *mut c_void) -> *mut zui_list_model,
    pub zui_list_model_destroy: unsafe extern "C" fn(*mut zui_list_model),
    pub zui_app_set_model: unsafe extern "C" fn(*mut zui_app, *const c_char, *const zui_list_model) -> c_int,
    pub zui_app_watch: unsafe extern "C" fn(*mut zui_app, *const c_char, zui_watch_fn, *mut c_void) -> u64,
    pub zui_app_unwatch: unsafe extern "C" fn(*mut zui_app, u64),
    pub zui_app_measure: unsafe extern "C" fn(*mut zui_app, *const c_char, *mut zui_element_box) -> c_int,
    pub zui_app_add_class: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char) -> c_int,
    pub zui_app_remove_class: unsafe extern "C" fn(*mut zui_app, *const c_char, *const c_char) -> c_int,
    pub zui_app_defer: unsafe extern "C" fn(*mut zui_app, zui_fn, *mut c_void),
    pub zui_app_set_timeout: unsafe extern "C" fn(*mut zui_app, zui_fn, *mut c_void, c_int) -> u64,
    pub zui_app_set_interval: unsafe extern "C" fn(*mut zui_app, zui_fn, *mut c_void, c_int) -> u64,
    pub zui_app_clear_timer: unsafe extern "C" fn(*mut zui_app, u64),
    pub zui_app_run: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_pump_frame: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_quit: unsafe extern "C" fn(*mut zui_app),
    pub zui_app_set_app_id: unsafe extern "C" fn(*mut zui_app, *const c_char),
    pub zui_app_user_data_dir: unsafe extern "C" fn(*mut zui_app, *mut c_char, usize) -> usize,
    pub zui_app_cache_dir: unsafe extern "C" fn(*mut zui_app, *mut c_char, usize) -> usize,
    pub zui_app_pick_open: unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, *mut zui_pick_paths),
    pub zui_app_pick_save: unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, *mut zui_pick_path),
    pub zui_app_pick_folder: unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, *mut zui_pick_path),
    pub zui_app_pick_open_async:
        unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, zui_pick_paths_fn, *mut c_void),
    pub zui_app_pick_save_async:
        unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, zui_pick_path_fn, *mut c_void),
    pub zui_app_pick_folder_async:
        unsafe extern "C" fn(*mut zui_app, *const zui_file_dialog, zui_pick_path_fn, *mut c_void),
    pub zui_pick_paths_free: unsafe extern "C" fn(*mut zui_pick_paths),
    pub zui_pick_path_free: unsafe extern "C" fn(*mut zui_pick_path),
    pub zui_app_clipboard_text: unsafe extern "C" fn(*mut zui_app, *mut c_int, *mut c_char, usize) -> usize,
    pub zui_app_set_clipboard_text: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_clipboard_image: unsafe extern "C" fn(*mut zui_app, *mut zui_image),
    pub zui_app_set_clipboard_image: unsafe extern "C" fn(*mut zui_app, c_int, c_int, *const u8, usize) -> c_int,
    pub zui_image_free: unsafe extern "C" fn(*mut zui_image),
    pub zui_app_open_uri: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_scroll_into_view: unsafe extern "C" fn(*mut zui_app, *const c_char, c_int) -> c_int,
    pub zui_app_focus: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_blur: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_gamepad: unsafe extern "C" fn(*mut zui_app, usize, *mut zui_gamepad),
    pub zui_app_gamepad_count: unsafe extern "C" fn(*mut zui_app) -> usize,
    pub zui_app_start_task:
        unsafe extern "C" fn(*mut zui_app, zui_task_work, zui_task_done, *mut c_void, *const c_char) -> u64,
    pub zui_app_cancel_task: unsafe extern "C" fn(*mut zui_app, u64) -> c_int,
    pub zui_task_cancelled: unsafe extern "C" fn(*const zui_task_signal) -> c_int,
    pub zui_task_set_progress: unsafe extern "C" fn(*mut zui_task_signal, f32),
    pub zui_app_set_title: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_set_window_size: unsafe extern "C" fn(*mut zui_app, c_int, c_int) -> c_int,
    pub zui_app_set_icon: unsafe extern "C" fn(*mut zui_app, c_int, c_int, *const u8, usize) -> c_int,
    pub zui_app_minimize: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_set_maximized: unsafe extern "C" fn(*mut zui_app, c_int) -> c_int,
    pub zui_app_set_fullscreen: unsafe extern "C" fn(*mut zui_app, c_int) -> c_int,
    pub zui_app_set_topmost: unsafe extern "C" fn(*mut zui_app, c_int) -> c_int,
    pub zui_app_minimized: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_maximized: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_fullscreen: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_topmost: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_window_visible: unsafe extern "C" fn(*mut zui_app) -> c_int,
    pub zui_app_display: unsafe extern "C" fn(*mut zui_app, *mut zui_display),
    pub zui_app_open_window: unsafe extern "C" fn(*mut zui_app, *const c_char, c_int) -> c_int,
    pub zui_app_close_window: unsafe extern "C" fn(*mut zui_app, c_int) -> c_int,
    pub zui_app_enable_diagnostics: unsafe extern "C" fn(*mut zui_app, *const c_char) -> c_int,
    pub zui_app_log_line: unsafe extern "C" fn(*mut zui_app, *const c_char),
    pub zui_event_prevent_default: unsafe extern "C" fn(*mut zui_event),
    pub zui_event_default_prevented: unsafe extern "C" fn(*const zui_event) -> c_int,
    pub zui_event_data: unsafe extern "C" fn(*const zui_event, *const c_char, *mut c_char, usize) -> usize,
    pub zui_event_drag_data: unsafe extern "C" fn(*const zui_event, *const c_char, *mut c_char, usize) -> usize,
}

/// Loaded API. Missing symbols panic. The library stays mapped for the process.
pub fn api() -> &'static Api {
    static API: OnceLock<Api> = OnceLock::new();
    API.get_or_init(load_api)
}

fn load_api() -> Api {
    let module = open_library(&library_path());
    macro_rules! load {
        ($lib:expr, $name:ident) => {
            sym($lib, concat!(stringify!($name), "\0").as_bytes())
        };
    }
    Api {
        zui_capi_abi_version: load!(module, zui_capi_abi_version),
        zui_capi_fill_layout: load!(module, zui_capi_fill_layout),
        zui_app_create: load!(module, zui_app_create),
        zui_app_destroy: load!(module, zui_app_destroy),
        zui_app_load: load!(module, zui_app_load),
        zui_app_load_fragment: load!(module, zui_app_load_fragment),
        zui_app_load_fragment_into: load!(module, zui_app_load_fragment_into),
        zui_app_replace_fragment: load!(module, zui_app_replace_fragment),
        zui_app_unload_fragment: load!(module, zui_app_unload_fragment),
        zui_app_set_theme: load!(module, zui_app_set_theme),
        zui_app_bind: load!(module, zui_app_bind),
        zui_app_unbind: load!(module, zui_app_unbind),
        zui_app_set: load!(module, zui_app_set),
        zui_app_get: load!(module, zui_app_get),
        zui_list_model_create: load!(module, zui_list_model_create),
        zui_table_model_create: load!(module, zui_table_model_create),
        zui_list_model_destroy: load!(module, zui_list_model_destroy),
        zui_app_set_model: load!(module, zui_app_set_model),
        zui_app_watch: load!(module, zui_app_watch),
        zui_app_unwatch: load!(module, zui_app_unwatch),
        zui_app_measure: load!(module, zui_app_measure),
        zui_app_add_class: load!(module, zui_app_add_class),
        zui_app_remove_class: load!(module, zui_app_remove_class),
        zui_app_defer: load!(module, zui_app_defer),
        zui_app_set_timeout: load!(module, zui_app_set_timeout),
        zui_app_set_interval: load!(module, zui_app_set_interval),
        zui_app_clear_timer: load!(module, zui_app_clear_timer),
        zui_app_run: load!(module, zui_app_run),
        zui_app_pump_frame: load!(module, zui_app_pump_frame),
        zui_app_quit: load!(module, zui_app_quit),
        zui_app_set_app_id: load!(module, zui_app_set_app_id),
        zui_app_user_data_dir: load!(module, zui_app_user_data_dir),
        zui_app_cache_dir: load!(module, zui_app_cache_dir),
        zui_app_pick_open: load!(module, zui_app_pick_open),
        zui_app_pick_save: load!(module, zui_app_pick_save),
        zui_app_pick_folder: load!(module, zui_app_pick_folder),
        zui_app_pick_open_async: load!(module, zui_app_pick_open_async),
        zui_app_pick_save_async: load!(module, zui_app_pick_save_async),
        zui_app_pick_folder_async: load!(module, zui_app_pick_folder_async),
        zui_pick_paths_free: load!(module, zui_pick_paths_free),
        zui_pick_path_free: load!(module, zui_pick_path_free),
        zui_app_clipboard_text: load!(module, zui_app_clipboard_text),
        zui_app_set_clipboard_text: load!(module, zui_app_set_clipboard_text),
        zui_app_clipboard_image: load!(module, zui_app_clipboard_image),
        zui_app_set_clipboard_image: load!(module, zui_app_set_clipboard_image),
        zui_image_free: load!(module, zui_image_free),
        zui_app_open_uri: load!(module, zui_app_open_uri),
        zui_app_scroll_into_view: load!(module, zui_app_scroll_into_view),
        zui_app_focus: load!(module, zui_app_focus),
        zui_app_blur: load!(module, zui_app_blur),
        zui_app_gamepad: load!(module, zui_app_gamepad),
        zui_app_gamepad_count: load!(module, zui_app_gamepad_count),
        zui_app_start_task: load!(module, zui_app_start_task),
        zui_app_cancel_task: load!(module, zui_app_cancel_task),
        zui_task_cancelled: load!(module, zui_task_cancelled),
        zui_task_set_progress: load!(module, zui_task_set_progress),
        zui_app_set_title: load!(module, zui_app_set_title),
        zui_app_set_window_size: load!(module, zui_app_set_window_size),
        zui_app_set_icon: load!(module, zui_app_set_icon),
        zui_app_minimize: load!(module, zui_app_minimize),
        zui_app_set_maximized: load!(module, zui_app_set_maximized),
        zui_app_set_fullscreen: load!(module, zui_app_set_fullscreen),
        zui_app_set_topmost: load!(module, zui_app_set_topmost),
        zui_app_minimized: load!(module, zui_app_minimized),
        zui_app_maximized: load!(module, zui_app_maximized),
        zui_app_fullscreen: load!(module, zui_app_fullscreen),
        zui_app_topmost: load!(module, zui_app_topmost),
        zui_app_window_visible: load!(module, zui_app_window_visible),
        zui_app_display: load!(module, zui_app_display),
        zui_app_open_window: load!(module, zui_app_open_window),
        zui_app_close_window: load!(module, zui_app_close_window),
        zui_app_enable_diagnostics: load!(module, zui_app_enable_diagnostics),
        zui_app_log_line: load!(module, zui_app_log_line),
        zui_event_prevent_default: load!(module, zui_event_prevent_default),
        zui_event_default_prevented: load!(module, zui_event_default_prevented),
        zui_event_data: load!(module, zui_event_data),
        zui_event_drag_data: load!(module, zui_event_drag_data),
    }
}

fn sym<T: Copy>(module: *mut c_void, name: &[u8]) -> T {
    assert_eq!(std::mem::size_of::<T>(), std::mem::size_of::<*mut c_void>());
    let ptr = get_proc(module, name.as_ptr() as *const c_char);
    if ptr.is_null() {
        let label = CStr::from_bytes_with_nul(name)
            .ok()
            .and_then(|text| text.to_str().ok())
            .unwrap_or("symbol");
        panic!("missing zui symbol {label}; the library does not match bindings/c/include/zui_capi.h");
    }
    unsafe { std::mem::transmute_copy(&ptr) }
}

fn library_path() -> PathBuf {
    let names: &[&str] = if cfg!(windows) {
        &["zui.dll"]
    } else if cfg!(target_os = "macos") {
        &["libzui.dylib"]
    } else {
        &["libzui.so"]
    };
    if let Some(dir) = std::env::var_os("ZUI_LIB_DIR") {
        return first_existing(&PathBuf::from(dir), names);
    }
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let build_root = manifest.join("../../../build");
    let builds = [
        "windows-x64",
        "windows-x64-release",
        "linux-x64",
        "linux-x64-release",
        "macos-arm64",
        "macos-x64",
    ];
    for build in builds {
        let dir = build_root.join(build);
        for name in names {
            let candidate = dir.join(name);
            if candidate.exists() {
                return candidate;
            }
        }
    }
    panic!("zui library not found; set ZUI_LIB_DIR to the directory that contains zui.dll");
}

fn first_existing(dir: &Path, names: &[&str]) -> PathBuf {
    for name in names {
        let candidate = dir.join(name);
        if candidate.exists() {
            return candidate;
        }
    }
    dir.join(names[0])
}

#[cfg(windows)]
fn open_library(path: &Path) -> *mut c_void {
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let handle = unsafe { LoadLibraryW(wide.as_ptr()) };
    if handle.is_null() {
        panic!("LoadLibraryW failed for {}", path.display());
    }
    handle
}

#[cfg(windows)]
fn get_proc(module: *mut c_void, name: *const c_char) -> *mut c_void {
    unsafe { GetProcAddress(module, name) }
}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn LoadLibraryW(name: *const u16) -> *mut c_void;
    fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *mut c_void;
}

#[cfg(unix)]
fn open_library(path: &Path) -> *mut c_void {
    use std::os::unix::ffi::OsStrExt;
    let mut bytes = path.as_os_str().as_bytes().to_vec();
    bytes.push(0);
    let handle = unsafe { dlopen(bytes.as_ptr() as *const c_char, RTLD_NOW) };
    if handle.is_null() {
        let message = unsafe { dlerror() };
        let text = if message.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned()
        };
        panic!("dlopen failed for {}: {text}", path.display());
    }
    handle
}

#[cfg(unix)]
fn get_proc(module: *mut c_void, name: *const c_char) -> *mut c_void {
    unsafe { dlsym(module, name) }
}

#[cfg(unix)]
const RTLD_NOW: c_int = 2;

#[cfg(all(unix, not(target_os = "macos")))]
#[link(name = "dl")]
unsafe extern "C" {
    fn dlopen(filename: *const c_char, flags: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    fn dlerror() -> *const c_char;
}

#[cfg(target_os = "macos")]
unsafe extern "C" {
    fn dlopen(filename: *const c_char, flags: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    fn dlerror() -> *const c_char;
}
