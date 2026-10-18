"""Python bindings over the zui C ABI.

The app object is used from the thread that created it. Event, and any strings
borrowed from it, are valid only inside the handler that received them.
"""

from __future__ import annotations

import ctypes
import traceback

from . import _abi

_OBJS: dict[int, object] = {}


def _keep(obj: object) -> int:
    key = id(obj)
    _OBJS[key] = obj
    return key


def _drop(key: int) -> None:
    _OBJS.pop(key, None)


def _live(user) -> object:
    if user is None:
        raise KeyError("null model user")
    key = user if isinstance(user, int) else int(user)
    return _OBJS[key]


def _b(text: str | None) -> bytes | None:
    if text is None:
        return None
    return text.encode("utf-8")


def _write(buf, cap: int, text: str) -> int:
    data = text.encode("utf-8")
    n = len(data)
    if buf and cap > 0:
        take = n if n < cap else cap - 1
        if take:
            ctypes.memmove(buf, data, take)
        ctypes.memset(buf + take, 0, 1)
    return n


def _read_key(ptr, length: int) -> str:
    if not ptr or not length:
        return ""
    return ctypes.string_at(ptr, length).decode("utf-8", "replace")


def _text(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return str(value)


def layout() -> _abi.Layout:
    out = _abi.Layout()
    out.struct_size = ctypes.sizeof(_abi.Layout)
    _abi.lib.zui_capi_fill_layout(ctypes.byref(out))
    return out


def abi_version() -> int:
    return int(_abi.lib.zui_capi_abi_version())


class Event:
    """One handler invocation. Do not store this object."""

    def __init__(self, pointer) -> None:
        self._pointer = pointer
        self._event = pointer.contents

    @property
    def type(self) -> str:
        return _text(self._event.type)

    @property
    def name(self) -> str:
        return _text(self._event.name)

    @property
    def source(self) -> str:
        return _text(self._event.source)

    @property
    def value(self) -> str:
        return _text(self._event.value)

    def data(self, name: str) -> str:
        n = _abi.lib.zui_event_data(self._pointer, _b(name), None, 0)
        buf = ctypes.create_string_buffer(n + 1)
        _abi.lib.zui_event_data(self._pointer, _b(name), buf, n + 1)
        return buf.value.decode("utf-8", "replace")

    def prevent_default(self) -> None:
        _abi.lib.zui_event_prevent_default(self._pointer)

    @property
    def default_prevented(self) -> bool:
        return bool(_abi.lib.zui_event_default_prevented(self._pointer))


class ListModel:
    """Subclass and implement row_count, value, and optionally row_key."""

    def row_count(self) -> int:
        raise NotImplementedError

    def value(self, row: int, key: str) -> str:
        raise NotImplementedError

    def row_key(self, row: int) -> str:
        return ""

    def with_children(self, row: int, visit) -> None:
        return None

    def __init__(self) -> None:
        self._key = _keep(self)
        self._handle = None
        self._row_count_cb = _abi.RowCount(self._on_rows)
        self._value_cb = _abi.ValueFn(self._on_value)
        self._row_key_cb = _abi.RowKeyFn(self._on_key)
        self._children_cb = _abi.WithChildrenFn(self._on_children)
        self._vt = _abi.ListVtable()
        self._vt.struct_size = ctypes.sizeof(_abi.ListVtable)
        self._vt.row_count = self._row_count_cb
        self._vt.value = self._value_cb
        self._vt.row_key = self._row_key_cb
        self._vt.with_children = self._children_cb

    def _ensure(self) -> None:
        if self._handle:
            return
        self._handle = _abi.lib.zui_list_model_create(ctypes.byref(self._vt), self._key)
        if not self._handle:
            raise RuntimeError("zui_list_model_create failed")

    def close(self) -> None:
        if self._handle:
            _abi.lib.zui_list_model_destroy(self._handle)
            self._handle = None
        _drop(self._key)

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            return

    def _on_rows(self, user) -> int:
        try:
            return int(_live(user).row_count())
        except Exception:
            traceback.print_exc()
            return 0

    def _on_value(self, user, row, key_ptr, key_len, buf, cap) -> int:
        try:
            model = _live(user)
            return _write(buf, cap, model.value(row, _read_key(key_ptr, key_len)))
        except Exception:
            traceback.print_exc()
            return 0

    def _on_key(self, user, row, buf, cap) -> int:
        try:
            return _write(buf, cap, _live(user).row_key(row))
        except Exception:
            traceback.print_exc()
            return 0

    def _on_children(self, user, row, visit, visit_user) -> None:
        try:
            def accept(child: ListModel) -> None:
                child._ensure()
                if visit:
                    visit(child._handle, visit_user)

            _live(user).with_children(row, accept)
        except Exception:
            traceback.print_exc()


class TableModel:
    """Subclass and implement row_count, column_count, column_key, and cell."""

    def row_count(self) -> int:
        raise NotImplementedError

    def column_count(self) -> int:
        raise NotImplementedError

    def column_key(self, column: int) -> str:
        raise NotImplementedError

    def cell(self, row: int, column: int) -> str:
        raise NotImplementedError

    def row_key(self, row: int) -> str:
        return ""

    def __init__(self) -> None:
        self._key = _keep(self)
        self._handle = None
        self._row_count_cb = _abi.RowCount(self._on_rows)
        self._column_count_cb = _abi.ColumnCountFn(self._on_cols)
        self._column_key_cb = _abi.ColumnKeyFn(self._on_col_key)
        self._cell_cb = _abi.CellFn(self._on_cell)
        self._row_key_cb = _abi.RowKeyFn(self._on_key)
        self._vt = _abi.TableVtable()
        self._vt.struct_size = ctypes.sizeof(_abi.TableVtable)
        self._vt.row_count = self._row_count_cb
        self._vt.column_count = self._column_count_cb
        self._vt.column_key = self._column_key_cb
        self._vt.cell = self._cell_cb
        self._vt.row_key = self._row_key_cb

    def _ensure(self) -> None:
        if self._handle:
            return
        self._handle = _abi.lib.zui_table_model_create(ctypes.byref(self._vt), self._key)
        if not self._handle:
            raise RuntimeError("zui_table_model_create failed")

    def close(self) -> None:
        if self._handle:
            _abi.lib.zui_list_model_destroy(self._handle)
            self._handle = None
        _drop(self._key)

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            return

    def _on_rows(self, user) -> int:
        try:
            return int(_live(user).row_count())
        except Exception:
            traceback.print_exc()
            return 0

    def _on_cols(self, user) -> int:
        try:
            return int(_live(user).column_count())
        except Exception:
            traceback.print_exc()
            return 0

    def _on_col_key(self, user, column, buf, cap) -> int:
        try:
            return _write(buf, cap, _live(user).column_key(column))
        except Exception:
            traceback.print_exc()
            return 0

    def _on_cell(self, user, row, column, buf, cap) -> int:
        try:
            return _write(buf, cap, _live(user).cell(row, column))
        except Exception:
            traceback.print_exc()
            return 0

    def _on_key(self, user, row, buf, cap) -> int:
        try:
            return _write(buf, cap, _live(user).row_key(row))
        except Exception:
            traceback.print_exc()
            return 0


class App:
    """One zui document. Not safe to share across threads."""

    def __init__(self) -> None:
        self._callbacks: list[object] = []
        self._handle = _abi.lib.zui_app_create(0, None)
        if not self._handle:
            raise RuntimeError("zui_app_create failed")

    def close(self) -> None:
        if self._handle:
            _abi.lib.zui_app_destroy(self._handle)
            self._handle = None
        self._callbacks.clear()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            return

    def __enter__(self) -> App:
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def load(self, path: str) -> bool:
        return bool(_abi.lib.zui_app_load(self._handle, _b(path)))

    def load_fragment(self, path: str, into_id: str | None = None) -> bool:
        if into_id is None:
            return bool(_abi.lib.zui_app_load_fragment(self._handle, _b(path)))
        return bool(_abi.lib.zui_app_load_fragment_into(self._handle, _b(path), _b(into_id)))

    def replace_fragment(self, path: str, into_id: str) -> bool:
        return bool(_abi.lib.zui_app_replace_fragment(self._handle, _b(path), _b(into_id)))

    def unload_fragment(self, element_id: str) -> bool:
        return bool(_abi.lib.zui_app_unload_fragment(self._handle, _b(element_id)))

    def set_theme(self, name: str) -> bool:
        return bool(_abi.lib.zui_app_set_theme(self._handle, _b(name)))

    def bind(self, name: str, handler) -> None:
        def tramp(event, _user) -> None:
            try:
                handler(Event(event))
            except Exception:
                traceback.print_exc()

        callback = _abi.Handler(tramp)
        self._callbacks.append(callback)
        _abi.lib.zui_app_bind(self._handle, _b(name), callback, None)

    def unbind(self, name: str) -> None:
        _abi.lib.zui_app_unbind(self._handle, _b(name))

    def set(self, element_id: str, field: str, value: str) -> bool:
        return bool(_abi.lib.zui_app_set(self._handle, _b(element_id), _b(field), _b(value)))

    def get(self, element_id: str, field: str) -> str:
        element = _b(element_id)
        name = _b(field)
        n = _abi.lib.zui_app_get(self._handle, element, name, None, 0)
        buf = ctypes.create_string_buffer(n + 1)
        _abi.lib.zui_app_get(self._handle, element, name, buf, len(buf))
        return buf.value.decode("utf-8", "replace")

    def set_model(self, element_id: str, model: ListModel | TableModel) -> bool:
        model._ensure()
        return bool(_abi.lib.zui_app_set_model(self._handle, _b(element_id), model._handle))

    def watch(self, element_id: str, handler) -> int:
        def tramp(watched, _user) -> None:
            try:
                handler(_text(watched))
            except Exception:
                traceback.print_exc()

        callback = _abi.WatchFn(tramp)
        self._callbacks.append(callback)
        return int(_abi.lib.zui_app_watch(self._handle, _b(element_id), callback, None))

    def unwatch(self, watch: int) -> None:
        _abi.lib.zui_app_unwatch(self._handle, watch)

    def add_class(self, element_id: str, class_name: str) -> bool:
        return bool(_abi.lib.zui_app_add_class(self._handle, _b(element_id), _b(class_name)))

    def remove_class(self, element_id: str, class_name: str) -> bool:
        return bool(_abi.lib.zui_app_remove_class(self._handle, _b(element_id), _b(class_name)))

    def set_title(self, title: str) -> bool:
        return bool(_abi.lib.zui_app_set_title(self._handle, _b(title)))

    def set_window_size(self, width: int, height: int) -> bool:
        return bool(_abi.lib.zui_app_set_window_size(self._handle, width, height))

    def run(self) -> int:
        return int(_abi.lib.zui_app_run(self._handle))

    def pump_frame(self) -> bool:
        return bool(_abi.lib.zui_app_pump_frame(self._handle))

    def quit(self) -> None:
        _abi.lib.zui_app_quit(self._handle)

    def clipboard_text(self) -> tuple[int, str]:
        status = ctypes.c_int()
        n = _abi.lib.zui_app_clipboard_text(self._handle, ctypes.byref(status), None, 0)
        buf = ctypes.create_string_buffer(n + 1)
        _abi.lib.zui_app_clipboard_text(self._handle, ctypes.byref(status), buf, len(buf))
        return int(status.value), buf.value.decode("utf-8", "replace")

    def open_uri(self, uri: str) -> bool:
        return bool(_abi.lib.zui_app_open_uri(self._handle, _b(uri)))

    def focus(self, element_id: str) -> bool:
        return bool(_abi.lib.zui_app_focus(self._handle, _b(element_id)))

    def scroll_into_view(self, element_id: str, align: int = _abi.SCROLL_START) -> bool:
        return bool(_abi.lib.zui_app_scroll_into_view(self._handle, _b(element_id), align))

    def minimize(self) -> bool:
        return bool(_abi.lib.zui_app_minimize(self._handle))
