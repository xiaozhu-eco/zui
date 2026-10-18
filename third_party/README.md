# 本地依赖源码

全部在本目录（或 junction）编译，不走系统包、不 FetchContent / CPM。

策略与 xz-assistant/third_party 相同。

| 目录 | 库 | 说明 |
| --- | --- | --- |
| SDL/ | SDL3 | junction → xz-assistant/third_party/SDL |
| freetype/ | FreeType 2.13 | vendored；量字与覆盖位图（`FT_LOAD_RENDER`）；`FT_DISABLE_HARFBUZZ` 保持 ON |
| harfbuzz/ | HarfBuzz 8.5.0 | vendored；`hb_shape` + hb-ft 供行内 advance/kerning（见 `VENDOR.txt`） |
| glfw/ | GLFW | window backend; `ZUI_WITH_GLFW` |
| lexbor/ | lexbor 3.1 | HTML5 + CSS Syntax/Selectors；Apache-2.0 |
| zlib/ | zlib 1.3.2 | vendored；libpng 的 deflate。见 `VENDOR.txt` |
| libpng/ | libpng 1.6.59 | vendored；PNG 读写。`image::load` 按 `.png` 后缀选用 |
| libjpeg-turbo/ | libjpeg-turbo 3.2.0 | vendored；JPEG 读写。`.jpg` 和 `.jpeg` 共用一个 driver。`WITH_SIMD` 关闭（不依赖 NASM） |
| googletest/ | GoogleTest 1.18.0 | gtest + gmock；测试由 ctest 跑 |
| perfetto/ | Perfetto Tracing SDK v58.2 | amalgamation（`perfetto.h` + `perfetto.cc`）；Apache-2.0；`ZUI_WITH_PERFETTO`，只有 `zui` 和 `zui_dump` 链它 |
| spdlog/ | spdlog 1.15.1 | vendored；内部调试日志。仅 Debug 与 RelWithDebInfo 编译为 `zui_spdlog`（命名空间 `zui_spdlog` / `zui_fmt`）。`ZUI_*` 走 stderr 彩色控制台 sink，`ZUI_FILE_*` 走 basic file sink。Release 不编译。应用不包含、不链接它 |

UI 文档不使用 pugixml。`cmake/deps.cmake` 不配置它。

接入见 cmake/deps.cmake。
