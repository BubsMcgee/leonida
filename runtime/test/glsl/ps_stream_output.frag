#version 460
// Trivial pixel shader for d3d12_stream_output_test (D3D-302): a render target is still needed so the draw has a
// valid area/layer count to rasterize into, but its colour is never checked.

layout(location = 0) out vec4 target;

void main() {
    target = vec4(1.0, 0.0, 0.0, 1.0);
}
