#ifndef ZUI_PROBE_PROBE_H
#define ZUI_PROBE_PROBE_H

// Test probes. Not an application header: nothing under include/zui/ may include this.
//
// ZUI_PROBE        observation and automation seams (frame::Access, style and geometry readback).
// ZUI_PROBE_FRAME  framebuffer readback after the OpenGL or Vulkan renderer has drawn.
//                  CMake sets it to 1 together with ZUI_PROBE. The read is
//                  IRenderer::read_frame, from an offscreen target.
//                  A test must not rasterize the display list itself when this is 0.
//
// CMake option ZUI_PROBE sets the first macro (on unless the build type is Release).
// A translation unit compiled without that definition follows NDEBUG.

#ifndef ZUI_PROBE
#  if defined(NDEBUG)
#    define ZUI_PROBE 0
#  else
#    define ZUI_PROBE 1
#  endif
#endif

#ifndef ZUI_PROBE_FRAME
#  define ZUI_PROBE_FRAME 0
#endif

static_assert(ZUI_PROBE == 0 || ZUI_PROBE == 1, "ZUI_PROBE is 0 or 1");
static_assert(ZUI_PROBE_FRAME == 0 || ZUI_PROBE_FRAME == 1, "ZUI_PROBE_FRAME is 0 or 1");

#endif
