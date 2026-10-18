#ifndef ZUI_SOURCE_CSS_ENGINE_INTERNAL_H
#define ZUI_SOURCE_CSS_ENGINE_INTERNAL_H

// Internal API shared by the css engine's translation units. Types are grouped by phase in
// sibling headers so this file stays under the module line cap; existing
// `#include "css/engine/cascade_internal.h"` keeps working.

#include "css/engine/cascade_decl.h"
#include "css/engine/cascade_share.h"
#include "css/engine/cascade_walk.h"

#endif
