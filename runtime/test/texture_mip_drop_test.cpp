// leonida::texture_mip_drop (src/texture_mip_drop.h): the VIctory fork's VICTORY_TEXTURE_DROP_MIPS eligibility
// rules and the LEONIDA_TEXTURE_DROP_MIPS parser. No GPU, no device.
#include "../src/texture_mip_drop.h"

#include <cstdint>
#include <cstdio>

#define EXPECT(condition) do { \
    if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } \
} while (0)

namespace {

using leonida::parse_texture_mip_drop;
using leonida::texture_mip_drop;
using leonida::TextureMipDrop;

TextureMipDrop base() {
    TextureMipDrop t;
    t.two_dimensional = true;
    t.title_filled = true;
    t.width = 1024;
    t.height = 1024;
    t.mip_levels = 11;
    return t;
}

int eligibility() {
    EXPECT(texture_mip_drop(base(), 0) == 0);
    EXPECT(texture_mip_drop(base(), 1) == 1);
    EXPECT(texture_mip_drop(base(), 2) == 2);
    EXPECT(texture_mip_drop(base(), 4) == 4);
    EXPECT(texture_mip_drop(base(), 9) == 4);  // clamped to the fork's ceiling
    TextureMipDrop t = base();
    t.mip_levels = 3;
    EXPECT(texture_mip_drop(t, 4) == 2);  // at most mip_levels - 1
    t.mip_levels = 1;
    EXPECT(texture_mip_drop(t, 4) == 0);
    t = base();
    t.two_dimensional = false;
    EXPECT(texture_mip_drop(t, 1) == 0);
    t = base();
    t.title_filled = false;  // render target, depth or UAV
    EXPECT(texture_mip_drop(t, 1) == 0);
    t = base();
    t.multisampled = true;
    EXPECT(texture_mip_drop(t, 1) == 0);
    t = base();
    t.width = 256;
    EXPECT(texture_mip_drop(t, 1) == 0);  // shorter side under 512
    t = base();
    t.width = 1002;
    EXPECT(texture_mip_drop(t, 1) == 0);  // 1002 % 8 != 0
    t = base();
    t.width = 1000;
    EXPECT(texture_mip_drop(t, 1) == 1);  // 1000 % 8 == 0
    EXPECT(texture_mip_drop(t, 2) == 0);  // 1000 % 16 != 0
    t = base();
    t.width = 512;
    t.height = 512;
    t.mip_levels = 6;
    EXPECT(texture_mip_drop(t, 1) == 1);
    return 0;
}

int parsing() {
    EXPECT(parse_texture_mip_drop(nullptr) == 0);
    EXPECT(parse_texture_mip_drop("") == 0);
    EXPECT(parse_texture_mip_drop("2") == 2);
    EXPECT(parse_texture_mip_drop(" 2") == 0);
    EXPECT(parse_texture_mip_drop("2x") == 0);
    EXPECT(parse_texture_mip_drop("9") == 4);  // clamped
    return 0;
}

}  // namespace

int main() {
    if (int r = eligibility()) return r;
    if (int r = parsing()) return r;
    std::fprintf(stderr, "texture_mip_drop: ok\n");
    return 0;
}
