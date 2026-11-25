// The console stream's integer-target blend bypass (D3D-INTEGER-TARGET-BLEND-BYPASS, ported from VIctory ddb7332):
// shared and independent blend states, the write masks kept on every target, and only an integer target's enable bit
// cleared. Synthetic state only: no game data, no D3D12.
#include "../src/native_pso.h"

#include <cstdio>

namespace np = leonida::native_pso;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// DXGI_FORMAT values (dxgi.h): R32_FLOAT, B4G4R4A4_UNORM and R8_UINT.
constexpr std::uint8_t R32_FLOAT = 41, B4G4R4A4_UNORM = 115, R8_UINT = 62;

np::TargetBlend on(std::uint8_t mask) {
    np::TargetBlend t;
    t.enable = true;
    t.write_mask = mask;
    return t;
}

void shared() {
    std::array<np::TargetBlend, 8> targets{};
    targets[0] = on(0xf);
    targets[1] = on(0x3);  // not read while the state is shared
    targets[2] = on(0x0);
    bool independent = false;
    const unsigned cleared =
        np::bypass_integer_target_blend(targets, independent, 3, {R32_FLOAT, B4G4R4A4_UNORM, R8_UINT});
    check(independent, "three shared targets become independent");
    check(targets[0].enable && targets[0].write_mask == 0xf,
          "target 0 (R32_FLOAT) keeps blending and its mask");
    check(targets[1].enable && targets[1].write_mask == 0xf,
          "target 1 gets target 0's state, mask included");
    check(!targets[2].enable && targets[2].write_mask == 0xf,
          "target 2 (R8_UINT) keeps the shared mask and stops blending");
    check(cleared == 1, "one target's enable bit was cleared");
}

void independent() {
    std::array<np::TargetBlend, 8> targets{};
    targets[0] = on(0xf);
    targets[1] = on(0xf);
    targets[2] = on(0x1);
    bool independent = true;
    const unsigned cleared =
        np::bypass_integer_target_blend(targets, independent, 3, {R32_FLOAT, B4G4R4A4_UNORM, R8_UINT});
    check(independent, "an independent state stays independent");
    check(targets[0].enable && targets[0].write_mask == 0xf, "target 0 keeps blending and mask 0xf");
    check(targets[1].enable && targets[1].write_mask == 0xf, "target 1 keeps blending and mask 0xf");
    check(!targets[2].enable && targets[2].write_mask == 0x1,
          "only the integer target stops blending, and keeps mask 0x1");
    check(targets[3].write_mask == 0xf && !targets[3].enable, "a target past the count is not touched");
    check(cleared == 1, "one target's enable bit was cleared");
}

void single_and_already_off() {
    std::array<np::TargetBlend, 8> one{};
    one[0] = on(0x7);
    bool independent = false;
    check(np::bypass_integer_target_blend(one, independent, 1, {R8_UINT}) == 1 && !one[0].enable &&
              one[0].write_mask == 0x7 && !independent,
          "one integer target: blending off, mask kept, still not independent");

    std::array<np::TargetBlend, 8> off{};
    off[0] = on(0xf);
    off[1].enable = false;
    off[1].write_mask = 0x3;
    bool still_shared = true;
    check(np::bypass_integer_target_blend(off, still_shared, 2, {R32_FLOAT, R8_UINT}) == 0 &&
              !off[1].enable && off[1].write_mask == 0x3,
          "an integer target that already has blending off clears nothing");
}

void shared_blending_off_is_left_alone() {
    std::array<np::TargetBlend, 8> targets{};
    targets[0].write_mask = 0xf;
    targets[1].write_mask = 0x3;
    bool independent = false;
    check(np::bypass_integer_target_blend(targets, independent, 2, {R32_FLOAT, R8_UINT}) == 0 && !independent &&
              !targets[0].enable && targets[1].write_mask == 0x3,
          "a shared state with blending off is not made independent");
}

void float_targets_untouched() {
    std::array<np::TargetBlend, 8> targets{};
    targets[0] = on(0xf);
    targets[1] = on(0x3);
    bool independent = true;
    check(np::bypass_integer_target_blend(targets, independent, 2, {R32_FLOAT, B4G4R4A4_UNORM}) == 0 &&
              targets[0].enable && targets[1].enable && targets[1].write_mask == 0x3,
          "no integer target: both keep blending");

    std::array<np::TargetBlend, 8> shared{};
    shared[0] = on(0xf);
    shared[1] = on(0x3);
    bool still_shared = false;
    check(np::bypass_integer_target_blend(shared, still_shared, 2, {R32_FLOAT, B4G4R4A4_UNORM}) == 0 &&
              !still_shared && shared[0].enable && shared[1].enable,
          "no integer target in a shared state: nothing is expanded");
}

}  // namespace

int main() {
    shared();
    independent();
    single_and_already_off();
    shared_blending_off_is_left_alone();
    float_targets_untouched();
    std::printf("%s\n", g_failures ? "FAILED" : "all passed");
    return g_failures ? 1 : 0;
}
