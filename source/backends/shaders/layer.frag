#version 450
// Layer program. Samples the mask and writes it for the premul pipeline blend
// (ONE, ONE_MINUS_SRC_ALPHA). Filter-layer blend modes on GL live in that backend's
// layer body. This shader does not evaluate shadow coverage or glyph winding.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 o;
layout(binding = 0) uniform sampler2D uTex;

void main() {
    o = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0);
    if (vCol.a < -1.0) {
        o = vec4(vUv, 0.0, 0.0);
    }
}
