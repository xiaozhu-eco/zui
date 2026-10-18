"""Same document round-trip the C conformance test runs, through ctypes."""

import ctypes
import os
import tempfile

from zui import _abi
from zui import App
from zui import ListModel
from zui import TableModel
from zui import abi_version
from zui import layout

fails = 0


def check(cond, label):
    global fails
    if not cond:
        fails += 1
        print("FAIL", label)


HTML = """<!DOCTYPE html><html id="page"><body>
<h1 id="title" class="base">Title</h1>
<p id="n">n</p>
<ul id="host"><template><li id="{{id}}">{{text}}</li></template></ul>
<ul id="grid"><template><li id="{{name}}">{{qty}}</li></template></ul>
</body></html>
"""


class Rows(ListModel):
    def __init__(self):
        super().__init__()
        self.rows = [("a", "one"), ("b", "two")]

    def row_count(self):
        return len(self.rows)

    def value(self, row, key):
        if key == "id":
            return self.rows[row][0]
        if key == "text":
            return self.rows[row][1]
        return ""

    def row_key(self, row):
        return self.rows[row][0]


class Grid(TableModel):
    def row_count(self):
        return 1

    def column_count(self):
        return 2

    def column_key(self, column):
        return ("name", "qty")[column]

    def cell(self, row, column):
        return ("Ada", "4")[column]


def main():
    info = layout()
    check(abi_version() == 1, "abi version")
    check(info.abi_version == 1, "layout version")
    check(info.event == ctypes.sizeof(_abi.Event), "event size")
    check(info.key == ctypes.sizeof(_abi.Key), "key size")
    check(info.pointer == ctypes.sizeof(_abi.Pointer), "pointer size")
    check(info.wheel == ctypes.sizeof(_abi.Wheel), "wheel size")
    check(info.drag == ctypes.sizeof(_abi.Drag), "drag size")
    check(info.drop == ctypes.sizeof(_abi.Drop), "drop size")
    check(info.list_vtable == ctypes.sizeof(_abi.ListVtable), "list vtable")
    check(info.table_vtable == ctypes.sizeof(_abi.TableVtable), "table vtable")
    check(info.file_dialog == ctypes.sizeof(_abi.FileDialog), "file dialog")
    check(info.element_box == ctypes.sizeof(_abi.ElementBox), "element box")
    check(info.gamepad == ctypes.sizeof(_abi.Gamepad), "gamepad")
    check(info.display == ctypes.sizeof(_abi.Display), "display")
    check(info.pick_paths == ctypes.sizeof(_abi.PickPaths), "pick paths")
    check(info.image == ctypes.sizeof(_abi.Image), "image")

    fd, path = tempfile.mkstemp(suffix=".html")
    os.write(fd, HTML.encode("utf-8"))
    os.close(fd)
    try:
        with App() as app:
            check(not app.load("zui-capi-no-such-file.html"), "missing load")
            check(app.load(path), "load")
            check(not app.set("n", "style", "color:red"), "reject style")
            check(app.set("n", "text", "Ready"), "set text")
            check(app.get("n", "text") == "Ready", "get text")
            check(app.set("n", "data-kind", "row"), "set data")
            check(app.get("n", "data-kind") == "row", "get data")
            check(app.add_class("title", "error"), "add class")
            check("error" in app.get("title", "class"), "class has error")
            check(app.remove_class("title", "error"), "remove class")
            check("error" not in app.get("title", "class"), "class dropped error")
            check(not app.set_title(""), "empty title")
            check(app.set_window_size(800, 600), "window size")
            check(not app.set_window_size(0, 10), "bad window size")

            rows = Rows()
            check(app.set_model("host", rows), "set list")
            check(app.get("a", "text") == "one", "row a")
            hits = {"a": 0, "b": 0}

            def on_watch(element_id):
                hits[element_id] = hits.get(element_id, 0) + 1

            watch_a = app.watch("a", on_watch)
            check(watch_a != 0, "watch a")
            check(app.set_model("host", rows), "equal model")
            check(hits["a"] == 0, "equal model is quiet")
            rows.rows[0] = ("a", "one-b")
            check(app.set_model("host", rows), "changed model")
            check(hits["a"] == 1, "watch fired")
            check(app.get("a", "text") == "one-b", "updated text")
            app.unwatch(watch_a)
            rows.rows[0] = ("a", "one-c")
            check(app.set_model("host", rows), "after unwatch")
            check(hits["a"] == 1, "unwatch stuck")
            rows.close()

            grid = Grid()
            check(app.set_model("grid", grid), "set table")
            check(app.get("Ada", "text") == "4", "table cell")
            grid.close()

            status, _text = app.clipboard_text()
            check(status == _abi.CLIPBOARD_UNSUPPORTED, "clipboard")
            check(not app.open_uri("javascript:alert(1)"), "reject javascript")
            check(not app.focus("missing"), "focus missing")
            check(not app.minimize(), "minimize without window")
            check(not app.pump_frame(), "pump without run")
    finally:
        os.remove(path)

    if fails:
        print(fails, "check(s) failed")
        raise SystemExit(1)
    print("python capi conformance ok")


if __name__ == "__main__":
    main()
