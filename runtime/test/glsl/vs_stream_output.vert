#version 460
// Minimal vertex shader for d3d12_stream_output_test (D3D-302): writes the fixed vec4 (1, 2, 3, 4) to both its
// Xfb-captured output (buffer 0, offset 0, stride 16 bytes: one vec4) and gl_Position. The rasterized output is
// unused by the test; only the captured buffer's contents, read back after the draw, are checked.

layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 out_val;

void main() {
    out_val = vec4(1.0, 2.0, 3.0, 4.0);
    gl_Position = out_val;
}
