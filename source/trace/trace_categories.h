#ifndef ZUI_TRACE_TRACE_CATEGORIES_H
#define ZUI_TRACE_TRACE_CATEGORIES_H

// The category registry for the Perfetto Tracing SDK. Only `source/trace/trace.cpp` includes this
// (it is the translation unit that also reserves the registry's storage), so the 8 MB SDK header
// stays out of every other file in the tree.
//
// The names are part of the interface, not an implementation detail: they are what the trace
// carries in `slice.category`, what `tools/trace/queries/*.sql` filters on, and what
// `ZUI_TRACE_CATEGORIES` selects. Renaming one breaks the query set.

#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("app").SetDescription("Frame driver stages"),
    perfetto::Category("engine").SetDescription("Layout engine passes"),
    perfetto::Category("layout.phase").SetDescription("Per-box cache lookup and store"),
    perfetto::Category("layout.algo").SetDescription("Per-box formatter"),
    perfetto::Category("layout.inner").SetDescription("Second-order layout sub-phases"),
    perfetto::Category("parse").SetDescription("HTML and CSS parsing and tree building"),
    perfetto::Category("text").SetDescription("Text shaping, line breaking and glyph runs"),
    perfetto::Category("image").SetDescription("Image and SVG decode"),
    perfetto::Category("input").SetDescription("Event dispatch and hit testing"),
    perfetto::Category("gpu").SetDescription("Backend submit, upload and present"));

#endif  // ZUI_TRACE_TRACE_CATEGORIES_H
