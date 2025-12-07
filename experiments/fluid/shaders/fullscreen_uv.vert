#version 450

layout(location = 0) out vec2 vUV;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    // Flip Y so that increasing world Y (camera moving up)
    // corresponds to features moving down in the window.
    vUV = vec2(pos.x * 0.5 + 0.5, -pos.y * 0.5 + 0.5);
}
