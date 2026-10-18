#version 450
// Fullscreen triangle for a shadow gaussian or blit. No vertex buffer.
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vCol;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    vUv = p;
    vCol = vec4(1.0);
}
