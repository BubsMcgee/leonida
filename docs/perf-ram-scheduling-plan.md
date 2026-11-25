# Perf plan: RAM and task scheduling

Working notes so this survives a compaction. Branch `perf/ram-scheduling`, worktree
`C:\Users\alexb\AppData\Local\Temp\opencode\leonida-ram`, based on `origin/main` `1fe5072af`.

Rules that still hold:
- The live checkout `C:\Users\alexb\leonida` belongs to another running instance. Read-only.
- Upstream `ubiquitarianism/leonida` is off-limits: no branch, PR, comment or push. Work goes to
  `fork` (`BubsMcgee/leonida`) only.
- Do not merge/rebase until the other instance has stopped booting and stopped landing commits.

## 0. Measure before changing anything

- Read **Working Set**, **Private Bytes** and **Mapped File** separately (VMMap / Process Explorer). Task
  Manager "Memory" is not enough.
- The ~3.88 GiB `XMemVirtualAlloc` window is **virtual**. On Windows it is lazy-committed, and its scratch is a
  **sparse file mapping** (`runtime/shims/xmem.cpp:334-366`), so touched pages are pageable, not private commit.
  Do not read that number as resident RAM.
- `LEONIDA_MEM_PROBE=1` reports arena/pool/upload live sizes. Do **not** set `LEONIDA_MEM_PROBE_CACHE=1` unless
  measuring the cache: the driver serializes the whole pipeline cache to answer.
- `LEONIDA_XMEM_SEC_COMMIT` only has an effect under Wine; on Windows sections are already per-page on first touch.

## 1. Env-only levers (biggest first)

| Variable | Default | Effect | Risk |
|---|---|---|---|
| `LEONIDA_ARENA_MB` | 256 (profile forces **2048**, `runtime/src/main.cpp:196`) | One host-visible `VkBuffer`; prefers `DEVICE_LOCAL\|HOST_VISIBLE` (VRAM with ReBAR) but can fall back to **system RAM** (`runtime/src/arena_size.h`). Try 512, watch `GPU address arena full`. | Windows into title memory refuse to map; world may stop drawing. |
| `LEONIDA_TITLE_MEMORY_MB` | host RAM (~32 GiB) | Value reported by `XMemGetWorkingSetStatistics` / `XSystemGetAppMemoryUsageLimit`; the title sizes its own pools from it (`runtime/shims/xmem.cpp:245`, clamp 2048..65536). Try 8192. | Title streams harder; pop-in / OOM. |
| `LEONIDA_HEAP_GUARD_MB` | 64 MiB | Resource-heap guard; scratch is `VirtualAlloc(bytes*3+guard, MEM_RESERVE\|MEM_COMMIT)` (`runtime/shims/d3d12_vulkan_device.cpp:3239,3602`), i.e. committed. Try 8. | Weaker past-end guard for heap indices. |
| `LEONIDA_PIPELINE_CACHE_BYTES` | session file on | Bounds the persisted `pipeline_cache.session.bin`; `0` disables it (`runtime/src/pipeline_cache_store.h`). Does not bound the in-memory cache. | Losing the warmed cache means recompiles on next boot. |
| `LEONIDA_WRITE_WATCH` | on (Windows) | `MEM_WRITE_WATCH` costs a kernel dirty bitmap plus the record/journal (`runtime/src/title_page_watch.h:466`). | **Breaks title write-back** on the integration branch. Do not disable there. |
| `LEONIDA_UPLOAD_SYSMEM` | 0 (device/BAR) | `1` moves uploads to system RAM. Keep `0` when cutting RAM. | More RAM. |
| `LEONIDA_ARENA_NULL_MB` | 0 | Commits zeroed arena head. Keep 0. | — |
| `LEONIDA_POOL_ZERO_PAGES` | unset | Maps zero pages on reads. Leave unset. | — |
| `LEONIDA_DSTORAGE_HISTORY` | path | `0` disables external history. | Slower loads. |
| `LEONIDA_DSTORAGE_FITTED` | on | `0` requires confirmed fitted blocks. | Slower loads. |
| `LEONIDA_PACKAGE_SPLASH` | on | `0` skips splash decode entirely. | None for the world. |

Low-RAM test block:

```powershell
$env:LEONIDA_ARENA_MB="512"; $env:LEONIDA_TITLE_MEMORY_MB="8192"; $env:LEONIDA_HEAP_GUARD_MB="8"
$env:LEONIDA_PIPELINE_CACHE_BYTES="0"; $env:LEONIDA_UPLOAD_SYSMEM="0"; $env:LEONIDA_DSTORAGE_HISTORY="0"
$env:LEONIDA_ARENA_NULL_MB="0"; $env:LEONIDA_PACKAGE_SPLASH="0"
```

Also investigate: `runtime/shims/acphal.cpp:1074-1132` commits cached + non-cached APU heaps up front (64 MiB
default when the title gives no size). Lazy per-page commit is possible but needs a first-touch path; risky.

## 2. Code work on this branch

- [x] Splash: one WIC decode shared by GDI paint and D3D12 texture, **released once both consumers are done**
  (`runtime/shims/package_splash.cpp`). 64 MiB-class image (256 MiB at the 64 Mpx cap) no longer lives for the
  whole process.
- [x] One-shot working-set trim after the splash hands off to the world frame, delayed 5 s and env-gated
  (`runtime/src/working_set.h`, `LEONIDA_TRIM_WORKING_SET=0` disables). The sparse XMem backing makes this page
  cold data back to the file, so resident RAM drops without changing commit or semantics.
- [ ] Bound the **in-memory** `VkPipelineCache`: after N MiB (or after world load) write the session file, destroy
  the cache and recreate an empty one. Created once at `runtime/shims/d3d12_vulkan_device.cpp:2020-2029`, saved at
  `:1425`. Pipelines already created stay valid; only dedup across later compiles is lost.
- [ ] Optional: skip the splash cache entirely when only one consumer is enabled.
- [ ] Optional: on `WM_ACTIVATEAPP` deactivation, trim the working set again (needs a window-proc hook).

## 3. Task scheduling / resource allocation

- [x] `LEONIDA_PIPELINE_THREADS` already exists (`runtime/src/pipeline_workers.h:154`): default
  `min(8, logical_processors / 2)`; `0` compiles on the submitting thread. Read it before adding another knob.
- [x] `LEONIDA_PIPELINE_PRIORITY` (`above`, `high`, `below`, `low`; normal when unset) and
  `PipelineWorkers::set_thread_start` ported to this branch. Compile workers can run above-normal so the render thread
  is not starved and warmup finishes sooner. Ported from `tools/warmup-scene-check` (`e605ce241`).
- [ ] Physical-core cap: `hardware_concurrency()` counts logical CPUs. Cap compile workers to
  `min(physical_cores - 1, 8)` so the render/submit thread keeps a core.
- [ ] Upload fairness: `TitleUploadBudget` (`runtime/src/title_staging.h`, on `title-streaming-workers`, not
  main) charges one pass's texture uploads. Expose its bytes/items as env knobs and cap peak staging.
- [ ] Frame pacing already exists (`LEONIDA_FRAME_PACING`, `FramePacer`); verify it does not run an extra
  prerender frame while streaming.
- Measure with `PipelineWorkers::Stats` (`d3d12::pipeline_worker_stats`) and `LEONIDA_MEM_PROBE`.

## 4. Merge / rebase / rebuild (only after the other instance stops)

1. Confirm `gta6rt` has exited and `local/render-int` has been still for ~10 min (and no new worktree/branch).
2. `git fetch origin --prune`; record `origin/main` and `local/render-int` heads.
3. Fast-forward `fork/main` to `origin/main` (mirror only).
4. Update our branches from main, keeping our commits on top, resolving in our favour, pushing **fork-only**:
   `local/render-int`, `title-streaming-workers`, `render-int-build`, `present-frame-pacer`,
   `tools/warmup-scene-check`, `geometry-adjacent-runs`, `fix/title-gpu-write-back`, `stream-output`,
   `perf/ram-scheduling`.
5. Bring `perf/ram-scheduling` (and the tools branch) into the refreshed integration.
6. Configure + build the integration:
   `cmake -S runtime -B runtime/build -G "Visual Studio 17 2022" -A x64`,
   `cmake --build runtime/build --config RelWithDebInfo --target gta6rt`.
7. Boot with the low-RAM block and splash off (`LEONIDA_PACKAGE_SPLASH=0`); sample Working Set / Private Bytes;
   confirm the world still draws; run the runtime tests.
8. Never write to upstream.

## 5. Verification

```powershell
# build (Vulkan SDK + msys64 on PATH)
& $cmake -S runtime -B runtime/build -G "Visual Studio 17 2022" -A x64
& $cmake --build runtime/build --config RelWithDebInfo --target gta6rt

# boot and sample memory
$env:LEONIDA_PACKAGE_SPLASH="0"
powershell -NoProfile -File scripts\windows-boot\boot.ps1 -Name ram -Seconds 240 `
  -Runtime "runtime\build\RelWithDebInfo\gta6rt.exe" `
  -SetEnv @{ LEONIDA_ARENA_MB='512'; LEONIDA_TITLE_MEMORY_MB='8192'; LEONIDA_HEAP_GUARD_MB='8' }

# process memory over time
Get-Process gta6rt | Select-Object WorkingSet64,PrivateMemorySize64,PagedMemorySize64
```

Acceptance: world still renders; no new `GPU address arena full` / device loss; Working Set lower than the
baseline for the same scene; `tests/test_scene_timing.py`, `pipeline_workers_test`, `fps_cap_test` still pass.
