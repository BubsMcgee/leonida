# frankenstein (VIctory NVIDIA 8 GB fork) → leonida pipeline map

Reference: `https://github.com/barkodr/frankenstein`, cloned read-only at
`C:\Users\alexb\AppData\Local\Temp\opencode\frankenstein`. It is the VIctory NVIDIA 8 GB / Wave32 fork:
Rust, GPL-3.0-or-later, running the console title on **host D3D12** (`crates/victory-gpu`, `victory-shader`,
`victory-pipeline`, `victory-hle`, `victory-loader`). Our leonida is C++ on **host Vulkan**, so nothing is a
drop-in; the behaviour and the switch names are what port. Our tree is GPL-3.0-or-later too, so the code can be
adapted with attribution.

The fork's own state (README, 2026-11-25): first room renders at 2–5 fps, RT shadows/bounce light missing,
`VICTORY_TEXTURE_DROP_MIPS=2` is what stops 8 GB VRAM exhaustion, `VICTORY_LEVELS_ON_DEMAND=1` keeps ~3.1 GB of
mip levels off the card.

## Feature map

| frankenstein | Where | leonida status / target |
|---|---|---|
| `VICTORY_TEXTURE_DROP_MIPS=N` (≤4): host copy leaves out the N largest levels; views shift by N; copies to/from a dropped texture are skipped | `crates/victory-gpu/src/resource.rs` `drop_levels` / `dropped` / `host_subresource`; `texture.rs` staging | **Decision landed**: `runtime/src/texture_mip_drop.h` + `texture_mip_drop_test` (ported rules: 2D, title-filled, single-sampled, shorter side ≥512, both sides divisible by `4<<N`, N ≤ mips−1). Wiring next: `TextureCreateInfo`/`Texture::create` (host extent/mips), `create_texture_view` (`first_mip`/`mip_count`/`width`/`height`/`resource_mips`), `region` + `upload_title_subresource` + title refresh (subresource map `m → m−N`, skip `m<N`), and any title-written T# mirroring (`gfx10_descriptors.h` `Image.first_mip/mip_count/width/height`, `image_min_lod`). Gate: `LEONIDA_TEXTURE_DROP_MIPS`, default 0. |
| `VICTORY_LEVELS_ON_DEMAND=1`: mip levels allocated on the card only as held, tiled-resources tier 2, 64 KiB tiles | `crates/victory-gpu/src/tiles.rs` (`wanted`, `floor`, pool) | Missing. Our images already live in shared 64 MiB blocks (`runtime/src/image_memory.h`, `vulkan_title_image_pool.h`); a Vulkan analogue is sparse residency / per-mip pools, not a direct port. Lower priority than drop-mips. |
| Descriptor records the title writes into its heap as bytes are mirrored before every submission | `descriptor.rs`; fork README (40k records/frame) | We have `leonida::TitleVaAliases::pull_descriptors` + `scan_table` (`runtime/src/title_va_alias.h`, used by `d3d12_vulkan_device.cpp`) — same idea; compare coverage/limits. |
| Room kept for the render targets and buffer copies a world makes in its first frame (945 MB copies, 550–710 MB scanned → 25 MB) | `resource.rs` `room`; `cardmeter.rs` | Partial: title-window budget (`LEONIDA_TITLE_WINDOWS_MB`, 64 MiB) and arena; no explicit first-frame card reservation. |
| Texture upload / refresh / host copy | `texture.rs`, `tiles.rs` | `runtime/src/title_texture_refresh.h`, `title_texture_memory.h`, `upload_title_subresource`, `refresh_title_textures`. |
| Buffer view off an element boundary gets its own bytes instead of refused | `resource.rs` (upstream 86a5ed6) | Check our V#/buffer view refusal path. |
| Frame pacing (`VICTORY_RELAY`, `VICTORY_LATE_WAIT`, `VICTORY_WIDE_BACK`, `VICTORY_CARD_BUFFERS_MIN_MB`, `VICTORY_CARD_BUFFERS_MB`) | `frame.rs`, `framelog.rs` | `LEONIDA_FRAME_PACING`, `FramePacer`, `LEONIDA_FPS_CAP`, `LEONIDA_PRESENT_UNCAP`; not the same knobs. |
| Paired Wave64 lowering on Wave32 GPUs, 64 KiB CBV clamp, host fixes | `victory-shader`; PRs #799/#854 | Our translator (`tools/rdna2spirv.py`, `runtime/src`) and `LEONIDA_W64_EMULATE`; compare. |
| `VICTORY_WIDE_TEXTURES`, `VICTORY_CARD_FETCH_INFLIGHT`, `VICTORY_SKIP_WC_COMPARE`, unmapped-block parking | `resource.rs`, `tiles.rs`, `queue.rs` | Not ported; texture-check parallelism and copy-back reduction. |

## Port order

1. **`LEONIDA_TEXTURE_DROP_MIPS`** — the direct texture-mass lever; decision is tested. Wire host extent/mips,
   slot-70 views, subresource mapping, and T# mirroring; measure with the fork's own rule (bytes ÷ ~4 per level).
2. **`LEONIDA_LEVELS_ON_DEMAND`** — per-mip residency, only if drop-mips is not enough on the 12 GB card.
3. Descriptor-mirroring and first-frame card reservation — compare with our `pull_descriptors`/title windows.
4. Frame pacing/copy cookbooks (`CARD_FETCH_INFLIGHT`, `SKIP_WC_COMPARE`) only after the picture is right.

## Licensing

Both repositories are GPL-3.0-or-later. Keep the provenance comment in ported files (as in
`runtime/src/texture_mip_drop.h`).
