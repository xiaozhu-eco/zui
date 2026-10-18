"""Regenerate the example application cases (tests/difftest/cases/example-*).

Each case is the app's static shell (`scripts: false` -> the harness strips the page's own script
and both ends load the same generated page.html) plus a small action script:

  1. shell at 1100x720;
  2. a content state built from the app's own template classes (insert) and its own state hooks
     (set_checked / remove_attr) - the engine runs no JavaScript, so the state is the same on both
     ends;
  3. an interaction state: hover + press on the app's primary top-bar button (coordinates measured
     with `zui_dump --brief` on the engine's own shell, then verified against the browser's
     recorded `hit` path), a wheel over a scroll container that actually overflows, and Tab/Escape;
  4. an overlay state: the `hidden` attribute is removed, which is the eraser half `set_attr`
     cannot express, and the page's dialog layout is compared for the first time.

Re-run: `python tests/difftest/tools/mkcases.py` then
`python tests/difftest/run.py record --case example-album --case example-calendar --case example-chat
--case example-contacts --case example-file-explorer --case example-kanban --case example-zdb`.
"""

import json
from pathlib import Path

CASES = Path(__file__).resolve().parents[1] / "cases"

STYLE_PROPS = [
    "display", "position", "box-sizing", "width", "height", "min-width", "min-height",
    "max-width", "max-height",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
    "overflow-x", "overflow-y", "visibility", "z-index",
    "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis",
    "justify-content", "align-items", "align-content", "align-self", "order",
    "row-gap", "column-gap",
    "font-family", "font-size", "font-weight", "font-style", "line-height",
    "letter-spacing", "word-spacing", "text-align", "text-indent", "text-overflow",
    "white-space", "word-break", "overflow-wrap", "vertical-align",
]
# `props.json` declares the grid templates, the border styles, colours, radii, shadows and
# `transform` / `opacity` as `deferred_props`: the two ends do not model them the same way yet, so
# they are outside the "aligned" range by the test plan's own scope. Comparing them here would add
# thousands of differences that the plan has already declared out of scope, and nothing else:
# grid layout itself is covered by the geometry and pixel layers, and by the dedicated grid cases.


def insert(target, text, tag="button", cls="", element_id=""):
    action = {"do": "insert", "target": target, "tag": tag, "text": text}
    if cls:
        action["class"] = cls
    if element_id:
        action["id"] = element_id
    return action


def set_text(target, text):
    return {"do": "set_text", "target": target, "text": text}


def remove_attr(target, name):
    return {"do": "remove_attr", "target": target, "name": name}


def set_attr(target, name, value=""):
    return {"do": "set_attr", "target": target, "name": name, "value": value}


def set_checked(target, value=True):
    return {"do": "set_checked", "target": target, "value": value}


def build(name, source, comment, states, press, wheel, overlay_id, overlay_actions,
          extra_keys=True):
    """Assemble one case.

    states: list of action lists (each becomes an `eval` + trace + capture).
    press:  (x, y) of the app's primary top-bar button, or None.
    wheel:  (x, y, delta_x, delta_y) of a point inside a scroll container, or None.
    """
    steps = [{"op": "load"}, {"op": "resize", "w": 1100, "h": 720}]
    for actions in states:
        if actions:
            steps.append({"op": "eval", "actions": actions})
        steps.append({"op": "trace"})
        steps.append({"op": "capture"})

    events = []
    if press is not None:
        events += [
            {"type": "mouseMoved", "x": press[0], "y": press[1]},
            {"type": "mousePressed", "x": press[0], "y": press[1], "button": "left", "count": 1},
            {"type": "mouseReleased", "x": press[0], "y": press[1], "button": "left", "count": 1},
        ]
    if wheel is not None:
        events.append({"type": "mouseWheel", "x": wheel[0], "y": wheel[1],
                       "deltaX": wheel[2], "deltaY": wheel[3]})
    if extra_keys:
        events += [
            {"type": "keyDown", "key": "Tab"}, {"type": "keyUp", "key": "Tab"},
            {"type": "keyDown", "key": "Escape"}, {"type": "keyUp", "key": "Escape"},
        ]
    if events:
        step = {"op": "dispatch", "events": events}
        if press is not None:
            step["hits"] = [{"x": press[0], "y": press[1]}]
        if wheel is not None:
            step.setdefault("hits", []).append({"x": wheel[0], "y": wheel[1]})
        steps += [step, {"op": "trace"}, {"op": "capture"}]

    if overlay_actions:
        steps.append({"op": "eval", "actions": overlay_actions})
        steps.append({"op": "freeze", "t": 400})
        steps += [{"op": "trace"}, {"op": "capture"}]

    return {
        "name": name,
        "page": "page.html",
        "source": source,
        "viewport": [1100, 720],
        "scripts": False,
        "_comment": comment,
        "matrix": [],
        "required": False,
        "steps": steps,
        "expect": "golden/",
        "pixels": "strict",
        "style_props": STYLE_PROPS,
        "text": {"breaks": True, "advances": False},
    }


APPS = []

# ---------------------------------------------------------------- album
album_albums = ["海边", "山径", "旧城", "冬港", "春溪", "夏夜", "秋原", "灯下", "渡口", "旧书",
                "远山", "长街", "雨巷", "晨雾", "砂丘", "浮岛", "礁石", "松林", "河谷", "巷口"]
album_photos = ["海边 %02d" % index for index in range(1, 13)]
APPS.append(build(
    "example-album",
    "../../../../example/applications/album/index.html",
    "小竹相册去掉页面脚本后的静态壳。空态提示、铺满相册栏与网格、顶部按钮 hover/按下"
    "（命中与焦点读回）、相册栏滚轮、Tab/Escape，最后撤掉 #lightbox 的 hidden 看灯箱版式。",
    [
        [set_text("#album-title", "海边"), remove_attr("#empty", "hidden")],
        [set_attr("#empty", "hidden")]
        + [insert("#albums", name, cls="album-btn on" if index == 0 else "album-btn")
           for index, name in enumerate(album_albums)]
        + [insert("#grid", name, cls="tile") for name in album_photos],
    ],
    press=(951, 27), wheel=(110, 300, 0, 240), overlay_id="#lightbox",
    overlay_actions=[remove_attr("#lightbox", "hidden")],
))

# ---------------------------------------------------------------- calendar
APPS.append(build(
    "example-calendar",
    "../../../../example/applications/calendar/index.html",
    "小竹日历去掉页面脚本后的静态壳。空态提示、日程项与时间块、顶部「新建」的 hover/按下"
    "、Tab/Escape，最后撤掉 #editor 的 hidden。",
    [
        [set_text("#title", "2026 年 10 月"), remove_attr("#agenda-empty", "hidden")],
        [set_attr("#agenda-empty", "hidden")]
        + [insert("#agenda-list", text, cls="agenda-item tone-gold")
           for text in ("09:30 站会", "14:00 评审", "17:30 交接")]
        + [insert("#day-%d" % day, text, cls="block tone-sea")
           for day, text in enumerate(("站会", "评审", "交接"))],
    ],
    press=(1045, 28), wheel=None, overlay_id="#editor",
    overlay_actions=[remove_attr("#editor", "hidden"), set_text("#editor-title", "新建日程")],
))

# ---------------------------------------------------------------- chat
APPS.append(build(
    "example-chat",
    "../../../../example/applications/chat/index.html",
    "小竹聊天去掉页面脚本后的静态壳。会话条目与两种气泡、顶部「新建」的 hover/按下"
    "、消息流滚轮、Tab/Escape，最后撤掉 #creator 的 hidden。",
    [
        [set_text("#thread-name", "设计组")]
        + [insert("#conv-list", text, cls="conv on" if index == 0 else "conv")
           for index, text in enumerate(("设计组 09:12", "后端 昨天", "产品 周一", "运维 上周",
                                         "茶水间 上周", "读书会 上月"))],
        [insert("#log", text, cls="msg mine" if index % 2 else "msg")
         for index, text in enumerate((
             "侧栏的滚动条现在是按内容撑开的。",
             "收到，我看一下断行。",
             "长句在 299px 宽的时候会提前折行，Chrome 也是这样。",
             "好，那就不动它。",
             "换行测试：这是一条足够长的消息，用来把消息流撑出滚动区域，两端应该给出同样的滚动高度。",
             "再补一条，凑够溢出。",
             "还有一条。",
             "最后一条。",
             "补一轮，让消息流真的溢出，看看滚动条出现后气泡宽度会不会跟着收窄。",
             "同一句话在两端应该折在同一处，字距和间距都不该差。",
             "好。",
             "还有，输入条的高度是跟 textarea 的行数走的。",
             "嗯，这条之后应该就到滚动区底部了。",
             "收工。",
             "再补两条，确保溢出量足够大。",
             "好了。"))],
    ],
    press=(1045, 27), wheel=(700, 400, 0, 160), overlay_id="#creator",
    overlay_actions=[remove_attr("#creator", "hidden")],
))

# ---------------------------------------------------------------- contacts
contacts_names = ["沈青禾 设计师", "林岸 后端", "周未 产品", "许一 运维", "陈默 前端", "苏晚 测试",
                  "何宁 数据", "于归 运营", "白露 法务", "郑南 商务", "秦朗 硬件", "吴桐 编辑",
                  "卓然 客服", "柳岸 摄影", "范青 声乐", "康乐 行政", "钟原 财务", "桑榆 采购",
                  "孔嘉 培训", "聂川 后勤", "关月 编辑", "黎夏 设计"]
APPS.append(build(
    "example-contacts",
    "../../../../example/applications/contacts/index.html",
    "小竹通讯录去掉页面脚本后的静态壳。名单条目、只显示星标（:checked 兄弟选择器）"
    "、顶部「新建」的 hover/按下、名单滚轮、Tab/Escape，最后撤掉 #editor 的 hidden。",
    [
        [set_text("#d-name", "沈青禾")]
        + [insert("#list", name, cls="person") for name in contacts_names],
        [set_checked("#star-only", True)],
    ],
    press=(1060, 29), wheel=(160, 400, 0, 200), overlay_id="#editor",
    overlay_actions=[remove_attr("#editor", "hidden")],
))

# ---------------------------------------------------------------- file-explorer
APPS.append(build(
    "example-file-explorer",
    "../../../../example/applications/file-explorer/index.html",
    "小竹文件去掉页面脚本后的静态壳。这一页没有 JS：目录、视图、选中文件全是 radio + label，"
    "靠 :checked / :has() 切换。交互：hover 一个条目、按「列表」标签（label 激活 radio）"
    "、切到文档目录、再切回图标视图、Tab/Escape。",
    [
        [set_checked("#dir-docs", True)],
        [set_checked("#view-grid", True)],
    ],
    press=(1067, 25), wheel=None, overlay_id=None, overlay_actions=None, extra_keys=True,
))

# ---------------------------------------------------------------- kanban
kanban_columns = [("col-todo", "待办", ["对照测试加用例", "滚动条两趟", "多栏碎片化", "文本抗锯齿"]),
                  ("col-doing", "进行中", ["表格折叠边框", "flex 百分比基准", "文字前进量取整"]),
                  ("col-done", "已完成", ["字距走 GPOS", "描边按格子分段", "绘制用布局字宽"]),
                  ("col-backlog", "积压", ["缓存键重算", "容器查询重入", "sticky 偏移"]),
                  ("col-review", "待评审", ["代理对断行", "字重 500"]),
                  ("col-blocked", "阻塞", ["等字体就位"])]
kanban_actions = []
for column_id, title, cards in kanban_columns:
    kanban_actions.append(insert("#board", "", tag="div", cls="column", element_id=column_id))
for column_id, title, cards in kanban_columns:
    kanban_actions.append(insert("#" + column_id, title, tag="div", cls="col-head"))
for column_id, title, cards in kanban_columns:
    kanban_actions.append(insert("#" + column_id, "", tag="div", cls="cards",
                                 element_id="cards-" + column_id.split("-")[1]))
for column_id, title, cards in kanban_columns:
    cards_id = "#cards-" + column_id.split("-")[1]
    for card in cards:
        kanban_actions.append(insert(cards_id, card, cls="card"))
APPS.append(build(
    "example-kanban",
    "../../../../example/applications/kanban/index.html",
    "小竹看板去掉页面脚本后的静态壳：看板是脚本铺的，这里用动作插六列十七条卡片。"
    "交互：顶部「新建列」的 hover/按下、看板横向滚轮、Tab/Escape，最后撤掉 #editor 的 hidden。",
    [kanban_actions],
    press=(1052, 27), wheel=(500, 400, 180, 0), overlay_id="#editor",
    overlay_actions=[remove_attr("#editor", "hidden"),
                     set_text("#f-title", "对照测试加用例")],
))

# ---------------------------------------------------------------- zdb
zdb_cities = ["杭州", "成都", "北京", "上海", "广州", "西安", "南京", "天津", "重庆", "长沙",
              "苏州", "青岛", "厦门", "昆明", "合肥", "福州"]
zdb_names = ["沈青禾", "林岸", "周未", "许一", "陈默", "苏晚", "何宁", "于归", "白露", "郑南",
             "秦朗", "吴桐", "卓然", "柳岸", "范青", "康乐"]
zdb_open = [
    set_attr("#pane-welcome", "hidden"), remove_attr("#modebar", "hidden"),
    remove_attr("#pane-data", "hidden"), remove_attr("#grid-title-row", "hidden"),
    remove_attr("#grid-table", "hidden"), remove_attr("#pager", "hidden"),
    set_text("#grid-title", "users"), set_text("#grid-kind", "表"),
    set_text("#grid-total", "16 行"), set_text("#page-range", "1–16 / 16"),
    set_text("#status-text", "已打开 sample.db"), set_text("#file-name", "sample.db"),
]
zdb_fill = [insert("#object-list", "users 表 16", cls="obj is-selected"),
            insert("#object-list", "orders 表 12", cls="obj"),
            insert("#object-list", "活跃用户 视图", cls="obj"),
            insert("#grid-head", "id", tag="div", cls="ztd"),
            insert("#grid-head", "name", tag="div", cls="ztd"),
            insert("#grid-head", "city", tag="div", cls="ztd")]
for index in range(1, 17):
    zdb_fill.append(insert("#grid-body", "", tag="div", cls="zrow", element_id="row-%d" % index))
for index in range(1, 17):
    zdb_fill.append(insert("#row-%d" % index, str(index), tag="div", cls="ztd"))
    zdb_fill.append(insert("#row-%d" % index, zdb_names[index - 1], tag="div", cls="ztd"))
    zdb_fill.append(insert("#row-%d" % index, zdb_cities[index - 1], tag="div", cls="ztd"))
APPS.append(build(
    "example-zdb",
    "../../../../example/applications/zdb/index.html",
    "小竹数据库去掉页面脚本后的静态壳：欢迎页、对象栏、数据窗格。交互：撤掉 hidden 打开"
    "对象栏与数据表、插十六行数据、顶部「载入示例库」的 hover/按下、数据区滚轮、Tab/Escape，"
    "最后打开单元格详情。",
    [zdb_open, zdb_fill],
    press=(984, 27), wheel=(600, 400, 0, 120), overlay_id="#cell-detail",
    overlay_actions=[remove_attr("#cell-detail", "hidden"),
                     set_text("#detail-label", "users.city"), set_text("#detail-value", "杭州")],
))

for entry in APPS:
    target = CASES / entry["name"] / "case.json"
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(entry, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print("%-24s steps=%d" % (entry["name"], len(entry["steps"])))
