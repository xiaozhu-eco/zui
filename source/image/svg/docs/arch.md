# SVG 渲染

定稿。`.svg` 的实现以本文为准。`image.h` 仍是门面：`load` / `invalidate` 的签名不变，调用方拿到的仍是一张 RGBA8 `Bitmap`。

## 边界

CPU 只解析 SVG 文件。解析结果收成 `source/image/svg/types/gpu_layout.h` 里的数据结构，上传给 GPU。曲线离散、填充覆盖、描边距离、渐变取样、图片取样、分组合成都在计算着色器里完成。

渲染写进离屏缓冲，读回一张静态 RGBA8，按路径放进 `image::load` 的缓存。主绘制只把这张图当 `DrawImage` 贴上，不往 `DisplayList` 加路径图元。`invalidate` 丢掉这张缓存。

依赖的 PNG / JPEG 走现有 driver 解码，再作为纹理绑进合成 pass。一张 SVG 只栅格化一次。分辨率取 `width` / `height`；只有 `viewBox` 时用 `viewBox` 的宽高。边长和像素上限沿用 `dimensions_ok`（边长 ≤ 32768，像素 ≤ 1e8）。`viewBox` 到像素的仿射乘进上传的控制点。

分组 `opacity` 不是 1 时，子树先画进单独的缓冲，再按透明度合成回来。这是 multipass。元素自身的 `opacity`、`fill-opacity`、`stroke-opacity` 写进该图元的数据。

## 没有计算着色器就不支持 SVG

用户选中的后端如果没有计算着色器，`decode` 失败，`error` 固定为：

```text
svg is not supported on this backend
```

不在 CPU 上补一套光栅。判断按当前上下文，不看文件内容：

| 后端 | 现在的上下文 | SVG |
| --- | --- | --- |
| Vulkan | 图形队列带 `COMPUTE` | 按本文的管线画 |
| OpenGL | 窗口要的是 4.3 core。本管线的着色器是 `#version 450`，所以要 4.5 及以上才算支持 | 4.3 明确不支持 |
| OpenGL ES | 窗口要的是 3.0。计算着色器要 3.1，`#version 450` 在这条上下文上不成立 | 明确不支持 |

解析失败仍是解析错误，不把无计算着色器的那句拿来代替。编码不实现，`encode` 返回 `svg encode is not supported`。

## 管线

着色器在 `shaders/`。每个 pass 是一次 compute dispatch。

1. `flatten.comp` 把每条二次或三次曲线离散成固定 16 段折线。直线只保留第一段，其余写成长度为 0 的段，光栅 pass 会跳过。
2. `clear.comp` 把当前图层缓冲清成透明。
3. `raster.comp` 对一个图元做非零环绕或奇偶填充，并按折线距离描边。每个像素用 4×4 盒式采样把填充和描边收成覆盖率，再源叠。渐变在这个 pass 里按像素中心取样。
4. `composite_image.comp` 把一张依赖纹理按矩形源叠到当前缓冲。
5. `composite_layer.comp` 把子图层按分组透明度源叠回父缓冲。

像素缓冲的第 0 行是图像顶端，读回不再翻转。

## 这一版解析的范围

元素：`svg`、`g`、`defs`、`rect`、`circle`、`ellipse`、`line`、`polyline`、`polygon`、`path`、`image`、`linearGradient`、`radialGradient`、`stop`。未知元素若有子节点，按分组留着，避免把子图丢掉。

属性：`id`、`transform`、`opacity`、`fill`、`stroke`、`fill-opacity`、`stroke-opacity`、`stroke-width`、`fill-rule`、`style` 里的同名项、渐变的 `gradientUnits` 和端点、`image` 的 `href` / `xlink:href`。表示属性按 SVG 继承；`style` 盖过表示属性。弧线命令在解析时收成三次贝塞尔。

不展开：`use`、`text`、`clipPath`、`mask`、滤镜、SMIL、CSS 动画、样式表、标记。没有动画时间轴，也不按盒子尺寸重画。

## 类

一个类一个 `.h` / `.cpp`，不要把解析、打包、设备绑到同一个类型里。

| 类 | 文件 | 职责 |
| --- | --- | --- |
| `Length` | `parser/length` | 读出以 px 为单位的数 |
| `Color` | `parser/color` | `#rgb` / `#rrggbb` / `rgb()` / `none` |
| `Transform` | `parser/transform` | 仿射与 `transform` 属性。函数从右向左作用 |
| `PathData` | `parser/path_data` | `d` 收成直线、二次、三次 |
| `Paint` | `parser/paint` | 无、纯色、或 `url(#id)` |
| `Gradient` | `parser/gradient` | 线性或径向渐变和色标 |
| `Node` | `parser/node` | 节点基类 |
| `Group` | `parser/group` | 子节点。`defs` 不进入绘制 |
| `Shape` | `parser/shape` | 一条路径加填充和描边 |
| `ImageNode` | `parser/image_node` | 外部图片引用 |
| `Document` | `parser/document` | 视口、根、渐变表 |
| `XmlScanner` | `parser/xml_scanner` | 标签和属性 |
| `StyleResolver` | `parser/style_resolver` | 继承后的表示属性 |
| `ShapeLowerer` | `parser/shape_lowerer` | 基本形状收成 `PathData` |
| `Viewport` | `parser/viewport` | `viewBox` 到像素的仿射，默认 `xMidYMid meet` |
| `Parser` | `parser/parser` | 字节到 `Document` |
| `Sprite` | `types/sprite` | 一张依赖图及其像素范围 |
| `Scene` | `scene` | `Document` 到 GPU 缓冲 |
| `FramePlan` | `render/frame_plan` | 把场景收成 compute dispatch 列表 |
| `Device` | `render/device` | 后端是否有计算着色器，以及一次渲染 |
| `Runtime` | `render/runtime` | 当前进程绑着的 `Device` |
| `UnsupportedDevice` | `render/unsupported_device` | 固定返回不支持 |
| `SvgDriver` | `driver` | 后缀 `svg` 的 `Driver` |
| `GlPrograms` / `GlSvgDevice` | `render/gl_programs` / `render/gl_svg_device` | OpenGL 计算管线 |
| `VkStorage` | `render/vk_storage` | 一块 Vulkan 缓冲 |
| `VkPrograms` / `VkSvgDevice` | `render/vk_programs` / `render/vk_svg_device` | Vulkan 计算管线 |

`Scene` 在 CPU 上做的是打包：继承后的画笔、乘完的控制点、图元顺序、图层 push/pop、依赖位图的解码。曲线求值和像素覆盖不在这里。
