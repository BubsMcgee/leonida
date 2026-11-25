#pragma once
// How many of a title texture's largest levels its host image leaves out, after the VIctory fork's
// VICTORY_TEXTURE_DROP_MIPS (crates/victory-gpu/src/resource.rs, drop_levels; GPL-3.0-or-later, as is this tree).
//
// A level left out halves both sides. The host image is made at width >> N, height >> N with mip_levels - N, and
// title level m is host level m - N texel for texel, so a view shifts its levels down by N and an upload skips the
// levels left out. Only a 2D texture the title fills (no render-target, depth-stencil or unordered-access flag),
// single-sampled, with a shorter side of at least 512 texels and both sides divisible by 4 << N is eligible: those
// dimensions keep whole 4-texel blocks (the block-compressed formats), and each level left out divides the
// texture's bytes by about four.
#include <algorithm>
#include <cstdint>

namespace leonida {

struct TextureMipDrop {
    bool two_dimensional = false;
    bool title_filled = false;  // no D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET / _DEPTH_STENCIL / _UNORDERED_ACCESS
    bool multisampled = false;
    std::uint32_t width = 0, height = 0;
    std::uint32_t mip_levels = 1;
};

// `requested` is LEONIDA_TEXTURE_DROP_MIPS, clamped to `max_drop` (the fork's ceiling is 4). 0 when the texture is
// not eligible or there is no level to leave out.
inline std::uint32_t texture_mip_drop(const TextureMipDrop& texture, unsigned requested, unsigned max_drop = 4) {
    const unsigned wanted = std::min(requested, max_drop);
    if (!wanted || !texture.two_dimensional || !texture.title_filled || texture.multisampled) return 0;
    const std::uint32_t mips = std::max<std::uint32_t>(texture.mip_levels, 1);
    const unsigned drop = std::min(wanted, mips - 1);
    if (!drop) return 0;
    if (std::min(texture.width, texture.height) < 512) return 0;
    const std::uint64_t block = std::uint64_t(4) << drop;
    if (texture.width % block || texture.height % block) return 0;
    return drop;
}

// LEONIDA_TEXTURE_DROP_MIPS: a decimal count, 0..4. Empty or absent is 0, and text that is not a number is 0.
inline unsigned parse_texture_mip_drop(const char* text) {
    if (!text || !*text) return 0;
    unsigned value = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < '0' || *c > '9') return 0;
        value = std::min(4u, value * 10 + unsigned(*c - '0'));
    }
    return value;
}

}  // namespace leonida
