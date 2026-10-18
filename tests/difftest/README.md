# difftest —— 对照测试模块

这是 [`docs/TESTING.md`](../../docs/TESTING.md) 的可运行实现：同一份用例 JSON 分别喂给**引擎**和**浏览器**，两端各导出一份同构的 trace 与一张 PNG，然后按结构字段 / 长度字段分开对拍。通过线数字在 `passline.json`。

测试可以驱动步骤、写 JSON、和浏览器对拍。绘制、级联和计算样式的取值不能在这里再写一套。边界在 [`docs/TESTING.md`](../../docs/TESTING.md)。`capture` 写出这一帧的颜色 PNG。

一条原则：**对齐转移函数，不对齐时间轨迹。** 动态效果降维成相同参数下的逐状态采样，复用同一条静态管线。

后面所有功能开发都用这套机制验收：提一个新特性 = 落一条用例 + 录 golden + `check` 到绿。

---

## 1. 命令

```bash
python tests/difftest/run.py list                  # 用例清单、必过集标记、golden 有无
python tests/difftest/run.py probe                 # L0 探针：环境钉死了没有
python tests/difftest/run.py record                # 在钉死的浏览器上录 golden（第 10 节）
python tests/difftest/run.py check                 # 引擎回放 + 对拍 + 出判决
python tests/difftest/run.py check --with-browser  # 另加浏览器回放，分离「漂移」与「回归」
python tests/difftest/run.py check --case flow-basic --case scroll-sequence
```

### 选哪些用例：一条、一族、全量

`--case` 既可以写用例名，也可以写通配（`*` / `?` / `[...]`），并且可以重复；不写就是全量。
`list` / `record` / `check` / `coverage` 四个命令认的是同一套选择器，录制和回放不会走岔。

```bash
python tests/difftest/run.py check --case flow-basic          # 一条
python tests/difftest/run.py check --case 'grid-*'            # 一族（16 条）
python tests/difftest/run.py check --case 'example-*'         # 小竹系列示例（18 条）
python tests/difftest/run.py check --case 'text-*' --case 'font-*'   # 几族合并
python tests/difftest/run.py check                            # 全量
python tests/difftest/run.py check --failed                   # 上一份报告里的红用例
```

`--failed` 读 `build/difftest/run/report.json`（可用 `--report <path>` 指到别的报告，比如基线），
只跑那份报告里 status 为 red 的用例——「改一条、验一条」的循环里不必再跑全量。
报告里的每个用例都会带 `名称 / 状态 / 耗时` 逐条打印并 flush，跑到哪一条一眼能看见。

### 并发

`check` 的引擎回放阶段可以并发：

```bash
python tests/difftest/run.py check --jobs 8          # 全量回放，约 8 倍并行
```

每条用例有自己的输出目录和自己的子进程，互不共享状态，所以引擎阶段天然可并发；浏览器阶段
（`--with-browser`、`record`、L0 探针）仍然串行——它们驱动的是同一个调试端口上的同一个浏览器。
默认 `--jobs 1`。本机 8 核、全量 176 条，`--jobs 8` 实测回放 420 s（不并发时这套用例大约要
15 分钟）。并发度不是越大越好：每条用例自己就吃满一个核，8 个并发会互相拖慢（同一批应用用例
4 并发 112 s 跑完，8 并发下最慢的一条反而从 110 s 涨到 244 s）。先按族选子集、并发给 4~6，
通常比全量 8 并发快。

`check` 的退出码：L0/L1/L2/L4 全绿为 0，否则 1。报告落在 `build/difftest/run/report.{json,html}`。

引擎侧驱动单独跑（不依赖 Python，也不依赖浏览器）：

```bash
cmake --build build/windows-x64 --target zui_difftest
./build/windows-x64/zui_difftest --case tests/difftest/cases/flow-basic/case.json \
    --repo . --out build/difftest/engine-smoke
```

`ctest -R difftest` 跑两件事：引擎驱动的静态单帧冒烟（`difftest_engine`）和 L6 架构自检（`zui_difftest_selfcheck`）。

---

## 2. 目录（对应 TESTPLAN 第 17 节）

| 内容                          | 位置                               |
| ----------------------------- | ---------------------------------- |
| 用例（页面 + steps + golden） | `cases/<case>/`                    |
| 分歧决策记录                  | `cases/<case>/DIVERGENCE.md`       |
| Chrome/Edge 驱动（CDP）       | `chrome/`                          |
| 引擎驱动（测试 API 适配）     | `engine/`                          |
| trace 组装、diff、报告        | `report/`                          |
| L0 探针页                     | `probe/`                           |
| L6 架构自检                   | `../difftest_selfcheck/`           |
| 用例生成脚本（示例应用族）    | `tools/mkcases.py`                 |
| 引擎侧观测通道                | `source/layout`（LAYOUT 第 16 节） |

配置（都是数据，改配置不用改代码）：

- `env.json` —— L0 环境钉死：flag 集、浏览器路径、字体清单、探针期望。
- `props.json` —— L1 属性清单、归一化规则、初始值表。
- `matrix.json` —— 支持矩阵的机器可读副本（`aligned` / `behavior` / `unsupported`）。
- `passline.json` —— 通过线、容差、必过集规则、验收声明开关。

---

## 3. 用例格式

```json
{
  "name": "flow-basic",
  "page": "page.html",
  "viewport": [800, 600],
  "matrix": ["flow.normal", "flow.flex"],
  "required": true,
  "steps": [
    { "op": "load" },
    { "op": "resize", "w": 800, "h": 600 },
    { "op": "trace" },
    { "op": "capture" },
    { "op": "scroll", "target": "#scroll", "x": 0, "y": 100 },
    { "op": "trace" }
  ],
  "expect": "golden/",
  "pixels": "strict",
  "text": { "breaks": false, "advances": false }
}
```

| 操作       | 语义                              | 浏览器端                                              | 引擎端                                        |
| ---------- | --------------------------------- | ----------------------------------------------------- | --------------------------------------------- |
| `load`     | 加载，等到 `document.fonts.ready` | `Page.navigate` + 等 load + 等字体                    | `load_file` + 级联 + 布局                     |
| `resize`   | 视口尺寸                          | `Emulation.setDeviceMetricsOverride`                  | 重新级联 + 布局                               |
| `eval`     | 状态准备（**动作 DSL**，不是 JS） | 翻译成 JS                                             | 改文档树 + 重排                               |
| `scroll`   | 设滚动偏移并读回                  | `scrollTo({behavior:'instant'})` / `scrollTop=`       | 夹紧后写 `scroll_x/y` + `sync_world`          |
| `freeze`   | 全部动画定到 t（ms）              | `getAnimations().forEach(pause + currentTime)`        | `Transitions::sample(t)`                      |
| `advance`  | 虚拟时间推进                      | `Emulation.setVirtualTimePolicy`                      | 虚拟时钟                                      |
| `dispatch` | 输入事件                          | `Input.dispatchMouseEvent` / `Input.dispatchKeyEvent` | `source/input/interaction.cpp`，与 App 同一份 |
| `trace`    | L1+L2 快照                        | `Runtime.evaluate(collector.js)`                      | `export_trace`                                |
| `capture`  | 像素                              | `Page.captureScreenshot`                              | OpenGL 离屏读回 → PNG。不 poll 窗口          |

### `eval` 的动作 DSL

TESTPLAN 第 3 节要求「`eval` 只许用两端共有的 API 子集，录制时校验」。做法是**把子集本身固定下来**：用例只能写下面这些动作，写别的两端都会硬报错，不会静默跳过。

```json
{ "op": "eval", "actions": [
  { "do": "set_style",    "target": "#card", "props": { "width": "50px" } },
  { "do": "add_class",    "target": "#card", "name": "wide" },
  { "do": "remove_class", "target": "#card", "name": "wide" },
  { "do": "set_text",     "target": "#title", "text": "Hello" },
  { "do": "set_attr",     "target": "#img",  "name": "width", "value": "300" },
  { "do": "remove_attr",  "target": "#lightbox", "name": "hidden" },
  { "do": "set_checked",  "target": "#settings_open", "value": true },
  { "do": "scroll",       "target": "#list", "x": 0, "y": 120 },
  { "do": "insert",       "target": "#grid", "tag": "button", "class": "tile", "text": "海边 01" },
  { "do": "remove",       "target": "#gone" }
] }
```

`target` 支持 `#id`、`.class`（首个匹配）、`tagName`（首个匹配）、`window` / `:root`。

- `remove_attr` 是 `set_attr` 的橡皮那一半：布尔属性（示例页的 `hidden`）写空串仍然**匹配**选择器，
  所以只靠 `set_attr` 打不开任何对话框。两个驱动各实现一次，`eval` 步结束后统一重排。
  示例应用里的 `#lightbox` / `#editor` / `#creator` / `#cell-detail` 都是这么进对照的。
- `insert` 是**结构化**的，不是 HTML 字符串：`tag` / `id` / `class` / `text` / `props`（内联样式）/
  `before`（插到某个兄弟前）。`id` 会立即注册进文档，后续动作可以直接引用刚插入的节点，所以能
  一层层搭出 `.column > .cards > .card` 这种结构。

---

## 4. trace 协议（两端必须同构）

`engine/trace_export.cpp` 与 `chrome/collector.js` 是同一个协议的两种实现，字段名、单位、归一化必须逐字一致。

```json
{
  "schema": "zui.difftest.trace/2",
  "elements": [
    {
      "path": "0/1/0",              // 元素子节点索引链，从 <html> 起；两端一致
      "tag": "div", "id": "page", "classes": "card wide",
      "has_box": true,              // 结构：display:none / contents 无盒
      "transformed": false,
      "border_box": [x, y, w, h],   // 长度，世界坐标（已扣滚动）
      "scroll_size": [w, h],        // 长度，padding box 撑到可滚动溢出（Chrome 给整数，比较前取整）
      "scroll_offset": [x, y],      // 长度
      "style": { "display": "block", "..." : "..." },
      "style_digest": "3f2a...",    // 归一化后的 prop=value; 流的 FNV-1a 64
      "lines": {                    // 只有「文本归属元素」才有，两端都省略就没有
        "count": 3,                 // 结构：行盒数，精确
        "tops": [...],              // 长度：每行碎片 rect 的 top
        "heights": [...],           // 长度
        "breaks": [...],            // 结构，opt-in
        "advances": [[...], ...]    // 长度，opt-in
      }
    }
  ]
}
```

### 映射规则（第 9 节）

- **只含两端同构的字段。** Chrome 不暴露匿名盒，所以匿名盒不进 trace；匿名盒引起的差异表现为可见盒的位置尺寸差，由 L2 长度 + L4 兜底。
- 引擎端**禁止**把 `used_requests`、缓存键、排除区快照写进 trace。报告里出现这些字段名就是导出层写错了。
- **文本归属元素**的定义两端一致：元素有非空白的**直接**文本子节点，且没有任何后代元素含非空白文本。只有这种元素，引擎的行盒摘要和浏览器的 `Range.getClientRects()` 描述的是同一个字符串。
- 长度一律量化到 **1/64 px**（LAYOUT 第 12 节同一个量子）。`scroll_size` 例外：Chrome 只给整数（`scrollWidth`），比较前把两端取整——容差吸收表示误差，不吸收拓扑差异。

### 字段分类与容差（第 9、15 节）

| 类别      | 字段                                                                                                        | 容差              |
| --------- | ----------------------------------------------------------------------------------------------------------- | ----------------- |
| structure | 元素存在、`has_box`、`transformed`、`lines.present`、`lines.count`、`lines.breaks`                          | **0，差即几何错** |
| length    | `border_box`、`scroll_offset`、`scroll_size`、`lines.advance[]`（逐行行距）、`lines.advances`（逐 cluster） | 1/64 px           |

`lines.tops` / `lines.heights` 仍在 trace 里，但不进 L2 比较：两端测的不是同一个量——浏览器
`Range.getClientRects()` 给的是设备像素对齐的文本片段矩形（内容盒），引擎给的是布局行盒。行数、
断行偏移与逐行行距才是两端同构的判据（TESTPLAN 第 6 节）。
| l1 | `style.<prop>` | 数值 0.01，字符串精确 |
| clamp | `scroll_offset` 读回值 vs 请求值 | 见第 4 节 |

容差不吸收拓扑差异：断行数不同、行盒数不同、滚动高度不同，像素差再小也判几何错。

`style_digest` 不当作被比较的字段，它是变更索引；如果**每个属性都比中了**而 digest 还不同，报告会出一条 `harness` 告警——那是两端归一化漂移，属于测试模块自己的 bug。

---

## 5. L1 属性清单

`props.json` 的 `default_props` 是 L1 唯一负责的范围。清单里的属性两端都必须能给出计算值，否则不算「一致」。用例可以用 `style_props` 收窄清单。

归一化四条规则（两端各实现一遍，必须逐字一致）：

1. 去首尾空白，字母小写，空白串压成单个空格。
2. 零长度统一成 `0`（`0px` / `0.0px` / `-0px` 都是 `0`）。
3. 双方都能解析成 `<数字><单位?>` 时按数值比较（长度 eps 0.01）。
4. 其余按字符串精确比较。

**`initial` 表**：引擎的 `computed` 只装「某个样式表给过值」的属性，初始值不落地；Chrome 的 `getComputedStyle` 一律给值。`props.json` 的 `initial` 把两端补齐到同一契约。表里的写法按 Chrome 的计算值形态写。表错 = L1 红，那是「你的初始值跟规范/浏览器不一致」的真实信号，别在现场改表消红。

**已在 `deferred_props` 里的**：颜色 / 背景 / 圆角 / 阴影（引擎侧 `currentcolor` 未解析、颜色写法不同）、`transform` / `opacity` / `filter`、`transition`、grid 模板、`writing-mode`、`direction`。用例要用就在 `style_props` 里显式声明，并接受它落进「输入未对齐」。

---

## 6. 失败分类（第 11 节）

| 分类               | 判定                                | 去向                                     |
| ------------------ | ----------------------------------- | ---------------------------------------- |
| `input-misaligned` | L1 不一致，或钳制断言失败           | 修驱动器或用例，不算引擎 bug             |
| `geometry`         | L2 不一致且 L1 一致                 | 修布局，按 ROADMAP 块归档                |
| `paint`            | L2 一致、L4 不一致                  | 修绘制                                   |
| `divergence`       | 按规范我们是对的，Chrome 是另一分支 | 改 LAYOUT 策略段，**准入门槛见第 11 节** |

报告**不给**「策略分歧」这个出口——准入需要证据清单和评审记录，落成 `cases/<case>/DIVERGENCE.md` 之后才成立。没有准入证据的分歧默认按几何错处理。

---

## 7. golden 治理（第 10 节）

- golden **只来自浏览器**，且在钉死的构建上录（`env.json` 的 flag 集）。
- 每条 golden 对应用例的一个 step，不共享。
- 升级浏览器：先 `check --with-browser` 做跨版本对照。浏览器对浏览器漂移的项记入版本漂移清单并更新 golden；未漂移而引擎对不上的，才算引擎回归。

---

## 8. 现在的状态（诚实版）

| 层  | 状态                           | 说明                                                                                                                                            |
| --- | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| L0  | 已实现，当前**绿（探针全过）** | 滚动条 15/15/15；advance 浏览器 131.609 vs 引擎 131.760（内嵌 Noto Sans，容差 0.5）                                                             |
| L1  | 已实现，当前**绿**             | `flow-basic` / `scroll-sequence` 的属性全中                                                                                                     |
| L2  | 已实现，当前**红**             | 8 条用例里 `flow-basic` / `grid-tracks` 全绿，`scroll-anchor` / `scroll-sequence` 几何也全绿（只剩像素）；红的是文本行盒、flex 收缩与 flow-page |
| L3  | 已实现                         | 必过集里的状态序列读回；`scroll-anchor` 的插入步骤带 `readback`（插 40px，scrollTop 120→160 由浏览器录制，两端一致）。`scrollIntoView` 仍是后置项 |
| L4  | 已实现，当前**红**             | 帧缓冲颜色 PNG 与 golden 对拍；`scroll-anchor` 的几何已对齐，差的是滚动条本身没画（绘制错，第 11 节）                                           |
| L5  | 已实现                         | `dispatch` 走无窗口交互模块；读回进 `<step>.state.json`。短页录进 golden 之前报告是 `not-implemented`                                           |
| L6  | 已实现，当前**红**             | `zui_difftest_selfcheck` 8 条断言全过（覆盖 TESTPLAN 第 12 节全部九项）；红的是用例级的 `converged == false` 与架构自检                         |
| L7  | 未实现                         | WPT 子集与 fuzz 差分是阶段 4                                                                                                                    |

`check` 现在多两件事：帧末诊断（LAYOUT 第 16 节）与必过集覆盖。前者是引擎写的
`step-XXX.diagnostics.json`，与 trace 并排、不进 trace（浏览器没有对应物，golden 只留两侧
同构的字段）；后者是 `run.py coverage`，按 TESTPLAN 第 15 节逐条问「这个「一致」特性有没有
golden 用例」。

| 新增               | 判据                                                                                                                           | 现状                                                                                                           |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| `converged` 诊断   | 每个 trace 步导出 `converged == false` 的盒子、DOM 路径与来源；用例除非声明 `expects_non_convergence`，否则算失败              | 2 条用例红，来源已指名（flex 的 Final 与 target 不一致）                                                       |
| 帧计数器           | `dirty_marks` / `laid_out` / `cache_hits` / `evicted_measure_slots` / `container_requery_rounds` / `interned_styles`，按步累加 | 支撑下面三条架构自检                                                                                           |
| 架构自检（用例级） | 滚动步零重排（`frames == 0`）、观测步零重排、动画步零 Layout 标脏                                                              | 当前全过                                                                                                       |
| L0 分类            | 环境项决定 L0；引擎参数分歧（滚动条厚度、advance）归 L2                                                                        | L0 绿，3 项引擎分歧进 L2                                                                                       |
| 必过集覆盖         | `matrix.json` 的「一致」特性每条至少一个 golden 用例                                                                           | 11/12；`text.opentype` 由 `font-kerning` 覆盖；缺 `text.font-load`（要有字体的异步加载与 `font-display` 时序） |
| `eval.insert`      | 结构化插入（`tag` / `id` / `class` / `props` / `before`），两端同一份 DSL                                                      | 已通，`scroll-anchor` 用例在用                                                                                 |

### 前置缺口（TESTPLAN 第 18 节点名的四项）

| 缺口       | 现状                                                                                                                                                         |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| trace 导出 | **已完成**（含结构字段；`breaks` / `advances` 需用例 opt-in）                                                                                                |
| 测试 API   | **完成**：load / resize / eval（含 `insert`）/ scroll / freeze / advance / dispatch / trace / capture。`dispatch` 与 App 共用 `source/input/interaction.cpp` |
| 帧画面     | OpenGL `probe_read_frame` 与 Vulkan `probe_read_frame_vk` 读离屏目标。`capture` 默认 OpenGL；`ZUI_PROBE_RENDERER=vk` 改走 Vulkan                          |
| L0 探针    | **已完成**（`run.py probe`）                                                                                                                                 |

### 第一次运行暴露出来的问题（都是实测，不是推测）

1. **flag 静默失效（已修）**：`--hide-scrollbars=false` 被 Chromium 当成布尔开关，视口滚动条厚度静默变 0。已从 `env.json` 删掉。这正是第 8 节说探针要抓的东西。
2. **视口滚动条厚度参数对不上（已修）**：经典滚动条实测 **15px**，引擎原来是三处各自的编译期常量 11px。现在厚度是 `PlatformMetrics.scrollbar_thickness`，由驱动按 `env.json` 注入 15（LAYOUT 第 9 节）。
3. **视口滚动条没被预留（已修）**：根元素曾经恒等于视口高，从不溢出，因此从不收缩。UA 样式去掉了 `html`/`body` 的宽高，触发条件恢复成「内容高过视口」；探针现在两侧都量到 15，`html` 宽 785。
4. **元素滚动器（已修）**：`overflow: auto` 现在按内容溢出留槽，且 `scrollWidth` / `scrollHeight` 由 `Overflow.scrollable`（padding box 去掉槽再并溢出）导出，滚动偏移按同一个量夹紧。`scroll-anchor` 的几何已经全绿。
5. **根元素高度（已修）**：UA 样式里的 `height: 100%` / `min-height: 100%` 去掉，`html` / `body` 回到内容高度，与 Chrome 的 html.css 一致。
6. **flex / grid 子项偏移一个边框宽（已修）**：`cb_offset` 按 LAYOUT 第 4 节是相对包含块的 padding box，flex 与 grid 原来又加了一次容器边框，而绘制/命中那一趟还会再加一次。去掉重复的一份后 `flow-basic` 全绿。
7. **字体没内嵌（已修）**：用例原来用系统字体，两端 advance 差到 1.2px。现在 `tests/difftest/fonts/NotoSans-Regular.ttf`（OFL）入库，浏览器 `@font-face`、引擎读 `env.json` 的 `fonts.embedded` 注册同一个 family；探针 advance 浏览器 131.609 vs 引擎 131.760（0.15px）。用 TTF 而不是 woff2：引擎侧 FreeType 没开 brotli，解不了 woff2。

加上 `grid-tracks` / `inline-text` / `scroll-anchor` 之后新一轮实测（这些用例是给二期块 1/2/3 立的靶子）：

8. **根元素高度到处在差**：`html` / `body` 的 `border_box.h` 每个用例都差（引擎 600 = 视口高，Chrome 是内容高）。这条与第 5 条同一个原因，现在是每条 golden 的第一批差异。
9. **文本行盒**：`inline-text` 的 `#para` 是 4 行 vs 3 行、行顶差 6px，`letter-spacing` / `vertical-align: sub` 的盒也不同。第 6 节的逐 cluster advance 要等内嵌字体，行数与行顶不需要。

   （已修：内嵌字体 + 行盒按 `line-height` + 断行在词前判断 + `white-space` 下换行折叠成空格。`#para` 的行数、行顶与断点现在与 Chrome 完全一致，`#justify` 只差最后一个词。）
10. **滚动器不暴露可滚动范围**：`scroll-anchor` 的 `#scroller` `scroll_size.h` 期望 270、引擎 100（第 4 条同因），滚动偏移因此不夹紧。
11. **flex 有不收敛帧**：引擎自己的诊断说 `#host` 的「一次 Final 的边框盒与 `flex_pass` 解出的 target 不一致」，两条用例（`flex-shrink-column`、`flow-page`）命中。这条以前只能靠画面猜。

补覆盖时新用例又暴露两条（都记在这里，不靠现场豁免）：

12. **纯 inline 元素的矩形两端不是同一个量**：Chrome 的 `getBoundingClientRect()` 给内容盒（16px Noto Sans 是 22px 高）、`scrollWidth`/`scrollHeight` 对纯 inline 返回 0；引擎给行内盒（`line-height` 20）与它的内容宽高。`text-font-fallback` 因此改用 inline-block（它测的是回退链，不是这条）。
13. **inline-block 所在的飞行盒高度**：Chrome 的行盒比 inline-block 自身高（基线在它的下外边距边，strut 的 descent 落在下面），引擎只取到 20px；`text-font-fallback` 的 `scroll_size.h` 因此差 1px。

## 11. 用例清单（覆盖用）

| 用例                                                                                                                                           | 覆盖的特性                                                                                                                                 | 状态                                                                     |
| ---------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------ |
| `flow-basic`                                                                                                                                   | flow.normal / flow.flex / flow.grid / scroll.overflow                                                                                      | 绿                                                                       |
| `grid-tracks`、`grid-item-percent`、`grid-named-lines`、`grid-auto-repeat`、`grid-auto-fill-outside`、`grid-template-areas`、`grid-shorthands` | flow.grid（轨道、放置、对齐、子项区域百分比、命名线、`repeat(auto-fill\|auto-fit)`、区域名、`grid-area` 与 `grid-template` / `grid` 简写） | 绿                                                                       |
| `grid-abspos-area`                                                                                                                             | position.out-of-flow（网格区域作绝对定位子项的包含块）                                                                                     | 绿                                                                       |
| `css-lengths`                                                                                                                                  | flow.normal（`em` / `rem` / `calc()` / `min()` / `max()` / `clamp()`）                                                                     | 绿                                                                       |
| `grid-baseline`                                                                                                                                | flow.grid（`align-items: baseline` 的行基线与行高）                                                                                        | 绿                                                                       |
| `grid-implicit-before`                                                                                                                         | flow.grid（负线号越界产生的、显式网格之前的隐式轨道）                                                                                      | 绿                                                                       |
| `writing-mode-inherit`、`writing-mode-orthogonal`                                                                                              | text.complex-script（纵向书写与正交流）                                                                                                    | 绿                                                                       |
| `animation-opacity`                                                                                                                            | animation.interpolation（冻结采样下的使用值）                                                                                              | 绿                                                                       |
| `scroll-anchor`、`scroll-sequence`                                                                                                             | scroll.overflow（插入后的锚定、滚动序列）                                                                                                  | 几何绿，差滚动条绘制                                                     |
| `inline-text`                                                                                                                                  | flow.inline（断行、行盒、vertical-align）                                                                                                  | 红（7）                                                                  |
| `inline-metrics`                                                                                                                               | flow.inline（行内盒上报字体内容区、半行距、`line-height: normal`、`top` / `bottom`）                                                       | 绿                                                                       |
| `inline-vertical-align`                                                                                                                        | flow.inline（`middle` / `sub` / `super` 的偏移量）                                                                                         | 红（量偏差用，不设 required）                                            |
| `example-table`                                                                                                                                | table（自动/固定列宽、跨格、边框合并、caption、匿名表盒、`visibility: collapse`）                                                          | 红（二期块4 未实现）                                                     |
| `font-kerning`                                                                                                                                 | text.opentype（`font-kerning` 决定 kerning，`lang` 不决定）                                                                                | 绿（必过）                                                               |
| `font-vf-instance`、`font-sans-resolution`、`font-generic`                                                                                     | 通配族解析、可变字体实例、逐字 advance（**系统字体诊断用例，golden 只在本机字体清单下有效，故意不设 required**）                           | `font-vf-instance` 绿；另两条差 `scroll_size` 取整与 Segoe UI 的 advance |
| `max-content-parent`、`flow-page`、`flex-shrink-column`                                                                                        | 固有宽度、综合页、flex 收缩                                                                                                                | 红                                                                       |
| `container-query`                                                                                                                              | container.query                                                                                                                            | 红（`cqw` 单位没实现）                                                   |
| `text-font-fallback`                                                                                                                           | text.font-fallback                                                                                                                         | 红（上面第 12/13 条）                                                    |
| `multicol-basic`                                                                                                                               | 多栏（`column-count` / `column-width` / `column-gap` / 平衡 / 定高 / `column-fill: auto` / 空尾切片）                                      | 绿（逐字段与 Chrome 一致）                                               |
| `multicol-text`                                                                                                                                | 多栏里的行内格式化上下文（段落在栏间按行切开，片段并集与逐行位置）                                                                         | 绿                                                                       |
| `multicol-fragments`                                                                                                                           | 多栏里的 grid / table / 列 flex 断点（row、row、item 边界），以及片段里孙级的几何                                                          | 绿（必过）                                                               |
| `multicol-float`                                                                                                                               | 多栏里的浮动（行缩短、`clear`、装不下的浮动开下一栏、平衡不受浮动影响）                                                                    | 绿（必过）                                                               |

问题 1 是测试模块自己的；2–6 是引擎的；7 是用例的前置条件。

## 12. 最终验收集：`example/tests/`

最终通过标准是 `example/tests/` 里的每个示例页：它的 HTML + CSS 原样与 Chrome 对齐。每个页面一个
用例（`example-text` / `example-hello` / `example-style` / `example-flow` / `example-checks` /
`example-studio` / `example-transition` / `example-table`），`required: true`，页面路径直接指向
示例目录，golden 由 Chrome 在 800x600 录制。`example-table` 是二期块4（表格）的验收页，故意放在
这一组里：块4 没做完它就不会绿。当前状态（2026-09-25 晚）：

| 用例                 | 结构/长度 | 像素  | 备注                                                |
| -------------------- | --------- | ----- | --------------------------------------------------- |
| `example-checks`     | 1         | 45429 | 差 `.sample.nowrap` 的横滚动条高度（§12.4 第 5 条） |
| `example-text`       | 1         | 43119 | 同上                                                |
| `example-transition` | 16        | 41080 |                                                     |
| `example-hello`      | 37        | 87691 |                                                     |
| `example-studio`     | 38        | 60610 |                                                     |
| `example-style`      | 178       | 45317 |                                                     |
| `example-flow`       | 186       | 34785 | `max-content` 与 `max-width` 组合（§12.4）          |
| `example-table`      | 23        | 29863 | collapse 行的 `scroll_size` 与两处句子宽度（§12.6） |

已知的成批来源（按影响排序，都是引擎侧）：

1. **逐字形 advance 的最后 1/64px（已定位，未消）**：浏览器与 FreeType 对同一个字形给出的值可能
   差一个量子。实测内嵌 Noto Sans 的 `A`：浏览器 10.234375（655 个量子）、引擎 10.21875（654），
   `V` 同样差一个；长串上这个差会累积到整数 `scroll_size` 的取整边界（`AVAVAVAVAVAVA` 121.5 vs
   121.484375，Chrome 的 `scrollWidth` 122 vs 引擎 121）。kerning 本身已经对上（§12.5）；这条要
   复刻浏览器的 hinted advance 才能消，暂记为已知残差。
2. ~~**脚本生成的 DOM**~~（已解决，见 §12.3）：`example-studio` 的 `<script>` 建了 `#log` 的行，
   本引擎不执行脚本，golden 里有、引擎里没有——5005 条结构差里绝大部分是它。改成两边都加载同一份
   「去掉脚本」的页面后降到 45 条。
3. **表格**（二期块4）：匿名表盒、`DisplayInner::Table` 与 `source/layout/table/` 已经落地，
   `example-table` 从 625 降到 23；剩下的是 collapse 行的 `scroll_size`（Chrome 对半个边框取整）
   与两处句子宽度（同第 1 条）。
4. **`flex` 的 `max-content` 与 `max-width` 组合**：卡片宽度决定 `.row` 的换行位置，是
   `example-flow` 那一坨的主因。
5. 行内元素两侧的空格在 advance 里会丢（§12.4），`middle` / `sub` / `super` 的偏移量还在猜
   （`inline-vertical-align` 用例专门量它们）。
6. **后代的滚动溢出会向上传染**：祖先的 `scrollWidth` / `scrollHeight` 包含未被裁剪的后代溢出
   （§12.7 的 `#fixed_height` 溢出栏正是这样进到 body/html 的）。引擎补上这条之后，`multicol-*`
   全绿，但 `example-style`（+1）与 `font-generic`（+2）各多出一条差——那是**放大**了这两条用例本来
   就错的 `scroll_size`（`.bar.calc-sub` 360 对 280、`.vcol` 21 对 141），不是新问题。
7. **绝对定位的静态位置在 flex 项里算错**（已定位，未消）：`example-style` 的 `.pick` 是
   `position: absolute`、父级 `.picks` 是普通块、包含块是初始包含块；Chrome 给的静态位置是
   「按流排时它会在的地方」(36, 599)，引擎给 (72, 1163)——两个坐标都偏，正是原点栈累计错了
   （不是绘制帧的问题：绘制已经按 `abs_containing_block` 用包含块那一帧，见 `paint_map` 的
   `Frame`）。根因方向：只有普通流与 `in_flow` 会 `push_content_origin`，flex / grid / 表放子项时
   不推，于是包含块到该盒之间少了一层、又有一层被重复计入。`.calc-sub` / `.use-min` / `.use-max` /
   `.use-clamp` 那 8 条是同一族的另一条：`calc(100% - 80px)` 这种「百分比 + 长度」的混合
   `calc()` 目前判无效，宽度退回 `auto`。

### 12.1 交互状态也要有用例（`eval` + `freeze`）

静态加载看不到只在状态里出问题的地方：`example/tests/transition` 的全屏遮罩 `.mask` 初始是
`opacity: 0`，只有应用加上 `.show` 才可见，所以原来的 `example-transition` 用例永远比不到它。
`example-transition-mask` 用 `eval`（`add_class`）+ `freeze`（冻结时钟跨过过渡）走同一条路，两个
状态各比一次：机制能表达，也确实看得出来——加上它之后遮罩那一帧有 46 万像素差。

看到的是绘制顺序问题：`.mask` 是 `position: fixed; z-index: 40`，几何（800×600）对，但被画在页面
内容**下面**，所以正文没有被压暗。层叠上下文在 `source/layout/stacking.cpp` 里还是 stub，这正是
下一步。

同一组步骤也暴露了 `example/tests/flow` 的卡片问题：`width: max-content` 的（当前按 auto 处理，
所以不是它）flex 项，**自己的边框盒对（233px）而里面的块级子项按 min-content 排版**（39.78px =
最长的不可断词），于是每行一个字。换成 `width: 233px` 就正常，所以触发条件是「auto 宽的 flex 项 +
块级子项」这条路径，具体在哪一趟把 min-content 宽度写进了子项，还没定论。

### 12.2 钉住的 `Final` 与测量趟：整条链路

### 12.3 带脚本的页面：只比 HTML + CSS

验收口径是「页面的 HTML 与 CSS」，而引擎没有脚本引擎，所以脚本生成的 DOM 不可能对齐：浏览器有
（脚本跑过），引擎没有，`element.present` 一条条报。`example-studio` 的 5005 条结构差里绝大部分
就是 `<script>` 建出来的 `#log` 行。

做法：用例写 `"scripts": false` 与 `"source": <示例页>`，`record` 时由 `materialize_page()` 生成
`page.html`（去掉 `<script>…</script>`，并把相对 `href` / `src` 重写成指向示例目录），**两端加载同
一份生成的文件**。只去掉浏览器的脚本会让它后面的元素路径全部错位，所以必须两边同一份。

脚本驱动的状态仍然可以比，只是要像 `example-transition-mask` 那样用 `eval` 步骤显式走一遍——
静态页去脚本、交互状态用 `eval`，两条并存。

上面那条已经查清，链路如下（都带代码位置与实测）：

1. flex 容器对每个项问三趟：`FlexBase`（`flex.cpp:443`）→ `MinContent`（自动最小值，`:491`）→
   `Final`（`:764`），Final 之前显式 `reset_ask_fuse`（`:769`）。三个不同的键，LAYOUT 第 5 节允许
   （每轮的两次提问记账在 reset 处归零）。
2. 每一趟都真的把这个项**排一遍**：内在大小的那趟先算出该项的 min-content 内联尺寸（实测：卡片
   209 → 39.78），再把**子树按这个宽度排完**。
3. 子树在测量趟里也被当作正式布局来问：`child_space()` 无条件写
   `space.request.kind = RequestKind::Final`（`normal_flow.cpp:458`；内联路径 `in_flow.cpp:129`，
   grid 同）。也就是说后代在测量趟拿到的键与正式布局是同一种。
4. `BoxCache::store` 只在 kind==Final 时移动 `pin_`，`BoxCache::find` 命中时不移动
   （`box_cache.cpp`）。于是 `pin_` 实际含义是「**最后一次必须计算的 Final**」，而不是 LAYOUT 第 2
   节说的「最近一次用于绘制的 Final」。
5. 落到实测：项的 Final 趟用真实尺寸再问子项，键与 FlexBase 趟（同尺寸）相同 → 命中 → **不重钉**
   → `pin_` 停在 MinContent 趟那份 39.78 的布局；画笔读 `pinned_final()`，于是卡片自己的边框盒对
   （233），子项按 min-content 排（每行一个字）。

三层问题，由浅到深：

- **契约层**：`pin_` 的含义与规范不一致。命中也要重钉；而且命中意味着**子树没有被重问**，所以重钉
  必须沿结果里的子请求往下走。
- **设计层**：后代在测量趟里带 `Final` 的键进缓存，于是「测量布局」与「正式布局」在同一个尺寸下
  **共用同一个键与槽**，事后无法区分。LAYOUT 第 3、5 节没有规定 kind 不传播，今天的实现只让被问的
  那个盒子带 kind；让 kind 传播能把两者分开，代价是内在尺寸测量会以「测量空间」排子级（Blink 的
  做法是相反的一条：不传播，但保证绘制只读最终树）。
- **算法层**：flex 一轮里对同一个项问三次，正是它在同一个 `compute` 内造出「min-content 尺寸的子树
  布局」。这条推论是：任何「最后一次布局胜出」的规则都必须以**最后一次提问**为准，而最后一次提问
  正是 Final。

判据（下一步的实验）：在 `pin_` 的每次写入处打印（盒子、kind、可用内联尺寸、结果尺寸），跑最小
复现。若子项的钉住只发生在卡片的 MinContent 趟、之后再没被写，契约层就够；若出现同一个键对应两次
不同尺寸的结果，设计层为真，必须让 kind 传播（或在键里带上「这次是不是用于绘制的布局」）。

上一轮把「命中即重钉 + 测量趟不钉 + 沿子树重钉」三件一起上了：示例大幅变好（flow 636→466、
flow-page 632→458、style 324→258），但 hello 卡片被钉到一份更窄的布局（93 应 72），说明子树重钉
会走到**别的趟**留下的子请求。先做上面这个实验把落点钉死，再决定修哪一层。

实验做完（提交 `669f1fe`）：打印显示 `.blk` 在 FlexBase 趟被钉到 84.68、在 MinContent 趟被钉到
23.90，而卡片的 Final 趟**一条后代钉住都没有**——它拿到的是 FlexBase 趟留下的键，命中缓存。也就是
判据的第一种：**同一个键没有出现两个尺寸**，契约层就够。修法是只留两件——命中 Final 时重钉、并沿
缓存结果里的子请求递归重钉（不做测量趟抑制，避免删掉后代唯一的钉住来源）。落地后最小复现的三个
盒子都与 Chrome 一致，示例把上一轮的成绩原样拿到且 hello 不再被牵连：flow 636→466、
flow-page 632→458、style 324→258、transition 35→29，L6 绿、无回退。

### 12.4 行盒与容器侧的四条实测（本轮）

1. **`lines.breaks` 的编号口径（已修，`70bd03b`）。** 浏览器按元素**全部后代文本节点**的码点编号，
   被折叠掉的白空格仍然占位；引擎原来只数画出来的 run，`#justify` 里换行后的每个断点都少掉折叠空白
   的字节数。现在 `LineBox::source_chars` 记录行内消耗的源字符（含被丢掉的白空格与强制换行），trace
   导出累加它。实测 `inline-text` 的 `#justify`：`[0,36,68,100]` → Chrome 的 `[0,36,68,104]`。
2. **flex/grid 的百分比基准与滚动范围（已修，`5d17cac`）。** 容器把 **padding box** 当子项与 gap 的
   百分比基准（grid 更狠，用的是它自己的包含块），并且从不填 `overflow.scrollable`，画笔只能退回
   padding box。按 LAYOUT 第 3、9 节改成「内容盒减去已留的滚动槽」当基准，规则收进
   `cache/overflow.h` 由三个算法共用。实测 `example-text` 的 `width: 100%` 面板 800 → 737，
   示例页 21 条几何差消失、无新增。
3. **父盒底边留着最后一个子项的下边距（已修，`8f9f62e`）。** CSS 2.1 §8.3.1：父盒有下边框/下内边距
   （或有 BFC、有 min-height）时，最后一个流入子项的下边距不能折出去，占的是父盒内容高。实测
   `example-flow` 430 → 294、`flow-page` 422 → 217（本轮的 341 条）。
4. **行内盒与行盒的两条（已修，`cbf6390`）。** 两条都先用新用例 `inline-metrics` 把 Chrome 的
   数字量出来（16 / 24 / 32px 的 `top` / `bottom` / `line-height: normal`），再改代码：
   - **`display: inline` 的盒子给浏览器的是字体内容区**（ascent + descent 的取整值），不是它折出来的
     行的高度；`line-height` 属于父级建的那条行盒。父级现在按子级字体度量建 piece（它与文本 piece
     同一条路径），并把子级的边框盒摆到内容区该在的位置。实测 `span#big` 20 → 33 高、y 160 → 153。
   - **半行距给 ascent 向下取整、descent 拿余数**，盒子仍正好 `line-height` 高：20px 行盒里的 33px
     字体盒从 -7 开始（不是 -6.5），行盒因此是 23 而不是 23.5。用到的 line-height 也按 1/64 向下
     取整（23.2 → 23.1875），这是 `.sample` 那种段落高度对上的原因。内容区溢出 line-box、以及后代
     的溢出，都进 `scrollable`（后者按子级书写模式换算，正交流不会被转错）。
     战绩：example-text 28 → 7、example-checks 42 → 5、inline-text 14 → 7，`inline-metrics` 绿。
5. **下面几条已定位、未修。**
   - **行内元素两侧的空格在 advance 里会丢。** `inline-text` 的 `span#sub` x 差 8.34px = 前后两个
     空格：文本子盒自己的那一趟先把行首空格丢掉（`skip_space` 里 `line.empty()`），父级再断行时那些
     字符已经不在 piece 列表里了。要修得让「测量趟丢掉的空白」以别的形式带回父级。
   - **flex 的块轴百分比基准暂不改。** 子项的百分比高度按规范该用容器内容盒，但把
     `space.percentage.block_size` 从 `content_block + pad_block` 换成 `content_block`，示例
     `example-transition` 的 `.band` 在后续帧里会停在旧位置（间隔 8px 而不是 27px）：
     `.load` 走四帧，后一帧读到前一帧留下的子项位置。这就是 12.2 那条「Final 命中缓存就不再重排
     子树」的残留，只在块轴基准上换一个数就能复现，换回去即消失。修「命中即重钉」时要连带处理。
   - **`vertical-align: middle` / `sub` / `super` 的偏移量**还在猜（`align_shift()`）：Chrome 对
     `middle` 用的是「父级基线 + 半个 x-height」，`sub` / `super` 用字体自带的偏移；新用例
     `inline-vertical-align` 就是量它们用的。
   - **`max-content` 与 `max-width` 的组合**（`example-flow` 的 `.case { width: max-content;
     max-width: 320px }`）仍是最大一坨：`.row` 的换行位置随卡片宽度走，先让卡片的 max-content
    被 max-width 夹住后重新收敛，再谈 x 差。
   - **自动高的滚动容器没有为横滚动条长高。** `example-text` / `example-checks` 的
     `.sample.nowrap`（`overflow: auto`、内容横向溢出）Chrome 报 62.1875 = 一行 23.1875 + 15 的
     横滚动条 + 24 内边距；引擎只报 47.1875。`block_scrollbar` 现在只缩小可用空间，没有加进自动高的
     使用高。

另外 `tests/layout_flow.cpp` 的 `LayoutFlow.CollapseAndFloats` 仍红：`#box` 的内容高已经对了
（100 → 110），但用例期望的是「相对父级内容盒」的 y（`a.y=30`、`child.y=0`），而 `geo.y` 是
世界坐标（`a.y=31`）。这条要单独决定：改用例口径，还是给几何另留一个相对量。

### 12.5 字体解析与 kerning 的一轮实测（本轮）

`example-*` 每个页面的文本宽度都系统性偏大约 0.4%，之前记成「两端 shaping 栈的小数漂移」。
逐项量下来是四件事，都在引擎侧，`font-kerning`（内嵌字体，必过）与两条系统字体诊断用例
（`font-vf-instance`、`font-sans-resolution`）现在把它们钉住了：

1. **通用族的映射本身是对的**，错的是这张脸没有拿到 kerning。Chrome 在本机把 `sans-serif`
   解析到 Noto Sans SC（`font-generic` 的 `#a`/`#e` 与显式 `"Noto Sans SC"` 同宽），引擎也解析到它，
   但同一串文本引擎量出 99.453、Chrome 98.9375。`AVAVAVAVAVAVA` 一比就是每个 AV 对少 0.2px：
   GPOS 的 `kern` 没取到。
2. **配对里 class 0 是合法的类，不是「没覆盖」。** PairPos format 2 的第一字形用 Coverage 表判覆盖，
   ClassDef 里没有条目的字形属于 class 0，而 class 0 的格子里有真值（Noto Sans SC 的 (A,V) 是
   类对 (0,41)、-11 单位）。原来把「ClassDef 里没有」当成「不在覆盖里」，这一对直接被跳过；
   现在按 Coverage 判覆盖、缺条目按 class 0 取值。
3. **VariationIndex 设备表在 GPOS 里，只有 ItemVariationStore 在 GDEF。** 原来两边都从 GDEF 取，
   而这张脸的 GDEF 只有 240 字节，任何偏移都越界，于是所有可变字体的配对都停在基础实例上；
   同时 `avar` 没有应用（Noto Sans SC 把 `wght:400` 的归一化 0.375 重映射到 0.39），补上之后
   wght 100/400/700 的配对值 -11 / -18.02 / -25.04 单位与 Chrome 逐个吻合。
   PairPos format 1 的 stride 原来也没算 `valueFormat2`，一并按规范补齐。
4. **`lang` 不是 kerning 开关。** 引擎原来对 `lang` 以 zh/ja/ko 开头的元素关掉 kerning；实测
   Chrome 在 `lang="zh-CN"` / `"ja"` / `"ko"` 的文档里对拉丁文本照常成对调整（`AVAV…` 三种 lang
   与 `en` 完全相同，119.84375）。现在 kerning 只由 `font-kerning` 决定：`auto`（初始值）与
   `normal` 开着，`none` 关掉；`LayoutStyle.font_kerning_none` 进哈希与比较，`font-kerning: none`
   的实测宽度（123.296875 / 101.015625）与 Chrome 完全一致。

另外一条与字体无关的解析：**不存在的族名回退到标准字体，不是回退到字形回退表。** 原来
`font-family: "Noto Sans"`（机器上没有）落到 `kFallback` 的第一项 Segoe UI（95.5），Chrome 落到
标准 sans（Noto Sans SC，98.9375）。现在请求的族名一个都没解析出来时先走 `kSansSerif`，再走
字形回退表。

战绩（同一轮，无回退）：L2 几何差 1047 → 674；`example-flow` 292 → 186、`example-style`
252 → 178、`flow-page` 215 → 107、`example-table` 61 → 23、`example-text` 7 → 1、
`example-checks` 5 → 1、`example-hello` 44 → 37、`example-studio` 44 → 38。

还差一条：**逐字形的最后一个量子**。内嵌 Noto Sans 的 `A` 浏览器给 10.234375（655 量子）、引擎给
10.21875（654），`V` 也是差一个；`AV`、`AA`、`AVAV` 这种短串因为 kerning 补偿反而完全对上，
长串就会在整数 `scroll_size` 的取整边界露出来。试过「每个字形按量子向上取整」（Blink 的
LayoutUnit 是这么构造的），反而整体偏宽 0.02–0.23px，说明浏览器用的是另一条路径
（Windows 上 DirectWrite 的 hinted advance），这条留给后面的绘制/文本精度一轮。

### 12.7 碎片化：断点、片段与 Chrome 的栏平衡

块与行内现在会消费 `FragmentationContext` 并产出 `BreakToken`，多栏按 Chromium
的算法消费它们。实测出来的几条口径写进了 `docs/LAYOUT.md`：

* 栏高初值是 `内容高 / 使用栏数`（向上取到 1/64），之后每趟按**最小 space shortage** 拉伸。用
  「内容仍能装进使用栏数的最低栏高」当目标会低估：Chrome 在 `#count3` 上给 120（不是 116.67）、
  在 `#width100` 上给 70（不是 60），差的就是断点位置限制。
* 一行盒子切不开、空片段不算前进：片段里放不下的**第一行/第一个子级**留下并让片段溢出（这一溢出
  就是拉伸量）；片段已经装了东西时，放不下的内容整块推到下一个片段。
* 片段容器剩余空间为 0 时子级直接推走（`#fixed_height` 的两个栏每栏两个 50px 子项，第三个整块进
  下一栏），不是「贴着边溢出」。
* 元素几何是**所有片段的并集**（浏览器 `getBoundingClientRect` 的口径），逐行位置取所属片段
  （第二栏的行从第二栏顶部开始）。
* 片段**里面**的盒子同样按所属片段报几何：`map_box` 整棵子树走「未分片长条」的坐标系，
  `place_in_fragments` 第二趟按每个盒子自己的块向区间把它放进对应片段（孙级也一样，例如表格
  行组里的行），跨多个片段的盒子给并集。实测：多栏里的 grid / table / 列 flex 三种容器的断点
  与片段内几何在 `multicol-fragments` 上逐字段与 Chrome 一致。
* 多栏容器问**子级**时给的是「长条」布局的键（不带自己的 `FragmentationContext`）：子级是
  整条排好、由片段裁剪的，容器自己决定在哪儿断。把容器的 token 交给子级会让子级以为自己已经
  消费过一段（grid 的第二个片段因此报出 0 高度的切片）。

### 多栏里的浮动（本轮）

容器持有一份「长条坐标系」的排除区，浮动按 `cache/float_place.h`（与块流共用的放置算术）就位、其外边距
盒进排除区，同一个栏里后面的行因此缩短；装不下当前栏剩余空间的浮动开下一栏（浮动不跨栏切开），它后面
的内容从新栏顶部开始、在它旁边；子级内部放下的浮动交回容器（CSS 2.1 第 9.5 节），所以子级之后的兄弟
也被它缩短。**浮动不进栏高平衡的基准**：它不推进块向游标，算进去会让估算偏高，于是把「本来就能装下」
的栏高当成正解（实测：一个「浮动 + 两段」的盒子修前引擎 70 / Chrome 60，修后 60）。用例
`multicol-float` 三段逐字段一致（控制组、平衡下的浮动、溢出到下一栏的浮动）。

仍与 Chrome 不同（刻意避开，不进用例）：浮动本身比栏还高时 Chrome 会把它按栏切开（元素矩形是跨栏并集），
本引擎不切，它后面的 `clear` 结果也随之不同；一行的可用宽度窄到放不下第一个字时 Chrome 把整行下移，本
引擎按字硬塞（两个浮动并排留下的窄缝就能碰到这条）。

### 已知限制

- **加粗字形。** `DisplayItem.font_weight` 来自 `LayoutStyle::font_weight`。画面由 GPU 按这个字重取 `FontChain`（600 及以上走粗体文件或可变字重轴）。
- **图片。** `DrawImage` 由产品绘制走查发出。文件打不开或解码失败在引擎里记失败，不在测试里另铺一张位图。
- **`transform` 的边框盒参加 L2。** 两端的 `border_box` 都是变换之后的边框盒（浏览器 `getBoundingClientRect`，引擎绘制走查后的世界坐标）。以前变换过的盒子整段延后，`translate` 的位置测不到。
- **`breaks` / `advances` 默认关。** 逐 cluster advance 在两端都还在探索阶段（Chrome 侧要按字符做 `Range` 二分，成本高），需要时在 `case.json` 的 `text` 里 opt-in。
- **`scroll-behavior: smooth` 与 scroll-snap 的中间动力学不比对**（第 2 节矩阵里声明放弃的范围）。

---

## 9. 怎么加一条用例

1. 建 `cases/<case>/{page.html,page.css,case.json}`。长度尽量写固定 px；要断文本就先给 `line-height` 定值。
2. 在 `case.json` 里写 `matrix`（引用 `matrix.json` 的 id）和 `required`。矩阵「一致」范围里的特性至少一条 golden 用例。
3. `python tests/difftest/run.py record --case <case>` 录 golden（会写 `golden/meta.json` 记浏览器版本与 flag 集）。
4. `python tests/difftest/run.py check --case <case>`。
5. 红了就按第 11 节分类：`input-misaligned` 改用例或驱动；`geometry` / `paint` 修引擎；要主张「策略分歧」先按第 11 节凑够证据并写 `DIVERGENCE.md`。

**没进必过集不算验收。** 必过集 = 矩阵「一致」范围每特性至少一条 golden 用例 + 全部回归用例。

---

## 10. 实现备注

- **JSON**：引擎侧用仓库里 vendored 的 `third_party/nlohmann_json`（3.11.3，之前在 `cmake/deps.cmake` 里没接线，这次补上 `nlohmann_json::nlohmann_json` 接口目标）。用 `ordered_json` 而不是默认的 map 版，trace 才按 DOM 顺序读、原始文件 diff 才有意义。Python 侧用 stdlib。
- **CDP**：`chrome/cdp.py` 自己实现 RFC 6455 里需要的那一小片（握手、掩码文本帧、分片、ping/pong），不引 Python 依赖。浏览器路径可配；本机没有 Chrome，探针和 golden 用的是 Edge 153。
- **PNG**：Python 侧 `report/png_io.py` 用 stdlib `zlib` 解浏览器截图。引擎读回的颜色由 `tests/difftest/engine/png_encode.cpp` 写成 PNG。
- **注释语言**：代码注释一律英文（`AGENTS.md`）。这不是洁癖——`tests/difftest_selfcheck/` 一带中文注释就编译失败：MSVC 按系统代码页 936 读源码，多字节序列会吞掉后面的字符，解析在离现场很远的地方报错。报告里的**输出文本**仍是中文，那是给用户看的。

---

## 13. 交互行为能测到什么（本轮加的）

三层，从「页面自己会反应」到「应用自己会反应」：

### 13.1 `dispatch` + 读回：页面级交互（L3/L5）

`dispatch` 的事件不经过任何测试专用的假路径：引擎侧是 `source/input/interaction.cpp`——App 用的
就是这一份；浏览器侧是 `Input.dispatchMouseEvent` / `Input.dispatchKeyEvent`，也就是真实输入。
每个 `dispatch` 步（和 `scroll` 步）两端各写一份 `<step>.state.json`，由 `compare_state` 对拍：

| 读回字段                | 内容                                                                               | 容差                   |
| ----------------------- | ---------------------------------------------------------------------------------- | ---------------------- |
| `head / target`         | 读回对象（默认 `documentElement`）的 DOM 路径                                      | 精确                   |
| `scroll` / `client_box` | 该对象的滚动偏移与客户区宽高                                                       | 1/64 px                |
| `scrollers`             | **页面上每个** `overflow: auto/scroll` 盒子的路径与其 scrollLeft/Top               | 路径精确、偏移 1/64 px |
| `hits`                  | 每个声明点位的 `elementFromPoint` 路径（点由用例给，缺省取最后一个鼠标事件的坐标） | 精确                   |
| `focus`                 | `document.activeElement` 的路径                                                    | 精确                   |
| `checked`               | 每个 checkbox / radio 的路径与 `checked`                                           | 精确                   |

`scrollers` 那一项是白拿的：一次滚轮之后，页面上所有滚动容器的偏移一起被对拍，不必为每个容器
写一条断言。两侧的收集规则必须一致——浏览器侧 `walkScrollers` 跳过 `display: none` 子树（引擎
那边这种盒子根本不存在）与 `display: contents`（它自己不生成盒子），否则隐藏抽屉里的滚动容器
会凭空多出来一串。

已入库的原语用例（TESTPLAN 第 7 节那张表）：`hit-click`、`hit-points`、`hover-style`、
`wheel-delta`、`wheel-chain`、`thumb-drag`、`track-click`、`key-scroll`、`scroll-clamp`、
`scrollbar-reflow`、`focus-visible`、`overscroll-contain`、`stacking-z-index`。

### 13.2 示例应用族：`example-album` … `example-zdb`

`example/tests/` 那几条是单页单特性；`example/applications/` 的九个应用（album、calendar、chat、
contacts、file-explorer、kanban、reader、zdb、zmd）现在是**整壳进对照**：18 条 `example-*` 用例，
每条 3–4 个状态 × (trace + capture)。

应用页的 DOM 是脚本铺的，引擎没有 JS，所以：

1. 用例写 `"scripts": false"` 与 `"source": "<应用页>"`，harness 把**同一份**去掉 `<script>` 的页面
   写进用例目录（`page.html`，相对 URL 重写到示例目录），两端加载同一份文件；
2. 状态用动作 DSL 摆出来：示例自己的模板类（`.album-btn` / `.agenda-item` / `.conv` / `.person` /
   `.column` / `.zrow`）插进去，示例自己的开关（radio/checkbox 用 `set_checked`）拨上去，
   `hidden` 的对话框用 `remove_attr hidden` 打开；
3. 交互用 `dispatch`：hover + 按下应用主操作按钮（命中与焦点读回）、在**真的会溢出**的容器上滚轮、
   Tab/Escape。

坐标不是猜的：先用 `zui_dump --brief` 量出引擎自己的顶部按钮盒，取中心写进用例，再用录下来的
golden 里那份 `state.json` 的 `hit` 路径确认那一点在浏览器里命中的就是同一个控件（本轮七个应用
逐个确认过）。

生成的案例脚本是 `build/mkcases.py`（不在版本库里，改完重跑再 `record` 就行）。

### 13.3 应用自己（C++）的交互：`zui_app_interact` 与 ReaderOracle

`zui_difftest` 不链 `App`，所以 `App::bind` 的 C++ 处理器在 difftest 里是测不到的。工作区里有两条
现成的路：

- **`zui_app_interact`**（`tests/app_interact/`，ctest 里跑）：无窗口地驱动应用自己的 session——
  C 档 `frame::Access::prepare_interaction()` + `click_id` / `click_xy` / `wheel_id` / `key` /
  `interactive_ids` / `box_rect` / `box_center` / `measure` / `get` / `set`，对象是 album / calendar / chat / contacts / kanban / zmd / zdb /
  reader 各自的 session。断言的是**应用状态机**（点了之后 overlay 可见、文本变没变、卡片在第几列），
  不是与浏览器对拍。
- **`reader_oracle.py`**（`tests/interact/`，ctest 的 `ReaderOracle.Shell`）：唯一一条**以浏览器为
  基准**的应用级对照——同一个场景文件（`tests/interact/scenarios/*.json`）在 Chrome 里跑
  `reader.js`、在 `zui_reader_oracle` 里跑 C++ session，两边都写出 mark → id → 字段的记录再 diff。
  想给别的应用加交互对照，这就是模板：一份页面脚本 + 一份 C++ session + 一份场景。

### 13.4 还没做的

- `dispatch` 到页面级交互为止：点击能改的是 CSS 状态（`:hover` / `:checked` / `:focus` / 滚动），
  不会调用页面的 `onclick`（那要 JS）。应用级的「点了会怎样」用 13.3 的两条路。
- 应用族的 golden 现在是**红的**，而且是真差异不是噪声（见 `build/difftest/run/report.html`）：
  最上游的一条是 `input[type=file]` 的 UA 边框——引擎给它 1px 边框 + `padding: 1px 2px`（Chrome
  实测 0 / 0），SR-only 的 `#file` 因此是 2×2 而不是 1×1，页面块向多出 2px，根元素于是两根滚动条
  都留了槽（读回 1085×705 对 1100×720），后面所有命中、滚动量、`client_box` 都被它带偏。
- L1 在两个应用族上是「引擎给指定值 / Chrome 给使用值」的差（`width: auto` vs `86px`、
  `line-height: 1.5` vs `22.5px`、`display: inline-block` vs 块化后的 `block`、初始值表缺失的
  `text-overflow: clip` / `grid-template-columns: none`）。这是一轮独立的归一化工作，不要在现场
  把属性从 `style_props` 里删掉消红。
