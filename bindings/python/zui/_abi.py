"""ctypes mirror of bindings/c/include/zui_capi.h. Field order is the ABI."""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

def _library_path() -> Path:
    env = os.environ.get("ZUI_LIB_DIR")
    names = ("zui.dll",) if sys.platform == "win32" else ("libzui.so", "libzui.dylib")
    if env:
        root = Path(env)
        for name in names:
            candidate = root / name
            if candidate.exists():
                return candidate
        return root / names[0]
    repo = Path(__file__).resolve().parents[3]
    builds = (
        "windows-x64",
        "windows-x64-release",
        "linux-x64",
        "linux-x64-release",
        "macos-arm64",
        "macos-x64",
    )
    for build in builds:
        for name in names:
            candidate = repo / "build" / build / name
            if candidate.exists():
                return candidate
    raise FileNotFoundError("zui library not found; set ZUI_LIB_DIR to the build directory")


lib = ctypes.CDLL(str(_library_path()))


class Key(ctypes.Structure):
    _fields_ = [
        ("key", ctypes.c_char_p),
        ("ctrl", ctypes.c_int),
        ("shift", ctypes.c_int),
        ("alt", ctypes.c_int),
        ("meta", ctypes.c_int),
        ("composing", ctypes.c_int),
    ]


class Drop(ctypes.Structure):
    _fields_ = [("paths", ctypes.POINTER(ctypes.c_char_p)), ("count", ctypes.c_size_t)]


class Wheel(ctypes.Structure):
    _fields_ = [
        ("delta_x", ctypes.c_float),
        ("delta_y", ctypes.c_float),
        ("ctrl", ctypes.c_int),
        ("shift", ctypes.c_int),
        ("alt", ctypes.c_int),
        ("meta", ctypes.c_int),
    ]


class Pointer(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("button", ctypes.c_char_p),
        ("buttons", ctypes.c_int),
        ("count", ctypes.c_int),
        ("ctrl", ctypes.c_int),
        ("shift", ctypes.c_int),
        ("alt", ctypes.c_int),
        ("meta", ctypes.c_int),
        ("pointer_id", ctypes.c_int),
        ("pointer_type", ctypes.c_int),
        ("is_primary", ctypes.c_int),
        ("pressure", ctypes.c_float),
    ]


class Drag(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("source_id", ctypes.c_char_p),
        ("text", ctypes.c_char_p),
        ("uri", ctypes.c_char_p),
        ("effect", ctypes.c_int),
        ("allowed", ctypes.c_int),
    ]


class Event(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("struct_size", ctypes.c_uint32),
        ("_impl", ctypes.c_void_p),
        ("type", ctypes.c_char_p),
        ("name", ctypes.c_char_p),
        ("source", ctypes.c_char_p),
        ("value", ctypes.c_char_p),
        ("key", ctypes.POINTER(Key)),
        ("drop", ctypes.POINTER(Drop)),
        ("wheel", ctypes.POINTER(Wheel)),
        ("pointer", ctypes.POINTER(Pointer)),
        ("drag", ctypes.POINTER(Drag)),
    ]


class FileFilter(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("label", ctypes.c_char_p),
        ("patterns", ctypes.c_char_p),
    ]


class FileDialog(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("title", ctypes.c_char_p),
        ("default_name", ctypes.c_char_p),
        ("filters", ctypes.POINTER(FileFilter)),
        ("filter_count", ctypes.c_size_t),
        ("multi", ctypes.c_int),
    ]


class Rect(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("width", ctypes.c_float),
        ("height", ctypes.c_float),
    ]


class ElementBox(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("laid", ctypes.c_int),
        ("border", Rect),
        ("content_width", ctypes.c_float),
        ("content_height", ctypes.c_float),
        ("client_width", ctypes.c_float),
        ("client_height", ctypes.c_float),
        ("scroll_width", ctypes.c_float),
        ("scroll_height", ctypes.c_float),
    ]


class GamepadButton(ctypes.Structure):
    _fields_ = [("pressed", ctypes.c_int), ("value", ctypes.c_float)]


class Gamepad(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("connected", ctypes.c_int),
        ("id", ctypes.c_char * 256),
        ("axes", ctypes.c_float * 4),
        ("buttons", GamepadButton * 17),
    ]


class Display(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("known", ctypes.c_int),
        ("id", ctypes.c_char * 256),
        ("index", ctypes.c_int),
        ("width_px", ctypes.c_int),
        ("height_px", ctypes.c_int),
        ("scale", ctypes.c_float),
    ]


class PickPaths(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("count", ctypes.c_size_t),
        ("paths", ctypes.POINTER(ctypes.c_char_p)),
    ]


class PickPath(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("path", ctypes.c_char_p)]


class Image(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("rgba", ctypes.POINTER(ctypes.c_uint8)),
        ("bytes", ctypes.c_size_t),
    ]


class Layout(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("file_dialog", ctypes.c_uint32),
        ("file_filter", ctypes.c_uint32),
        ("element_box", ctypes.c_uint32),
        ("gamepad", ctypes.c_uint32),
        ("display", ctypes.c_uint32),
        ("list_vtable", ctypes.c_uint32),
        ("table_vtable", ctypes.c_uint32),
        ("event", ctypes.c_uint32),
        ("key", ctypes.c_uint32),
        ("pointer", ctypes.c_uint32),
        ("wheel", ctypes.c_uint32),
        ("drag", ctypes.c_uint32),
        ("drop", ctypes.c_uint32),
        ("pick_paths", ctypes.c_uint32),
        ("pick_path", ctypes.c_uint32),
        ("image", ctypes.c_uint32),
    ]


RowCount = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)
ValueFn = ctypes.CFUNCTYPE(
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
)
RowKeyFn = ctypes.CFUNCTYPE(
    ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t
)
VisitFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)
WithChildrenFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_size_t, VisitFn, ctypes.c_void_p)
ColumnCountFn = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)
ColumnKeyFn = ctypes.CFUNCTYPE(
    ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t
)
CellFn = ctypes.CFUNCTYPE(
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
)
CellValueFn = ctypes.CFUNCTYPE(
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_size_t,
)
Handler = ctypes.CFUNCTYPE(None, ctypes.POINTER(Event), ctypes.c_void_p)
VoidFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
WatchFn = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_void_p)


class ListVtable(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("row_count", RowCount),
        ("value", ValueFn),
        ("row_key", RowKeyFn),
        ("with_children", WithChildrenFn),
    ]


class TableVtable(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("row_count", RowCount),
        ("column_count", ColumnCountFn),
        ("column_key", ColumnKeyFn),
        ("cell", CellFn),
        ("cell_value", CellValueFn),
        ("row_key", RowKeyFn),
    ]


def _sig(name: str, restype, argtypes) -> None:
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = argtypes


_vp = ctypes.c_void_p
_sz = ctypes.c_size_t
_i = ctypes.c_int
_u64 = ctypes.c_uint64
_cp = ctypes.c_char_p

_sig("zui_capi_abi_version", ctypes.c_uint32, [])
_sig("zui_capi_fill_layout", None, [ctypes.POINTER(Layout)])
_sig("zui_app_create", _vp, [_i, _vp])
_sig("zui_app_destroy", None, [_vp])
_sig("zui_app_load", _i, [_vp, _cp])
_sig("zui_app_load_fragment", _i, [_vp, _cp])
_sig("zui_app_load_fragment_into", _i, [_vp, _cp, _cp])
_sig("zui_app_replace_fragment", _i, [_vp, _cp, _cp])
_sig("zui_app_unload_fragment", _i, [_vp, _cp])
_sig("zui_app_set_theme", _i, [_vp, _cp])
_sig("zui_app_bind", None, [_vp, _cp, Handler, _vp])
_sig("zui_app_unbind", None, [_vp, _cp])
_sig("zui_app_set", _i, [_vp, _cp, _cp, _cp])
_sig("zui_app_get", _sz, [_vp, _cp, _cp, _cp, _sz])
_sig("zui_list_model_create", _vp, [ctypes.POINTER(ListVtable), _vp])
_sig("zui_table_model_create", _vp, [ctypes.POINTER(TableVtable), _vp])
_sig("zui_list_model_destroy", None, [_vp])
_sig("zui_app_set_model", _i, [_vp, _cp, _vp])
_sig("zui_app_watch", _u64, [_vp, _cp, WatchFn, _vp])
_sig("zui_app_unwatch", None, [_vp, _u64])
_sig("zui_app_add_class", _i, [_vp, _cp, _cp])
_sig("zui_app_remove_class", _i, [_vp, _cp, _cp])
_sig("zui_app_set_title", _i, [_vp, _cp])
_sig("zui_app_set_window_size", _i, [_vp, _i, _i])
_sig("zui_app_run", _i, [_vp])
_sig("zui_app_pump_frame", _i, [_vp])
_sig("zui_app_quit", None, [_vp])
_sig("zui_app_clipboard_text", _sz, [_vp, ctypes.POINTER(_i), _cp, _sz])
_sig("zui_app_set_clipboard_text", _i, [_vp, _cp])
_sig("zui_app_open_uri", _i, [_vp, _cp])
_sig("zui_app_focus", _i, [_vp, _cp])
_sig("zui_app_scroll_into_view", _i, [_vp, _cp, _i])
_sig("zui_app_minimize", _i, [_vp])
_sig("zui_app_gamepad", None, [_vp, _sz, ctypes.POINTER(Gamepad)])
_sig("zui_app_display", None, [_vp, ctypes.POINTER(Display)])
_sig("zui_event_prevent_default", None, [ctypes.POINTER(Event)])
_sig("zui_event_default_prevented", _i, [ctypes.POINTER(Event)])
_sig("zui_event_data", _sz, [ctypes.POINTER(Event), _cp, _cp, _sz])

ABI_VERSION = 1
PICK_UNSUPPORTED = 2
CLIPBOARD_UNSUPPORTED = 1
SCROLL_START = 0
