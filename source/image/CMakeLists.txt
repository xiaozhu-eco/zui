# Image facade, SVG scene, the CPU SVG parser, and the GPU SVG devices'
# CPU-side partners. PNG, JPEG, and SVG driver registration, plus the SVG
# software-raster fallback, live in plugins/image. The parser is compiled
# here so scene.cpp resolves it inside this static library. The SVG driver
# calls Parser::parse; it does not compile the parser.

add_library(zui_image STATIC
  driver.cpp
  image.cpp
  svg/gpu_decode.cpp
  svg/scene.cpp
  svg/parser/color.cpp
  svg/parser/document.cpp
  svg/parser/gradient.cpp
  svg/parser/group.cpp
  svg/parser/image_node.cpp
  svg/parser/length.cpp
  svg/parser/node.cpp
  svg/parser/paint.cpp
  svg/parser/parser.cpp
  svg/parser/path_data.cpp
  svg/parser/shape.cpp
  svg/parser/shape_lowerer.cpp
  svg/parser/style_resolver.cpp
  svg/parser/transform.cpp
  svg/parser/viewport.cpp
  svg/parser/xml_scanner.cpp
  svg/render/device.cpp
  svg/render/frame_plan.cpp
  svg/render/runtime.cpp
  svg/render/unsupported_device.cpp
)
target_include_directories(zui_image PUBLIC
  "${ZUI_ROOT}/source"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
)

zui_add_interface_sources(zui_src_svg_gpu
  svg/render/gl_programs.cpp
  svg/render/gl_svg_device.cpp
  svg/render/vk_programs.cpp
  svg/render/vk_storage.cpp
  svg/render/vk_svg_device.cpp
  svg/render/svg_probe.cpp
)
