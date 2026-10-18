#version 450
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vCol;
layout(push_constant) uniform PC {
    vec2 screen;
    int mode;
    int round_count;
    vec4 round_rect[4];
    vec4 round_r;
    vec4 shape;
    vec4 shape_radii;
    float shape_stroke;
} pc;
void main() {
    float x = aPos.x / pc.screen.x * 2.0 - 1.0;
    float y = aPos.y / pc.screen.y * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    vUv = aUv;
    vCol = aCol;
}
