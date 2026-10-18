# transform-origin —— 变换感知进度

本用例已进必过集（`required: true`）：L2 对齐 Chrome 的变换后 `getBoundingClientRect` AABB。

## 已落地

1. **变换后 AABB**：`paint_map` 对 border-box 四角做 `to_world`，写入 `geo.aabb_*`；trace 的 `border_box` 导出 AABB。
2. **滚动吸收变换溢出**：子级 AABB / scroll 范围并进祖先 `scroll_width` / `scroll_height`（Chrome 会因旋转伸出布局盒而增大 `scrollHeight`）。
3. **图元变换**：`Box::paint_*` Aff2。GPU 按 Aff2 逆映射画填充。
4. **非轴对齐 overflow clip**：`push_clip_xform`。GPU 在着色器里用 Aff2 逆映射裁切，不是轴对齐剪刀。

## 仍后置

* 圆角 Aff2、尖角 Aff2 FillRect、Aff2 文字和图像另有用例（`paint-transform-radius` / `paint-transform-fill` / `paint-transform-text`）。

## 参考

`现有代码和css规范的差距.md` B3/A6 GPU transform；用例 `paint-transform-fill` / `paint-transform-text`。
