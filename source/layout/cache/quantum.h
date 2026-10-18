#ifndef ZUI_LAYOUT_CACHE_QUANTUM_H
#define ZUI_LAYOUT_CACHE_QUANTUM_H

#include <cmath>
#include <cstdint>

namespace zui {
namespace layout {

constexpr float kQuantum = 1.0f / 64.0f;

inline float quantize(float v) { return std::round(v / kQuantum) * kQuantum; }

inline std::int64_t quantum_key(float v) { return std::llround(static_cast<double>(v) * 64.0); }

}  // namespace layout
}  // namespace zui

#endif
