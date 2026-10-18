//! Safe Rust bindings for zui.
//!
//! [`App`] is used only on the thread that created it. [`Event`] and any
//! strings borrowed from it are valid only for the handler call. A string with
//! an interior NUL is rejected and is not stripped. Library memory from pick
//! dialogs and clipboard images is not exposed here.

use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::os::raw::{c_char, c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

/// Scroll alignment passed to [`App::scroll_into_view`].
pub const SCROLL_START: i32 = 0;
/// Clipboard status when the host has no clipboard.
pub const CLIPBOARD_UNSUPPORTED: i32 = 1;

/// One document. Drop destroys it, then releases handlers.
pub struct App {
    raw: *mut zui_sys::zui_app,
    retired: Vec<Slot>,
    _thread: PhantomData<*const ()>,
}

/// Copy of the app pointer for a `'static` handler.
///
/// Valid on the creating thread until the [`App`] is dropped.
#[derive(Clone, Copy)]
pub struct Handle {
    raw: *mut zui_sys::zui_app,
    _thread: PhantomData<*const ()>,
}

/// One handler invocation. Do not store it.
pub struct Event {
    raw: *mut zui_sys::zui_event,
    _thread: PhantomData<*const ()>,
}

/// Rows rendered by a `<template>`. The engine reads it only during `set_model`.
pub trait ListModel {
    fn row_count(&self) -> usize;
    fn value(&self, row: usize, key: &str) -> String;
    fn row_key(&self, row: usize) -> String {
        let _ = row;
        String::new()
    }
    fn with_children(&self, row: usize, visit: &mut dyn FnMut(&dyn ListModel)) {
        let _ = (row, visit);
    }
}

/// Columns rendered by a `<template>`. `column_key` supplies `{{name}}`.
pub trait TableModel {
    fn row_count(&self) -> usize;
    fn column_count(&self) -> usize;
    fn column_key(&self, column: usize) -> String;
    fn cell(&self, row: usize, column: usize) -> String;
    fn row_key(&self, row: usize) -> String {
        let _ = row;
        String::new()
    }
}

struct Erased {
    data: usize,
    vtable: usize,
}

enum SlotKind {
    Handler,
    Watch,
}

struct Slot {
    kind: SlotKind,
    ptr: *mut c_void,
}

impl App {
    pub fn new() -> Option<App> {
        let raw = unsafe { (zui_sys::api().zui_app_create)(0, ptr::null_mut()) };
        if raw.is_null() {
            None
        } else {
            Some(App { raw, retired: Vec::new(), _thread: PhantomData })
        }
    }

    pub fn handle(&self) -> Handle {
        Handle { raw: self.raw, _thread: PhantomData }
    }

    pub fn close(&mut self) {
        if !self.raw.is_null() {
            unsafe { (zui_sys::api().zui_app_destroy)(self.raw) };
            self.raw = ptr::null_mut();
        }
        for slot in self.retired.drain(..) {
            free_slot(slot);
        }
    }

    pub fn load(&self, path: &str) -> bool {
        call_path(self.raw, path, |raw, text| unsafe { (zui_sys::api().zui_app_load)(raw, text) })
    }

    pub fn load_fragment(&self, path: &str, into_id: Option<&str>) -> bool {
        let Some(path) = cstr(path) else { return false };
        match into_id {
            None => flag(unsafe { (zui_sys::api().zui_app_load_fragment)(self.raw, path.as_ptr()) }),
            Some(id) => {
                let Some(id) = cstr(id) else { return false };
                flag(unsafe {
                    (zui_sys::api().zui_app_load_fragment_into)(self.raw, path.as_ptr(), id.as_ptr())
                })
            }
        }
    }

    pub fn replace_fragment(&self, path: &str, into_id: &str) -> bool {
        let Some(path) = cstr(path) else { return false };
        let Some(id) = cstr(into_id) else { return false };
        flag(unsafe {
            (zui_sys::api().zui_app_replace_fragment)(self.raw, path.as_ptr(), id.as_ptr())
        })
    }

    pub fn unload_fragment(&self, id: &str) -> bool {
        call_path(self.raw, id, |raw, text| unsafe {
            (zui_sys::api().zui_app_unload_fragment)(raw, text)
        })
    }

    pub fn set_theme(&self, name: &str) -> bool {
        call_path(self.raw, name, |raw, text| unsafe {
            (zui_sys::api().zui_app_set_theme)(raw, text)
        })
    }

    pub fn bind<F>(&mut self, name: &str, handler: F)
    where
        F: FnMut(&Event) + 'static,
    {
        let Some(name) = cstr(name) else { return };
        let boxed: Box<Box<dyn FnMut(&Event)>> = Box::new(Box::new(handler));
        let user = Box::into_raw(boxed) as *mut c_void;
        self.retired.push(Slot { kind: SlotKind::Handler, ptr: user });
        unsafe { (zui_sys::api().zui_app_bind)(self.raw, name.as_ptr(), Some(on_event), user) };
    }

    pub fn unbind(&self, name: &str) {
        let Some(name) = cstr(name) else { return };
        unsafe { (zui_sys::api().zui_app_unbind)(self.raw, name.as_ptr()) };
    }

    pub fn set(&self, id: &str, field: &str, value: &str) -> bool {
        set_raw(self.raw, id, field, value)
    }

    pub fn get(&self, id: &str, field: &str) -> String {
        get_raw(self.raw, id, field)
    }

    pub fn set_list_model(&self, id: &str, model: &dyn ListModel) -> bool {
        let Some(id) = cstr(id) else { return false };
        let erased = erase(model);
        let vt = list_vtable();
        let api = zui_sys::api();
        let handle = unsafe {
            (api.zui_list_model_create)(&vt, &erased as *const Erased as *mut c_void)
        };
        if handle.is_null() {
            return false;
        }
        let ok = flag(unsafe { (api.zui_app_set_model)(self.raw, id.as_ptr(), handle) });
        unsafe { (api.zui_list_model_destroy)(handle) };
        ok
    }

    pub fn set_table_model(&self, id: &str, model: &dyn TableModel) -> bool {
        let Some(id) = cstr(id) else { return false };
        let erased = erase(model);
        let vt = table_vtable();
        let api = zui_sys::api();
        let handle = unsafe {
            (api.zui_table_model_create)(&vt, &erased as *const Erased as *mut c_void)
        };
        if handle.is_null() {
            return false;
        }
        let ok = flag(unsafe { (api.zui_app_set_model)(self.raw, id.as_ptr(), handle) });
        unsafe { (api.zui_list_model_destroy)(handle) };
        ok
    }

    pub fn watch<F>(&mut self, id: &str, handler: F) -> u64
    where
        F: FnMut(&str) + 'static,
    {
        let Some(id) = cstr(id) else { return 0 };
        let boxed: Box<Box<dyn FnMut(&str)>> = Box::new(Box::new(handler));
        let user = Box::into_raw(boxed) as *mut c_void;
        self.retired.push(Slot { kind: SlotKind::Watch, ptr: user });
        unsafe { (zui_sys::api().zui_app_watch)(self.raw, id.as_ptr(), Some(on_watch), user) }
    }

    pub fn unwatch(&self, watch: u64) {
        unsafe { (zui_sys::api().zui_app_unwatch)(self.raw, watch) };
    }

    pub fn add_class(&self, id: &str, class_name: &str) -> bool {
        pair(self.raw, id, class_name, |raw, a, b| unsafe {
            (zui_sys::api().zui_app_add_class)(raw, a, b)
        })
    }

    pub fn remove_class(&self, id: &str, class_name: &str) -> bool {
        pair(self.raw, id, class_name, |raw, a, b| unsafe {
            (zui_sys::api().zui_app_remove_class)(raw, a, b)
        })
    }

    pub fn set_title(&self, title: &str) -> bool {
        call_path(self.raw, title, |raw, text| unsafe {
            (zui_sys::api().zui_app_set_title)(raw, text)
        })
    }

    pub fn set_window_size(&self, width: i32, height: i32) -> bool {
        flag(unsafe { (zui_sys::api().zui_app_set_window_size)(self.raw, width, height) })
    }

    pub fn run(&self) -> i32 {
        unsafe { (zui_sys::api().zui_app_run)(self.raw) }
    }

    pub fn pump_frame(&self) -> bool {
        flag(unsafe { (zui_sys::api().zui_app_pump_frame)(self.raw) })
    }

    pub fn quit(&self) {
        unsafe { (zui_sys::api().zui_app_quit)(self.raw) };
    }

    pub fn clipboard_text(&self) -> (i32, String) {
        let mut status = 0;
        let api = zui_sys::api();
        let n = unsafe { (api.zui_app_clipboard_text)(self.raw, &mut status, ptr::null_mut(), 0) };
        let text = read_text(n, |buf, cap| unsafe {
            (api.zui_app_clipboard_text)(self.raw, &mut status, buf, cap)
        });
        (status, text)
    }

    pub fn open_uri(&self, uri: &str) -> bool {
        call_path(self.raw, uri, |raw, text| unsafe { (zui_sys::api().zui_app_open_uri)(raw, text) })
    }

    pub fn focus(&self, id: &str) -> bool {
        call_path(self.raw, id, |raw, text| unsafe { (zui_sys::api().zui_app_focus)(raw, text) })
    }

    pub fn scroll_into_view(&self, id: &str, align: i32) -> bool {
        let Some(id) = cstr(id) else { return false };
        flag(unsafe { (zui_sys::api().zui_app_scroll_into_view)(self.raw, id.as_ptr(), align) })
    }

    pub fn minimize(&self) -> bool {
        flag(unsafe { (zui_sys::api().zui_app_minimize)(self.raw) })
    }
}

impl Drop for App {
    fn drop(&mut self) {
        self.close();
    }
}

impl Handle {
    pub fn set(self, id: &str, field: &str, value: &str) -> bool {
        set_raw(self.raw, id, field, value)
    }

    pub fn get(self, id: &str, field: &str) -> String {
        get_raw(self.raw, id, field)
    }

    pub fn add_class(self, id: &str, class_name: &str) -> bool {
        pair(self.raw, id, class_name, |raw, a, b| unsafe {
            (zui_sys::api().zui_app_add_class)(raw, a, b)
        })
    }

    pub fn remove_class(self, id: &str, class_name: &str) -> bool {
        pair(self.raw, id, class_name, |raw, a, b| unsafe {
            (zui_sys::api().zui_app_remove_class)(raw, a, b)
        })
    }
}

impl Event {
    pub fn event_type(&self) -> &str {
        field(self, |event| event.type_name)
    }

    pub fn name(&self) -> &str {
        field(self, |event| event.name)
    }

    pub fn source(&self) -> &str {
        field(self, |event| event.source)
    }

    pub fn value(&self) -> &str {
        field(self, |event| event.value)
    }

    pub fn data(&self, name: &str) -> String {
        let Some(name) = cstr(name) else { return String::new() };
        let api = zui_sys::api();
        let n = unsafe { (api.zui_event_data)(self.raw, name.as_ptr(), ptr::null_mut(), 0) };
        read_text(n, |buf, cap| unsafe { (api.zui_event_data)(self.raw, name.as_ptr(), buf, cap) })
    }

    pub fn prevent_default(&self) {
        unsafe { (zui_sys::api().zui_event_prevent_default)(self.raw) };
    }

    pub fn default_prevented(&self) -> bool {
        flag(unsafe { (zui_sys::api().zui_event_default_prevented)(self.raw) })
    }
}

fn field<'a>(event: &'a Event, pick: fn(&zui_sys::zui_event) -> *const c_char) -> &'a str {
    if event.raw.is_null() {
        return "";
    }
    cstr_ref(pick(unsafe { &*event.raw }))
}

extern "C" fn on_event(event: *mut zui_sys::zui_event, user: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if event.is_null() || user.is_null() {
            return;
        }
        let slot = unsafe { &mut *(user as *mut Box<dyn FnMut(&Event)>) };
        let view = Event { raw: event, _thread: PhantomData };
        slot(&view);
    }));
}

extern "C" fn on_watch(id: *const c_char, user: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if user.is_null() {
            return;
        }
        let slot = unsafe { &mut *(user as *mut Box<dyn FnMut(&str)>) };
        slot(cstr_ref(id));
    }));
}

fn erase<T: ?Sized>(model: &T) -> Erased {
    let ptr = ptr::from_ref(model);
    assert_eq!(std::mem::size_of_val(&ptr), std::mem::size_of::<[usize; 2]>());
    let parts: [usize; 2] = unsafe { std::mem::transmute_copy(&ptr) };
    Erased { data: parts[0], vtable: parts[1] }
}

fn list_of<'a>(user: *mut c_void) -> Option<&'a dyn ListModel> {
    unerased(user)
}

fn table_of<'a>(user: *mut c_void) -> Option<&'a dyn TableModel> {
    unerased(user)
}

fn unerased<'a, T: ?Sized + 'a>(user: *mut c_void) -> Option<&'a T> {
    if user.is_null() {
        return None;
    }
    let erased = unsafe { &*(user as *const Erased) };
    if erased.data == 0 {
        return None;
    }
    let parts = [erased.data, erased.vtable];
    let ptr: *const T = unsafe { std::mem::transmute_copy(&parts) };
    Some(unsafe { &*ptr })
}

extern "C" fn on_rows(user: *mut c_void) -> usize {
    catch_unwind(AssertUnwindSafe(|| list_of(user).map(|model| model.row_count()).unwrap_or(0)))
        .unwrap_or(0)
}

extern "C" fn on_value(
    user: *mut c_void,
    row: usize,
    key: *const c_char,
    key_len: usize,
    buf: *mut c_char,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        let text = match list_of(user) {
            Some(model) => model.value(row, &read_key(key, key_len)),
            None => String::new(),
        };
        write_buf(buf, cap, &text)
    }))
    .unwrap_or(0)
}

extern "C" fn on_row_key(user: *mut c_void, row: usize, buf: *mut c_char, cap: usize) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        let text = match list_of(user) {
            Some(model) => model.row_key(row),
            None => String::new(),
        };
        write_buf(buf, cap, &text)
    }))
    .unwrap_or(0)
}

extern "C" fn on_children(
    user: *mut c_void,
    row: usize,
    visit: zui_sys::zui_list_visit_fn,
    visit_user: *mut c_void,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Some(model) = list_of(user) else { return };
        model.with_children(row, &mut |child| {
            let Some(visit) = visit else { return };
            let child_erased = erase(child);
            let vt = list_vtable();
            let api = zui_sys::api();
            let handle = unsafe {
                (api.zui_list_model_create)(&vt, &child_erased as *const Erased as *mut c_void)
            };
            if !handle.is_null() {
                unsafe {
                    visit(handle, visit_user);
                    (api.zui_list_model_destroy)(handle);
                }
            }
        });
    }));
}

extern "C" fn on_table_rows(user: *mut c_void) -> usize {
    catch_unwind(AssertUnwindSafe(|| table_of(user).map(|model| model.row_count()).unwrap_or(0)))
        .unwrap_or(0)
}

extern "C" fn on_columns(user: *mut c_void) -> usize {
    catch_unwind(AssertUnwindSafe(|| table_of(user).map(|model| model.column_count()).unwrap_or(0)))
        .unwrap_or(0)
}

extern "C" fn on_column_key(user: *mut c_void, column: usize, buf: *mut c_char, cap: usize) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        let text = match table_of(user) {
            Some(model) => model.column_key(column),
            None => String::new(),
        };
        write_buf(buf, cap, &text)
    }))
    .unwrap_or(0)
}

extern "C" fn on_cell(
    user: *mut c_void,
    row: usize,
    column: usize,
    buf: *mut c_char,
    cap: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        let text = match table_of(user) {
            Some(model) => model.cell(row, column),
            None => String::new(),
        };
        write_buf(buf, cap, &text)
    }))
    .unwrap_or(0)
}

extern "C" fn on_table_key(user: *mut c_void, row: usize, buf: *mut c_char, cap: usize) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        let text = match table_of(user) {
            Some(model) => model.row_key(row),
            None => String::new(),
        };
        write_buf(buf, cap, &text)
    }))
    .unwrap_or(0)
}

fn list_vtable() -> zui_sys::zui_list_vtable {
    let mut vt: zui_sys::zui_list_vtable = unsafe { std::mem::zeroed() };
    vt.struct_size = std::mem::size_of::<zui_sys::zui_list_vtable>() as u32;
    vt.row_count = Some(on_rows);
    vt.value = Some(on_value);
    vt.row_key = Some(on_row_key);
    vt.with_children = Some(on_children);
    vt
}

fn table_vtable() -> zui_sys::zui_table_vtable {
    let mut vt: zui_sys::zui_table_vtable = unsafe { std::mem::zeroed() };
    vt.struct_size = std::mem::size_of::<zui_sys::zui_table_vtable>() as u32;
    vt.row_count = Some(on_table_rows);
    vt.column_count = Some(on_columns);
    vt.column_key = Some(on_column_key);
    vt.cell = Some(on_cell);
    vt.cell_value = None;
    vt.row_key = Some(on_table_key);
    vt
}

fn free_slot(slot: Slot) {
    unsafe {
        match slot.kind {
            SlotKind::Handler => {
                drop(Box::from_raw(slot.ptr as *mut Box<dyn FnMut(&Event)>));
            }
            SlotKind::Watch => {
                drop(Box::from_raw(slot.ptr as *mut Box<dyn FnMut(&str)>));
            }
        }
    }
}

fn cstr(text: &str) -> Option<CString> {
    CString::new(text).ok()
}

fn cstr_ref<'a>(text: *const c_char) -> &'a str {
    if text.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(text) }.to_str().unwrap_or("")
}

fn read_key(key: *const c_char, key_len: usize) -> String {
    if key.is_null() || key_len == 0 {
        return String::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(key as *const u8, key_len) };
    String::from_utf8_lossy(bytes).into_owned()
}

fn write_buf(buf: *mut c_char, cap: usize, text: &str) -> usize {
    let bytes = text.as_bytes();
    if !buf.is_null() && cap > 0 {
        let take = bytes.len().min(cap - 1);
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, take);
            *buf.add(take) = 0;
        }
    }
    bytes.len()
}

fn flag(value: c_int) -> bool {
    value != 0
}

fn call_path(raw: *mut zui_sys::zui_app, text: &str, call: impl FnOnce(*mut zui_sys::zui_app, *const c_char) -> c_int) -> bool {
    let Some(text) = cstr(text) else { return false };
    flag(call(raw, text.as_ptr()))
}

fn pair(
    raw: *mut zui_sys::zui_app,
    a: &str,
    b: &str,
    call: impl FnOnce(*mut zui_sys::zui_app, *const c_char, *const c_char) -> c_int,
) -> bool {
    let (Some(a), Some(b)) = (cstr(a), cstr(b)) else { return false };
    flag(call(raw, a.as_ptr(), b.as_ptr()))
}

fn set_raw(raw: *mut zui_sys::zui_app, id: &str, field: &str, value: &str) -> bool {
    let (Some(id), Some(field), Some(value)) = (cstr(id), cstr(field), cstr(value)) else {
        return false;
    };
    flag(unsafe { (zui_sys::api().zui_app_set)(raw, id.as_ptr(), field.as_ptr(), value.as_ptr()) })
}

fn get_raw(raw: *mut zui_sys::zui_app, id: &str, field: &str) -> String {
    let (Some(id), Some(field)) = (cstr(id), cstr(field)) else {
        return String::new();
    };
    let api = zui_sys::api();
    let n = unsafe { (api.zui_app_get)(raw, id.as_ptr(), field.as_ptr(), ptr::null_mut(), 0) };
    read_text(n, |buf, cap| unsafe { (api.zui_app_get)(raw, id.as_ptr(), field.as_ptr(), buf, cap) })
}

fn read_text(n: usize, fill: impl FnOnce(*mut c_char, usize) -> usize) -> String {
    const MAX: usize = 8 * 1024 * 1024;
    if n > MAX {
        return String::new();
    }
    let mut buf = vec![0u8; n + 1];
    let written = fill(buf.as_mut_ptr() as *mut c_char, buf.len());
    let end = written.min(n);
    String::from_utf8_lossy(&buf[..end]).into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;

    struct Rows {
        rows: Vec<(&'static str, &'static str)>,
    }

    impl ListModel for Rows {
        fn row_count(&self) -> usize {
            self.rows.len()
        }
        fn value(&self, row: usize, key: &str) -> String {
            let Some(item) = self.rows.get(row) else { return String::new() };
            match key {
                "id" => item.0.to_string(),
                "text" => item.1.to_string(),
                _ => String::new(),
            }
        }
        fn row_key(&self, row: usize) -> String {
            self.rows.get(row).map(|item| item.0.to_string()).unwrap_or_default()
        }
    }

    struct Grid;

    impl TableModel for Grid {
        fn row_count(&self) -> usize {
            1
        }
        fn column_count(&self) -> usize {
            2
        }
        fn column_key(&self, column: usize) -> String {
            ["name", "qty"].get(column).copied().unwrap_or("").to_string()
        }
        fn cell(&self, _row: usize, column: usize) -> String {
            ["Ada", "4"].get(column).copied().unwrap_or("").to_string()
        }
    }

    fn expect_size(got: u32, ty: usize, label: &str) {
        assert_eq!(got as usize, ty, "{label}");
    }

    #[test]
    fn conformance() {
        let api = zui_sys::api();
        let mut layout: zui_sys::zui_capi_layout = unsafe { std::mem::zeroed() };
        layout.struct_size = std::mem::size_of::<zui_sys::zui_capi_layout>() as u32;
        unsafe {
            (api.zui_capi_fill_layout)(&mut layout);
            assert_eq!((api.zui_capi_abi_version)(), 1);
        }
        assert_eq!(layout.abi_version, 1);
        expect_size(layout.event, std::mem::size_of::<zui_sys::zui_event>(), "event");
        expect_size(layout.key, std::mem::size_of::<zui_sys::zui_key>(), "key");
        expect_size(layout.pointer, std::mem::size_of::<zui_sys::zui_pointer>(), "pointer");
        expect_size(layout.wheel, std::mem::size_of::<zui_sys::zui_wheel>(), "wheel");
        expect_size(layout.drag, std::mem::size_of::<zui_sys::zui_drag>(), "drag");
        expect_size(layout.drop, std::mem::size_of::<zui_sys::zui_drop>(), "drop");
        expect_size(layout.list_vtable, std::mem::size_of::<zui_sys::zui_list_vtable>(), "list");
        expect_size(layout.table_vtable, std::mem::size_of::<zui_sys::zui_table_vtable>(), "table");
        expect_size(layout.file_dialog, std::mem::size_of::<zui_sys::zui_file_dialog>(), "dialog");
        expect_size(layout.file_filter, std::mem::size_of::<zui_sys::zui_file_filter>(), "filter");
        expect_size(layout.element_box, std::mem::size_of::<zui_sys::zui_element_box>(), "box");
        expect_size(layout.gamepad, std::mem::size_of::<zui_sys::zui_gamepad>(), "gamepad");
        expect_size(layout.display, std::mem::size_of::<zui_sys::zui_display>(), "display");
        expect_size(layout.pick_paths, std::mem::size_of::<zui_sys::zui_pick_paths>(), "paths");
        expect_size(layout.pick_path, std::mem::size_of::<zui_sys::zui_pick_path>(), "path");
        expect_size(layout.image, std::mem::size_of::<zui_sys::zui_image>(), "image");

        let html = r#"<!DOCTYPE html><html id="page"><body>
<h1 id="title" class="base">Title</h1>
<p id="n">n</p>
<ul id="host"><template><li id="{{id}}">{{text}}</li></template></ul>
<ul id="grid"><template><li id="{{name}}">{{qty}}</li></template></ul>
</body></html>
"#;
        let path = std::env::temp_dir().join(format!("zui-capi-rust-{}.html", std::process::id()));
        std::fs::write(&path, html).expect("temp html");
        let path_str = path.to_string_lossy().into_owned();

        let mut app = App::new().expect("create");
        assert!(!app.load("zui-capi-no-such-file.html"));
        assert!(app.load(&path_str));
        assert!(!app.set("n", "style", "color:red"));
        assert!(app.set("n", "text", "Ready"));
        assert_eq!(app.get("n", "text"), "Ready");
        assert!(app.set("n", "data-kind", "row"));
        assert_eq!(app.get("n", "data-kind"), "row");
        assert!(app.add_class("title", "error"));
        assert!(app.get("title", "class").contains("error"));
        assert!(app.remove_class("title", "error"));
        assert!(!app.get("title", "class").contains("error"));
        assert!(!app.set_title(""));
        assert!(app.set_window_size(800, 600));
        assert!(!app.set_window_size(0, 10));

        let mut rows = Rows { rows: vec![("a", "one"), ("b", "two")] };
        assert!(app.set_list_model("host", &rows));
        assert_eq!(app.get("a", "text"), "one");
        let hits = Rc::new(Cell::new(0u32));
        let hits_cb = hits.clone();
        let watch = app.watch("a", move |id| {
            if id == "a" {
                hits_cb.set(hits_cb.get() + 1);
            }
        });
        assert_ne!(watch, 0);
        assert!(app.set_list_model("host", &rows));
        assert_eq!(hits.get(), 0);
        rows.rows[0].1 = "one-b";
        assert!(app.set_list_model("host", &rows));
        assert_eq!(hits.get(), 1);
        assert_eq!(app.get("a", "text"), "one-b");
        app.unwatch(watch);
        rows.rows[0].1 = "one-c";
        assert!(app.set_list_model("host", &rows));
        assert_eq!(hits.get(), 1);

        assert!(app.set_table_model("grid", &Grid));
        assert_eq!(app.get("Ada", "text"), "4");

        let (status, _) = app.clipboard_text();
        assert_eq!(status, CLIPBOARD_UNSUPPORTED);
        assert!(!app.open_uri("javascript:alert(1)"));
        assert!(!app.focus("missing"));
        assert!(!app.minimize());
        assert!(!app.pump_frame());

        app.close();
        let _ = std::fs::remove_file(path);
    }
}
