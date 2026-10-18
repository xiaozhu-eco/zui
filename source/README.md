# source/

Internal modules of the `zui` target. See `docs/ARCHITECTURE.md`.
Each folder has a `CMakeLists.txt`. `add_subdirectory` in this directory is bottom-up.

```
log/        internal debug log (Debug and RelWithDebInfo: ZUI_* stderr color console sink, ZUI_FILE_* file sink; empty macros otherwise)
trace/      Perfetto / no-op trace macros
perf/       Debug work-unit counters (FrameWork)
image/      file suffix → RGBA8 Bitmap
text/       font chain, hb_shape, coverage bitmaps
html/       Node / Document; HTML5 via lexbor
css/        stylesheet parse, match, cascade, transitions
layout/     retained box tree; lower, flow, flex, grid, hit-test
paint/      box tree → DisplayList (not a second scene graph)
input/      pointer / keyboard / scroll / focus
backends/   IWindow (sdl3/glfw3) + IRenderer (vk/gl/gles)
app/        frame loop, bind tables, load/set
probe/      ZUI_PROBE macros (not a library)
```

Private include path only. Nothing here is installed.

Inside `html/`, `css/`, `layout/`, `paint/`, and `input/`, one algorithm per
translation unit (see `layout/flex/`) and a 700-line cap. Details:
`docs/ARCHITECTURE.md` §9.
