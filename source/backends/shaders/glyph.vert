#version 450
// One quad per glyph. `aEm` is the em-space corner (y up). Scale, sideways orientation, and an
// Aff2 are one matrix; the fragment shader interpolates em and evaluates the outline.
layout(location = 0) in vec2 aEm;
layout(location = 1) in vec4 aCol;
layout(location = 2) in uvec2 aRange;
layout(location = 3) in vec3 aM0;
layout(location = 4) in vec3 aM1;
layout(location = 0) out vec2 vEm;
layout(location = 1) out vec4 vCol;
layout(location = 2) flat out uvec2 vRange;
// Only the screen size is read here. The fragment stage owns the rest of the 256-byte block
// (the same layout as ui.frag). Declaring those members here would put `shape_stroke` on top
// of the fragment's `shape_radii_y`.
layout(push_constant) uniform PC {
    vec2 screen;
} pc;
void main() {
    vec2 s = vec2(dot(vec3(aEm, 1.0), aM0), dot(vec3(aEm, 1.0), aM1));
    float x = s.x / pc.screen.x * 2.0 - 1.0;
    float y = s.y / pc.screen.y * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    vEm = aEm;
    vCol = aCol;
    vRange = aRange;
}
