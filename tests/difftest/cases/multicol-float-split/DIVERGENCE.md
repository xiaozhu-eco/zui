# multicol-float-split —— 已收口

比栏还高的浮动按栏切开（CSS Multi-column §7 + CSS 2.1 §10.6.7）。

## 收口方式

1. **子级栏数 / 空尾片段**：`multicol.cpp` 在内容分片之后，按子级上报的浮动带 `float_end` 把最后一片伸到栏边，并开高度可为 0 的尾片段；不推进内容游标、不抬 `pass.bottom`（避免挤走后续兄弟或抬高 auto 平衡）。
2. **覆盖高度 vs 可视高度**：`FragmentBox::coverage_height` 保留整栏带给子孙切片；`height` 按内容盒裁切（Chrome 的空尾 `client_rects` 为 0）；零面积片段进 `client_rects` 但不进 `getBoundingClientRect` 并集。
3. **非 BFC 的 `scrollHeight`**：`normal_flow.cpp` 把 `float_bottom` 折进 `overflow.scrollable`（浮动不进高度、进可滚动溢出）。

## 验收

- 用例 `multicol-float-split` L2/L4 **绿**（`required: true`）
- 回归：`multicol-float` / `multicol-block-float` 仍绿
- 自检 `MulticolFloatSplitAcrossColumns`
