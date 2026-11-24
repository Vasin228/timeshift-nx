# TimeShift ReXGlue Port — Archive (Parts 266–285)

## 2026-11-20 (Part 266) — Pure `08034ba` Verification & Surgical Secondary-Stage / Streaming / Mip Fix
- New Game (`city_part01`) log check on pure `08034ba` (`switch_newgame.log`, `5,481` lines): `0` missing shaders, `0` `[flash]` warnings, `0` memory evictions (`2,435 / 3,075` MB), `~18.2 FPS`. Yet user saw glowing/disappearing/flickering objects because pure `08034ba` dropped secondary texture stages (`texture_va == 0`), rejected streamed `dword[3]` updates, allowed `StoreCardsOn()`, and allocated unuploaded mips when `MaxMipLevel == 0`.
- Surgical fixes applied cleanly atop `08034ba` (`textures.cpp`, `draw.cpp`) and mirrored to `d:\Anti\timeshift-renut` (without `setDepthBias`, `FallbackPsPair`, or `texture_layout.cpp` changes):
  - `textures.cpp`: Added `SyntheticTextureVa` + `texture_va == 0` secondary stage binding, removed blocking `dword[3] & 0x0007FFFFu` check, capped `target_levels = std::min(max_mip_level + 1, size_levels)` and `effective_max_mip = max_mip_level` (no unuploaded mips), disabled `StoreCards` under `TIMESHIFT_RECOMP`.
  - `draw.cpp`: Guarded `FillTextures` header update with `!binding.gpu_written`.
- Build & Deploy (`192.168.0.6:4150`):
  - `ninja` + `build_switch_app.py`: exit 0 (`220` objects, `26/26` hooks, `0` foreign); `check_profile.py` PASS.
  - `timeshift.elf`: `2026-11-20 16:33:04`, `545,913,472` B, SHA-256 `629a52a82fc65c866630f024f49e1aa0c62517c174f72a2dbb2258c8ab73c332`.
  - `timeshift.nro`: `2026-11-20 16:33:07`, `91,723,973` B, SHA-256 `8241347d5a1b1e44a03a7e73bb4f2ee6645a6958e17e6d09ecae2f1cce96b7c6`.
  - Uploaded to Switch & verified 100% remote SHA-256 match (`8241347d5a1b1e44a03a7e73bb4f2ee6645a6958e17e6d09ecae2f1cce96b7c6`); cleared `pipeline_list.bin` and `timeshift.log`.
- Next: Launch via Title Override (Hold R) in New Game (`city_part01`) and check whether glowing/disappearing/flickering objects are resolved.

## 2026-11-20 (Part 267) — Disable Unsafe Banjo/SA2 Rendering Shortcuts (`renut_tuning_shims.cpp`, `pipeline.cpp`, `timeshift.toml`)
- Root Cause Investigation:
  - `switch_p266.log` (`67,762` B) confirmed `0` missing shaders (`4,887` loaded), `0` memory evictions, and only `1` `[flash]` warning, yet `city_part01` still exhibited glowing/disappearing/flickering objects across `0c50177`..`08034ba` and Part 266.
  - `git diff --stat 0c50177..08034ba` confirmed zero renderer changes between those commits; the shared `sa2-nx`/`banjo-nx` baseline had aggressive speculative shortcuts active (`resolve_skip=true`, `cpu_memos=true`, `depth_only=true`, `Deko3DBarrierMergeOn=true`, `Deko3DBarrierLeanOn=true`, `Deko3DVsOutputsOn=true`, `EarlyZOn=true`, `EarlyHoistOn=true`, `zfunc == 7` depth disable in `pipeline.cpp`, and `sa2_renut_lazy_resolve_mips`/`direct_resolve_mips`/`fetch_slots_skip`/`fetch_data_unread`/`stream_mask`).
- Applied Clean Fix (mirrored byte-identically to `d:\Anti\timeshift-renut`):
  - `adapter/renut_tuning_shims.cpp`: Disabled `resolve_skip`, `cpu_memos`, `depth_only`, `Deko3DBarrierMergeOn`, `Deko3DBarrierLeanOn`, `Deko3DVsOutputsOn`, `EarlyHoistOn`, `EarlyZOn`.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/pipeline.cpp`: Removed obsolete Banjo-NX `zfunc == 7 && !depthWriteEnabled` `depthEnabled = false` override.
  - `timeshift.toml`: Disabled `sa2_renut_lazy_resolve_mips`, `sa2_renut_direct_resolve_mips`, `sa2_renut_fetch_slots_skip`, `sa2_renut_fetch_data_unread`, `sa2_renut_stream_mask`.
- Build:
  - `check_profile.py` PASS; `ninja` + `build_switch_app.py` exit 0 (`220` objects, `26/26` hooks, `0` foreign).
  - `timeshift.elf`: `545,922,376` B, SHA-256 `71f063147238204af20d9c64f0ebc3240fb2f287d33691c6fa653a4f7d906b85`.
  - `timeshift.nro`: `91,723,973` B, SHA-256 `6b798543e6943974dec34ca1b29c0011e42294ad9b86393e170fb44b2f11535f`.
- Next: Deploy to Switch (`192.168.0.6:4150`) and test in New Game (`city_part01`) via Title Override (Hold R).

## 2026-11-23 (Part 270) — Xenos `GetTextureGradients` Fix (850 PS) & Clean Single-Pass 4,887-Shader Rebuild
- **Root Causes Isolated (Live Switch Playtest & Shader Audit)**:
  1. **Low-Res Blurry Mipmaps & Flat Parallax Surfaces**: Across `4,887` unique Xenos shader containers (`745` VS + `4,142` PS), `850` pixel shaders (`1,199` occurrences; e.g. `ps_14249C9D7E279474`, `ps_235C3B962B8D4967`, `ps_BC27542B...`) execute `FetchOpcode::GetTextureGradients` (opcode `18`) twice before `log2(...)` + `SetTextureLod` + `textureLod(...)`. `XenosRecomp` silently dropped opcode `18`, leaving stale derivative registers that pushed LOD to `6..8` and forced `box_01`, `concrete3`, `brckvall_tile`, and `_hdetm` materials onto ~16x16 mipmaps.
  2. **Glowing White Characters/Boots/Heads & Draw-Order Flicker**: Auditing `shader_cache_switch.cpp` against `deko3d_glsl.bin` uncovered **1,220 / 4,887** colliding `spirvOffset` ranges, **150 / 745** colliding `vertexInputOffset` ranges, and **432 / 4,040** colliding `literalOffset` ranges from prior incremental cache merging — causing **969 shaders** to bind wrong literal constants (e.g. `PS 7C2F5632221D1CA9` on `ts2_rebel_legs` reading `v[255] = 255.0f` instead of `0.2f`, blowing out the kneeling soldier's boots, belt, and knit cap to pure `#FFFFFF`).
- **Fixes Applied & Verified**:
  - `upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/XenosRecomp/shader_recompiler.cpp` (byte-identical across `timeshift-nx` and `timeshift-renut`, SHA-256 prefix `449b07891f9bea6d`): Implemented `FetchOpcode::GetTextureGradients` for pixel shaders under `#ifdef RENUT_RECOMP`, emitting predicated `float4(ddx_coarse(u), ddy_coarse(u), ddx_coarse(v), ddy_coarse(v))` with full dest/`0`/`1` swizzle handling; rebuilt WSL `XenosRecomp` and verified `dFdxCoarse` / `dFdyCoarse` in generated GLSL.
  - **Clean Single-Pass 4,887-Shader Rebuild**: Recompiled all `4,887` containers in one `XenosRecomp` pass (`spirv-cross`, `deko3d_glsl.py`, `uam`: `0` failures):
    * `spirv_cache.zst`: `5,750,593` B (`28,232,335` B decompressed), SHA-256 prefix `0c8881635295c670`.
    * `deko3d_glsl.bin` (`BDKG v1`, `4,887` entries, round-trip verified): `16,360,504` B, SHA-256 `52a2477080e22066272826a3034e93d0ab139c3d849b1ed93645569df0289f2a`.
    * `deko3d_shaders_f0.bin` (`BDKC v1`, `5,772` total records, `4,887/4,887` GLSL `fnv1a64` keys present): `36,453,800` B, SHA-256 `f1cf783c0f6dbe7010d158c1285fd72e25d14b68e0474f902ec308e695c621f7`.
    * Cache integrity audit PASS: `4,887` sorted unique entries, `0` `spv`/`vd`/`lit` range overlaps, `0` literal register mismatches (`PS 7C2F5632221D1CA9` verified at `v[253]=[-0.5,2.0,0.8,10.0]`, `v[255]=[0.2,0.0,0.0,0.0]`).
- **Build, Audits & Live Switch Verification (`192.168.0.7:4150`)**:
  - Clean release binary (temporary `[s3d-tex]`/`[mip2100]`/`[draw2100]` diagnostics removed): `ninja` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` TimeShift hooks, `0` foreign hooks); `audit_private_trees.py` PASS; `check_profile.py` PASS.
  - `build/app-switch/timeshift.elf`: `2026-11-23 00:37:55`, `545,549,792` B, SHA-256 `650AC28E0D5ECE1526689738A62865DA5793CBF80618BB63C3319684B80D0A2C`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 00:37:59`, `91,244,741` B, SHA-256 `FE461AAFB6AEBBFA3FF8830DA6A123632BDD712457DD22C7E64535CFF98F0711`.
  - Deployed to `/switch/timeshift/` (`deko3d_glsl.bin`, `deko3d_shaders_f0.bin`, `timeshift.nro`) with `HTTP 201`, `100%` transfer, and remote SHA-256 `match=True`.
  - **Live Playtest (`city_part01`)**: Main Menu, intro cutscene, Room 1, Room 2 (`box_01` "DANGER" crates, `SB-T118a` generators, boardwalks, pipes, torn plaster, fusebox, archway corridor, stairwell) all render with sharp POM/mipmap detail and **0 glowing/disappearing/flickering objects**; the kneeling soldier at the `CAUTION` crate now renders with normal dark leather boots, belt, and grey knit cap.
  - **Clean Log Audit (`9,908` lines / `1,026,623` B, `45,180` swaps)**: `0` `[error]`, `0` `TSDBG missing guest function`, `0` missing/fallback shaders, `0` crashes/faults.
- **Next**: User playtesting outdoors/late chapters (`city_part01` street onward) and reviewing `perf_baseline.csv` metrics for FPS tuning if needed.

## 2026-11-23 (Part 271) — EDRAM 4xMSAA Soft-Particle Aliasing Fix (Fire/Explosions) + Custom NRO Icon & Author (`vasin228`)
- **Root Cause Isolated (Low-Res Soft Particles / Fire / Explosions)**:
  - Guest PPC disassembly (`sub_82184908`, `sub_82185440` `BeginLowRes`, `sub_821855C0` `EndLowRes`, `sub_821C8C90`) proved TimeShift renders main color/depth (`2236`/`2240`) at `1280x720 1x` and low-res soft-particle color/depth (`2276`/`2280`) at `640x360 4xMSAA` sharing the same EDRAM base tiles (`0` / `r30`).
  - Because `SampleCount()` forces `COUNT_1` on `TIMESHIFT_RECOMP` while `TargetKey()` keyed on raw `SurfaceInfo` pitch (`640` vs `1280`) and MSAA bits (`2` vs `0`), `2276`/`2280` created orphaned `640x360` host targets, discarding all fire/smoke/grenade/explosion draws (`part_mega_pak`, `PS 0F4077C8FF4A44C6`) when `EndLowRes` switched back to `2236`.
- **Fixes Applied & Mirrored (`timeshift-nx` & `timeshift-renut`)**:
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/surfaces.cpp` (SHA-256 `AD8AB27B4967F6720A8A2741363A58F63B1FD2648D638B73C2E8F9ABA4929341`): Added `EdramMsaaScale()` and updated `TargetKey()`, `BuildTarget()`, and `GetHostSurfaceSlow()` under `#if defined(TIMESHIFT_RECOMP)` so 2x/4x MSAA EDRAM views alias the `COUNT_1` sample-space target (`1280x720`).
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/draw.cpp`: Added `msaa_scale_x`/`msaa_scale_y` handling across `Targets`, `GatherTargets()`, `ComputeViewport()`, `ComputeViewportSlow()`, `ViewportReuse`, `SameTargets()`, `DrawStateLocked()` (`sc.resolution_scale` scaled by `msaa_scale_x`, plus `!ps_writes_depth` guard via `kShaderFlagPixelDepth` preserving `targets.depth` when pixel shaders write depth), and `ClearStateLocked()`.
  - `assets/icon.jpg`: Custom Beta Suit chrono-ring icon (`256x256` RGB baseline JPEG, `SOF0=True`, `SOF2=False`, `24,973` B, SHA-256 `74DB982218ECA68E50897DBE4D60FDD24F2A92D50A7B807C4F052636CD9462F6`).
  - `tools/build_switch_app.py`: Generates `build/app-switch/timeshift.nacp` via `nacptool --create "TimeShift" "vasin228" "1.0.0"` and packages `timeshift.nro` with `--nacp` and `--icon=assets/icon.jpg`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` TimeShift hooks, `0` foreign hooks).
  - `verify_nro.py` PASS (`Embedded Icon: 24,973 bytes` matches `assets/icon.jpg`; `NACP Title='TimeShift', Author='vasin228', Version='1.0.0'`); `audit_private_trees.py` PASS; `check_profile.py` PASS.
  - `build/app-switch/timeshift.elf`: `2026-11-23 02:53:03`, `545,558,384` B, SHA-256 `6E7A55353B8B775A97512BBA3771BEC2D8402B37EFA360D7BE1D75DFAC258005`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 02:53:06`, `91,263,429` B, SHA-256 `B5CC9EE8F44030C96BF5423628BC6850C9F3A7D68BE7A93E6EF94D59C83E4D56`.
  - Uploaded `/switch/timeshift/timeshift.nro` via `sys-autopilot` (`HTTP 201`, `91,263,429` B, `100%`, remote SHA-256 exact match). Game intentionally not auto-launched per user instruction.
- **Next**: User manual verification via **Hold R Title Override** for `hbmenu` icon/author (`vasin228`) and in-game fire/campfires/grenades/explosions (`VIDEO_UseSoftParticles = Yes`).

## 2026-11-23 (Part 272) — Fix 2x Zoomed/Shifted Camera on Tiled 4xMSAA Main Scene While Preserving Aliased 640x360 Soft Particles
- **Root Cause Isolated (Switch Screenshot + `default_image.bin` Preset 4 Disassembly)**:
  - Live user screenshot confirmed Part 271 restored explosion smoke/flash/debris particles with proper depth occlusion and intact `1x` HUD, but zoomed the 3D scene camera ~2x into the top-left quadrant (pushing the player weapon viewmodel off the bottom-right).
  - Inspecting `default_image.bin` (`0x82B28188`, active preset index `0x82B288E0 = 4`), `sub_82184908`, `sub_82184E30`, and Switch GPU memory telemetry (`8 RTs / 33 MB` in Part 270 -> `6 RTs / 41 MB` in Part 271) proved TimeShift renders the main 3D scene into **Predicated Tiling Preset 4** (`1280x256` 4xMSAA, `msaa=2`, `pitch=1280`, 3 horizontal tiles spanning `1280x720`).
  - Part 271 applied `EdramMsaaScale` (`2x2`) unconditionally to every `msaa != 0` surface, expanding Preset 4 to `2560x720` (`+11 MB`), scaling its `1280x720` viewport to `2560x1440`, and causing `EndTiling` to resolve only the top-left `(0,0)-(1280,720)` quadrant into the `1280x720` scene texture.
- **Fixes Applied & Mirrored (`timeshift-nx` & `timeshift-renut`)**:
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/surfaces.h` (SHA-256 `F929F3408476FC8AFD8CD53F2A25150A15A6DF554509A53CA3004C1CCC4C6839`): Added `HostSurface::target_msaa` recording the backing EDRAM target's native MSAA mode at `BuildTarget()`.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/surfaces.cpp` (SHA-256 `ED421898B0399A804AD64EF3125D4EA4745FEE9F3EB5EB41C87A65F64D94B337`): Restored upstream `TargetKey()` and logical `surface.width`/`height` sizing (keeping tiled Preset 4 at `1280x720` 1:1); in `GetHostSurfaceSlow()` under `#if defined(TIMESHIFT_RECOMP)`, alias an untiled (`!g_tiling_width && !g_tiling_height`) 2x/4x MSAA view (`2276`/`2280` `640x360`) onto an existing `1x` target (`2236`/`2240` `1280x720`) only when matching base + sample-space pitch exists and already covers `SurfaceWidth * msaa_x` by `SurfaceHeight * msaa_y`.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/draw.cpp` (SHA-256 `D79C34B6C705B2DAFF3D03A298F89EBD9EDD4FF225B9D7527FDABD12EB055ED2`): Restricted `GatherTargets()` `msaa_scale_x`/`msaa_scale_y` scaling strictly to `first->target_msaa == 0 && msaa != 0` so standalone MSAA targets (`Preset 4`) render 1:1 while aliased soft particles (`2276`/`2280`) still scale `2x2` onto the `1280x720` target.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` TimeShift hooks, `0` foreign hooks).
  - `verify_nro.py` PASS (`NACP Title='TimeShift', Author='vasin228', Version='1.0.0'`, embedded icon matches `assets/icon.jpg`); `audit_private_trees.py` PASS; `check_profile.py` PASS.
  - `build/app-switch/timeshift.elf`: `2026-11-23 05:26:57`, `545,565,192` B, SHA-256 `08FB6577D06522B2F1BE0221F80BBA2BEC97DCDF823C096A9E00C0A9D3D27AD2`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 05:27:01`, `91,263,429` B, SHA-256 `CDECE0E7DF957C664D85EF85B70DDD7D4951B21AC0287D8D0093E94774163142`.
  - Uploaded `/switch/timeshift/timeshift.nro` via `sys-autopilot` (`HTTP 201`, `91,263,429` B in `33.84s`, `100%`, remote SHA-256 `match=True`). Game intentionally not launched per user instruction.
- **Next**: User manual playtest via **Hold R Title Override** confirming normal 1:1 camera + weapon framing alongside working soft-particle fire/explosions and `hbmenu` icon/author (`vasin228`).

## 2026-11-23 (Part 273) — Stage 1 & Stage 2 Performance Optimization (`-O3`, Snapshot Deltas, Chain Reuse, Hand-Over, PMULL, 4x Aniso & Native `480p` Render Scale)
- **Measured Baseline (`perf_baseline.csv`, `1280x720`)**:
  - `60.798 ms` avg (`16.45 FPS`, `1% low = 9.62 FPS`), `render_busy = 39.813 ms` (up to `57 ms` outdoors), `gpu = 43.377 ms`, `draws = 420.9` (up to `840` outdoors), `pipeline_binds = 133.7`, `resolves_drawn = 20.0`, `resolves_skipped = 0.0`.
- **Stage 1 & Stage 2 Applied & Mirrored (`timeshift-nx` & `timeshift-renut`)**:
  - `upstream/sa2-nx/app/src/renut_engine/gpu/backend/queued.cpp` (`D7FD62E7...`) & `adapter/presentation_hooks.cpp` (`60796B9B...`): Enabled NEON `sa2_renut_snapshot_deltas` + constant deltas + deferred chain without mutating `m_Pending` (comparing the 64 VS + 64 PS 64-byte constant groups directly against `c.shadow` since UP draw hooks run after guest `m_Pending` clears), while keeping `marks`/`precise` off; added `renut::gpu::SnapshotDeltasNewFrame()` in `NativeSwap()`.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/draw.cpp` (`B846A0DB...`): Unlocked `sa2_renut_colour_handover`, `sa2_renut_clear_repeat`, and `sa2_renut_chain_reuse` (`TargetsReuse` + `KeyReuse`), fixing `targets_reuse` device offset check from hardcoded `12816..12835` to `d3d::kDeviceOffsetRenderTargets..d3d::kDeviceOffsetDepthStencil + 3` (`12432..12451` in TimeShift).
  - `adapter/renut_tuning_shims.cpp` (`6E6F12F1...`): Enabled `depth_words_lazy = true`, `depth_handover = true`, and ARMv8 PMULL CRC64 hashing (`banjo_pmull_fingerprint.h`, `g_pmull_hash = kAvailable`); intentionally kept `depth24 = false`, `Deko3DDepthCompressionOn() = false`, and `Deko3DZcullOn() = false` per Parts 234/240 GM20B findings.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/textures.cpp` (`FB0B4920...`) & `graphics_options.cpp` (`6632B5DE...`): Removed hardcoded `std::max(..., 16u)` anisotropy floor and defaulted `renut_anisotropic_filtering` to `"4x"`.
  - `upstream/sa2-nx/app/src/renut_engine/gpu/render/surfaces.cpp` (`49E2AB00...`): Unlocked `REXCVAR_GET(renut_resolution)` (`"480p"` = `2/3` `853x480`, `"720p"` = `1x`, `"1080p"` = `1.5x`, etc.) under `TIMESHIFT_RECOMP`; verified `3,079/4,887` shaders in `deko3d_glsl.bin` already carry `renutTextureScale` (`PackageScales() = true`).
  - `integration/CMakeLists.txt` (`016C3083...`) & `tools/build_switch_app.py` (`A2A68C6B...`): Upgraded `timeshift_renut_core` and app units (`renut_tuning_shims.cpp`, `timeshift_app.cpp`) from `-O2` to `-O3`.
  - `timeshift.toml` (`385041C3...`): Enabled `renut_vsync = true`, `renut_resolution = "480p"`, `renut_anisotropic_filtering = "4x"`, `sa2_renut_snapshot_deltas = true`, `sa2_renut_chain_reuse = true`, `sa2_renut_colour_handover = true`, `sa2_renut_clear_repeat = true`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja` (CMake reconfigure + `28/28` `-O3` objects) + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` TimeShift hooks, `0` foreign hooks).
  - `verify_nro.py` PASS; `audit_private_trees.py` PASS; `check_profile.py` PASS.
  - `build/app-switch/timeshift.elf`: `2026-11-23 06:02:22`, `553,576,896` B, SHA-256 `80A9833258E207BFBBA46606A66593E4EB8262D048955CF5E708A2F38D630434`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 06:02:25`, `91,431,365` B, SHA-256 `F63D4BF0D24BD670C29E61A926BD7F2D5F5949A08AB3E66CFC01F559F0659229`.
  - Deployed `timeshift.nro` (`HTTP 201`, `31.41s`, `100%`) and `timeshift.toml` (`422` B, `HTTP 201`, `100%`) to `/switch/timeshift/`, both remote SHA-256 `match=True`.
- **Next**: User hardware playtest via **Hold R Title Override** (`city_part01`) to measure post-Stage-1/2 FPS and visuals in `perf_baseline.csv`.

## 2026-11-23 (Part 274) — Restore Native 720p & Add Zero-Overhead Game-Thread CPU Breakdown Telemetry
- **Root Cause Proven (Live Switch Part 273 Logs: `perf_baseline.csv`, `timeshift.log`, `deko3d.log`)**:
  - Part 273 `480p` (`853x480`, `0.667x`) halved GPU time (`43.1 ms` -> `20.1 ms` avg) and cut Core 2 `render_busy` ~30% (`39.8 ms` -> `27.0-32.8 ms` indoors, `57 ms` -> `40-47 ms` outdoors), yet `frame_ms` stayed `65.41 ms` (`15.3 FPS`, `1% low = 6.5 FPS`) and spiked to `101-128 ms` (`8-10 FPS`) outdoors at `670-786` draws while `game_wait_ms` read `0.31 ms` and `resolves_drawn` only dropped `20 -> 19` (`1` `[flash]` warning at frame 175).
  - Visuals became pixelated with `0` net FPS gain because the **game CPU thread** (not GPU or render thread) bottlenecks every frame, and `game_wait_ms` previously missed waits inside `fence_wait_hooks.cpp`, `DeviceSources::Wait`, and `NativeSwap` backpressure.
- **Changes Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - `timeshift.toml` (`2E98E8E2...`): Reverted `renut_resolution = "720p"` (restoring crisp `1280x720` visuals while retaining Stage 1 render accelerators for clean `720p` A/B measurement).
  - `adapter/perf_breakdown.h` (`AD2FDB89...`, header-only so object count stays `220`): Added `armGetSystemTick()`-based (`~2 ns`) per-frame accumulators and non-double-counting `FenceWaitScope` (`thread_local` depth guard).
  - `adapter/draw_hooks.cpp` (`B615562A...`, CRLF preserved), `adapter/fence_wait_hooks.cpp` (`98595EB6...`), `adapter/source_memory.cpp` (`B59FA1FC...`), `adapter/fence_query_hooks.cpp` (`7686004F...`), `adapter/tiling_hooks.cpp` (`DBDCCFDA...`), `adapter/presentation_hooks.cpp` (`1F8A5071...`): Timed owned adapter hooks vs `__imp__sub_*` original D3D calls (skipping nested UP calls), outer fence waits, global fence queries, tiling, `NativeSwap` interval, and game-thread Core mask (`svcGetCurrentProcessorNumber()`).
  - `adapter/renut_tuning_shims.cpp` (`1FFF0ADD...`): Recorded `CountHandOver` bins `[0..5]`, appended 6 CPU breakdown columns to `perf_baseline.csv` (`131072` B buffer), read live `clkrst` CPU/GPU/EMC MHz once per 300 frames, and emitted `[perf-cpu]` + `[perf-delta]` (`SnapshotDeltaReport()`) summary lines.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja` (`6/6` adapter objects) + `build_switch_app.py` (`renut_tuning_shims.cpp` + link) exit `0` (`220` AArch64 objects, `26/26` hooks, `0` foreign hooks).
  - `audit_private_trees.py` PASS; `check_profile.py` PASS; direct NRO ASET check PASS (`Title='TimeShift'`, `Author='vasin228'`, `Version='1.0.0'`, `24,973` B icon match).
  - `build/app-switch/timeshift.elf`: `2026-11-23 06:32:30`, `553,759,112` B, SHA-256 `177AC95BE72F8707A84D84D744A58FB07FAC8A8C6C75584F36E6661101E18B61`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 06:32:34`, `91,439,557` B, SHA-256 `47A9CCE1943DEE59D4BACC29562CD9468D222E6BF10CBF4F2EEBFC3C5EAE8ED5`.
  - Uploaded `/switch/timeshift/timeshift.nro` (`HTTP 201`, `100%`) and `/switch/timeshift/timeshift.toml` (`422` B, `HTTP 201`, `100%`), remote SHA-256 `match=True` for both.
- **Next**: User playtest via **Hold R Title Override** confirming crisp `720p` restoration and collecting 300-frame `[perf-cpu]` / `[perf-delta]` / `perf_baseline.csv` breakdown on Switch.

## 2026-11-23 (Part 275) — Stage A Optimization (`DrawIndexed` PM4 Bypass, Zero-Scan Constants, Single-Lock Registry, `-O3` & Core 2 Profiler)
- **Measured Part 274 `720p` Baseline (`timeshift.log` `f[960..1259]`, `406` avg draws, `1428/921/1600 MHz`, `cores=0x7`, `ho=[2120,600,900,0,0,0]`)**:
  - `frame_ms = 59.51 ms` (`16.80 FPS`, `1% low = 10.30 FPS`); `gframe = 59.54 ms`: `logic = 44.10 ms` (74%), `adp = 8.39 ms` (14%), `orig_d3d = 6.45 ms` (11%), `fwait = 0.02 ms`, `fqry = 0.00 ms`, `tile = 0.26 ms`, `swap = 0.33 ms`.
  - `render_busy_ms = 35.88 ms`, `gpu_ms = 40.30 ms`, `resolves_drawn = 19.0`, `resolves_skipped = 0.0`, `[perf-delta]` `121,876` draws / `643 B` avg (`5.9` chunks + `3.3` const groups).
- **Stage A Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - **Bypass Dead `DrawIndexed` PM4 Emission** (`adapter/draw_hooks.cpp` `4C168284...`, `draw_runtime_bridge.h/.cpp` `446C9B47...`/`17F59EE7...`): Added `ConsumeImmediatePending()` returning swapped `m_Pending[2]` and zeroing `m_Pending[0..4]`. In `sub_826505F0`, skipped `__imp__sub_826505F0` and called retained helper `__imp__sub_82655E00` only when `(p2 & 0x1E0000ULL) != 0` (preserving `ctx.lr`).
  - **Gate 128-Group Constant NEON Scan** (`upstream/.../queued.cpp` `F376449E...`): Ran the 64+64 `Line64Differs` scan in `CaptureDelta()` strictly for `call.up_vertices != nullptr`; regular `DrawIndexed` uses `m_Pending[0..1]` directly and keeps `c.shadow` synced inside `ForEachGroupRun`.
  - **Single-Lock `DeviceRegistry`** (`adapter/device_registry.h/.cpp` `D86F2174...`/`769CC5D1...`, `draw_hooks.cpp`): Replaced `std::unordered_map` `entries_` with `std::vector<std::pair<...>>` and unified `Contains`+`Acquire` into single-lock `AcquireIfOwned()` across all 7 draw/clear/resolve hooks under `HooksActive()`.
  - **`-O3` Across Renderer & Hot Adapters** (`integration/CMakeLists.txt` `B44DE1B2...`): Added `PRIVATE -O3` to `plume` and all 8 hot adapter targets (`draw_hooks`, `tiling`, `sources`, `lifecycle`, `sdk_tiling`, `fence_query`, `fence_wait`, `presentation_hooks`).
  - **Render-Thread Sub-Breakdown `[perf-ren]`** (`adapter/perf_breakdown.h` `310AE8A4...`, `draw.cpp` `78B880FF...`, `present.cpp` `5A53FBBB...`, `renut_tuning_shims.cpp` `3DC30BE1...`): Switched timers to `<switch.h>`-free `mrs cntpct_el0` + `(ticks * 625ull) / 12ull` with RAII `ScopedTick`; measured `dev`, `tex`, `geo`, `const`, `pipe`, `res`, and `pres` (excluding both `WaitScope` waits) into `[perf-ren]`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` hooks, `0` foreign hooks).
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, NRO ASET check PASS (`Title='TimeShift'`, `Author='vasin228'`, `24,973` B icon).
  - `build/app-switch/timeshift.elf`: `2026-11-23 07:01:20`, `557,270,208` B, SHA-256 `BF0DB18D1AF44F5BB9D3B760DADDD6C0FB3F0A29E027141139B7FF172C0EF2DA`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 07:01:24`, `91,488,709` B, SHA-256 `8B66BB4E84D884D786FA2705D35ABF3DB2412F2A5EB48999CA2DF3F1FEBED56C`.
  - Uploaded `/switch/timeshift/timeshift.nro` (`HTTP 201`, `91,488,709` B, remote SHA-256 `match=True`).
- **Next**: User closes old session, relaunches TimeShift via **Hold R Title Override**, plays 1-2 min in `city_part01` (indoors + outdoors), then pull `timeshift.log` + `perf_baseline.csv` to measure Stage A speedup and guide Stage B.

## 2026-11-23 (Part 276) — Stage B.1 (`cpu-pin` Core 0/1, `SampleHash` Fix, `2048`-Slot Memo, `[perf-tex]`/`[perf-orig]`)
- **Measured Stage A Results (`stageA_timeshift.log`, `stageA_perf_baseline.csv`, `1428/921/1600 MHz`, `cores=0x7`)**:
  - Clean stability: `0` crashes/errors (`1` `[flash]` warning at frame 1077). Menu `43.5–44.3 ms` (`22.6–23.0 FPS`); combat `53.2–70.9 ms` (`14.1–18.7 FPS`, `422–591` draws, `gpu = 40.8–48.1 ms`).
  - `adp + orig_d3d` dropped from `14.84 ms` (at `406` draws) to `11.38–13.92 ms` even at `422–591` draws; Core 2 `[perf-ren]` isolated **`tex = 27.71–28.23 ms`** out of `36.8–39.5 ms` `render_busy` (all other CPU render stages combined: `geo = 2.8–3.9`, `const = 1.5–1.9`, `pipe = 1.4–1.7`, `dev = 0.3–0.4`, `res = 0.25`, `pres = 0.3–0.6 ms`).
- **Stage B.1 Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - **Game/Render Core Isolation** (`adapter/presentation_hooks.cpp` `998D5C0E...`): One-time `svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 0x3u)` + `svcSetThreadPriority(..., 0x24)` in `NativeSwap()` keeping game thread strictly off Core 2 (`render_thread`) with `[cpu-pin]` logging.
  - **`SampleHash` Bugs Fixed** (`upstream/.../textures.cpp` `5D9BE1A6...`): Replaced redundant `TextureGuestExtent` recalculation with `KnowGuestExtent()`, and masked `fetch.dword` with `kDescMask` before hashing so sampler filter/clamp/aniso bits stop triggering false re-uploads.
  - **Expanded Texture Slot Memo** (`upstream/.../draw.cpp` `325EF800...`): Grew `g_texture_slot_memo` `512 -> 2048` (`>> 21`) and enabled memo hits for `!texture_va` (`header == nullptr`) while keeping `treuse` `all_memo` gated on `header != nullptr`.
  - **Zero-Overhead `[perf-tex]` & `[perf-orig]` Profilers** (`adapter/perf_breakdown.h` `259D8F3B...`, `draw_hooks.cpp` `31FBCBF1...`, `textures.cpp`, `draw.cpp`, `renut_tuning_shims.cpp` `641E38DE...`): Split `tex` (`memo`, `bind`, `sample`, `linear`, `upload`, `create`, `ho`, `samp`) and `orig_d3d` (`didx`, `up`, `clr`, `res`).
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` hooks, `0` foreign hooks); `audit_private_trees.py` PASS, `check_profile.py` PASS, NRO ASET PASS (`Title='TimeShift'`, `Author='vasin228'`).
  - `build/app-switch/timeshift.elf`: `2026-11-23 07:31:45`, `557,380,760` B, SHA-256 `CD6452FF537BE2FD4C5FC059185E22EA6E1BE5FA156455FD313B40926F576979`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 07:31:48`, `91,492,805` B, SHA-256 `E798BB4A60AA81ECA3EDF3D68FE022B6918CAA37BC654315E82825C9C303B4E5`.
  - Uploaded `/switch/timeshift/timeshift.nro` (`HTTP 201`, `91,492,805` B, remote SHA-256 `match=True`).
- **Next**: User launches via **Hold R Title Override**, plays 1-2 min in `city_part01` (indoors + outdoors), then pull `timeshift.log` + `perf_baseline.csv` to verify `[cpu-pin]` + `SampleHash`/`2048`-memo drop in `tex` and examine `[perf-tex]`/`[perf-orig]` for Stage B.2.

## 2026-11-23 (Part 277) — Stage B.2 (`RefreshLinearLocked` Sampled Hash + 1-in-8 Stagger for >64 KB Static Linear Textures)
- **Measured Stage B.1 Results (`stageB1_timeshift.log`, `stageB1_perf_baseline.csv`, `1428/921/1600 MHz`, `0` errors, `1` `[flash]` at frame 862)**:
  - `[cpu-pin]` succeeded (`ideal=0 mask=0x3 rc=0x0 prio=0x24 rc=0x0`, `cores=0x3`); `~420`-draw indoor windows (`W6`, `W10`) rose from `58.3 ms / 17.2 FPS` to `45.2–46.2 ms / 21.7–22.1 FPS` (`logic` `46.1 -> 36.9–38.0 ms`, `adp` `6.67 -> 3.48–3.65 ms`); heavier windows `W5/W7/W8/W11` (`495–537` draws): `52.1–69.7 ms` (`14.3–19.2 FPS`), `gpu = 37.6–48.1 ms`.
  - `[perf-tex]` pinpointed **`linear = 24.56–26.68 ms`** out of `tex = 27.0–29.4 ms` (`render_busy = 37.5–40.5 ms`), whereas `sample = 0.00–0.01 ms`, `upload = 0.02–0.03 ms` (`1.9–4.9/f`), `ho = 0.14–0.16 ms`, `samp = 0.19–0.22 ms`, `memo = 1234–1742`, `bind = 336–387`. `[perf-orig]`: `didx = 1.82–3.03 ms` (`99–160/f`), `up = 1.35–1.88 ms` (`37–50/f`), `res = 0.47–0.54 ms` (`30–32/f`), `clr = 0.29–0.41 ms` (`9/f`).
- **Stage B.2 Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - `upstream/.../textures.cpp` (`E7DD21F8...`): In `RefreshLinearLocked()`, computed `sampled_linear = (sample_sizes[0] + sample_sizes[1]) > 65536u` via `KnowGuestExtent()` consistently across all 4 `CheckedHash()` calls (`!t.uploaded`, `t.lock_driven`, `t.dynamic`, `else`) so large linear textures hash via `SampleHash` instead of hashing multi-MB buffers byte-for-byte every frame, and staggered stabilized static (`t.changes == 0 && (g_frame - t.verified_frame) >= 4`) `>64 KB` textures `1-in-8` frames via `(((t.guest_va >> 4) + g_frame) & 7u) != 0`. Small `<=64 KB` UI/font/LUT textures still full-hash every frame.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` hooks, `0` foreign hooks); `audit_private_trees.py` PASS, `check_profile.py` PASS, NRO ASET PASS (`Title='TimeShift'`, `Author='vasin228'`).
  - `build/app-switch/timeshift.elf`: `2026-11-23 07:43:30`, `557,383,280` B, SHA-256 `0A551FBDA097829EBBFF79F265C60C9CAB6410652C9FD21AB7EF5C31239DB9CC`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 07:43:34`, `91,492,805` B, SHA-256 `2FB86DB25530175EC81225C8E7C0DC11BC4F259F1FF3940E05239900D88DD56F`.
  - Uploaded `/switch/timeshift/timeshift.nro` (`HTTP 201`, `91,492,805` B, remote SHA-256 `match=True`).
- **Next**: User launches via **Hold R Title Override**, plays 1-2 min in `city_part01` (indoors + outdoors), then pull `timeshift.log` + `perf_baseline.csv` to verify `linear`/`render_busy` drop (`~26 ms` -> `<3 ms`) and inspect memory/game-thread FPS uplift before GPU Stage C.

## 2026-11-23 (Part 278) — Stage B.3 (Core 3 Activation for Guest Workers, Priority Equalization, `CaptureDelta` Prefetch, `-O3` Guest Wait/Adapters)
- **Measured Stage B.2 Results (`stageB2_timeshift.log`, `stageB2_perf_baseline.csv`, `1428/921/1600 MHz`, `0` errors)**:
  - **`linear` crushed `24.56–26.68 ms` -> `2.33–2.89 ms`**, cutting `tex` `27.0–29.4 ms` -> `4.17–5.15 ms` and Core 2 `render_busy_ms` `37.5–44.5 ms` -> `10.80–17.94 ms` (`13.07 ms` avg in `W11` = `76.5 FPS` render capacity).
  - Heavy combat (`>=550` draws, `638` avg) jumped `12.02 -> 18.76 FPS` (`1% low` `5.1 -> 12.8 FPS`, `+56%`); `LOW (<350 draws)`: `39.52 ms` (`25.30 FPS`, `gpu = 25.56 ms`); `W11` overall (`444` avg draws): `48.40 ms` (`20.66 FPS`, `gpu = 33.64 ms`). Across all scenes, **Game CPU Thread (`gframe = 48.39 ms`: `logic = 37.74`, `adp = 5.66`, `orig_d3d = 4.41 ms`)** bounds frame time.
  - `[threads]` confirmed 6 Saber3D workers (`sub_82264A98`) + streaming workers (`sub_822003C8`) running on default mask `0x7` (`prio=0x2C`), leaving Core 3 idle while Main Game Thread held `prio=0x24` (`cores=0x3`). Note: inspected `sub_82655E00` (`timeshift_recomp.29.cpp`), which updates `m_DestinationPacket`/`m_ProgramPacket`/`m_ControlPacket` read by `DrawStateLocked`, so kept intact.
- **Stage B.3 Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - **Unlock 4th Switch Core (Core 3) & Equalize Priority** (`adapter/presentation_hooks.cpp` `5F4DC352...`): Equalized `NativeSwap()` game thread priority `0x24 -> 0x28` (`ideal=0, mask=0x3`); hooked `sub_82145740` (guest worker affinity setter) to split workers `ideal = (guest_hw & 1u) ? 3 : 1` across `mask=0xB` (Cores `0,1,3`, excluding Core 2 `render_thread`, fallback `ideal=1, mask=0x3`) at `prio=0x28`; hooked `sub_821455E8` (`WaitForSingleObject` wrapper) with `1-in-256` per-thread Core probe into `g_game_core_mask`.
  - **`CaptureDelta` Chunk Prefetch** (`upstream/.../queued.cpp` `8D8F4CA2...`, CRLF): Added `__builtin_prefetch(bytes + at)` / `c.shadow + at` over `kDeltaReadChunks` (`39` chunks) in the un-marked `else` path before comparison.
  - **Trim Remaining `linear` Hash Overhead** (`upstream/.../textures.cpp` `77B829FE...`): Lowered `sampled_linear` threshold `64 KB -> 16 KB` (`16384u`); staggered stabilized static (`changes==0 && !unlocked`) `>16 KB` `1-in-16` (`>=4f`) and `<=16 KB` `1-in-4` (`>=8f`).
  - **`-O3` on Managed Guest Wait & Adapters** (`integration/CMakeLists.txt` `00A70547...`): Upgraded `timeshift_renut_guest_wait` (`timeshift_recomp.2/78/153.cpp`) `-O2 -> -O3` and added `-O3` to `resource_hooks`, `device_calls`, `device_cleanup`, and `callback_hooks`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` hooks, `0` foreign hooks, strong `sub_82145740`/`sub_821455E8` verified via `nm`).
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, NRO ASET PASS (`Title='TimeShift'`, `Author='vasin228'`, `24,973` B icon).
  - `build/app-switch/timeshift.elf`: `2026-11-23 08:24:25`, `560,397,096` B, SHA-256 `1E2A4D2A3E5259260351B981EB23F915599A83822E2C2974BC7B4371A6FCB109`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 08:24:29`, `91,636,165` B, SHA-256 `B6B85CD3B2A34236FC1313A6FBBCA9101921BEAF298923A085415B7518F3CAA3`.
  - Uploaded `/switch/timeshift/timeshift.nro` while prior session PID 163 (`010021c000b6a000`) remained open (`HTTP 201`, `91,636,165` B written, `/files/hash` SHA-256 `match=True`).
- **Next**: User closes running TimeShift session and relaunches via **Hold R Title Override**, plays 1-2 min in `city_part01` (indoors + outdoors), then pull `timeshift.log` + `perf_baseline.csv` to verify `cores=0xB` / `[cpu-pin] worker thread` + `[perf-cpu]` `logic`/`adp` + `[perf-tex]` `linear` improvements before GPU Stage C.

## 2026-11-23 (Part 279) — Stage B.4 (Strict 4-Core Thread Topology & Lock-Free Saber3D Atomics)
- **Measured Stage B.3 Results (`stageB3_timeshift.log`, `stageB3_perf_baseline.csv`, `1428/921/1600 MHz`, `0` errors)**:
  - `linear` hash dropped `2.33–2.89 ms -> 0.20–0.26 ms`, `tex` dropped `4.17–5.15 ms -> 1.94–2.45 ms`, and Core 2 `render_busy_ms` dropped to `9.57–12.46 ms` (`7.76–11.26 ms` CPU sum = `88–128 FPS` capacity).
  - `[cpu-pin]` + `[threads]` exposed why gameplay still ran `16.5–20.2 FPS` (`cores=0x3`, `logic=39.2–50.1 ms`): `TID 04` (physics/sim) swaps on boot while `TID 09` (`hw=3`, Saber3D 3D scene/draw thread) swaps every frame in gameplay, so `NativeSwap()` had overwritten `TID 09` onto Core 0 alongside `TID 04`; Horizon migrated `mask=0xB` workers off Core 3 back onto Cores `0..1`; and 50% of `TID 04`/`TID 09` thread snapshots sat in `sub_8214F008` / `sub_82144E28`, locking `rex::thread::global_critical_region::mutex()` twice per atomic op.
- **Stage B.4 Applied & Mirrored Byte-Identically (`timeshift-nx` & `timeshift-renut`)**:
  - **Strict 4-Core Role Mapping** (`adapter/presentation_hooks.cpp` `AE2EA253...`): Unified `g_thread_core_pinned` + `PinCurrentThread()`; in `sub_82145740`, routed `0x8224A53C` (`TID 04`) -> **Core 0** (`mask=0x1, prio=0x24`), `0x82264AC8 && hw==3` (`TID 09`) -> **Core 1** (`mask=0x2, prio=0x24`), `0x82264AC8 && hw!=3` (`TID 0B`, etc.) -> **Core 3** (`mask=0x8, prio=0x26`), streaming (`0x8220040C`) -> **Core 3** (`mask=0x8, prio=0x2A`), leaving `render_thread` on **Core 2** (`0x4`); preserved existing pin in `NativeSwap()`, and pinned unpinned `sub_821455E8` waiters to Core 3 (`mask=0x8, prio=0x28`).
  - **Lock-Free ARM64 Hardware Atomics** (`adapter/presentation_hooks.cpp`): Strong overrides for `sub_82144E28` (`InterlockedIncrement`), `sub_82144E48` (`InterlockedDecrement`), `sub_82145820` (`InterlockedCompareExchange`), and `sub_8214F008` (Saber3D spinlock with ARM64 `yield` + `std::this_thread::yield()`), eliminating `global_critical_region::mutex()` serialization across all 4 cores.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` AArch64 objects, `26/26` hooks + strong `sub_82144E28`/`4E48`/`5820`/`F008`/`55E8`/`5740`, `0` foreign hooks); `audit_private_trees.py` PASS, `check_profile.py` PASS, NRO ASET PASS (`Title='TimeShift'`, `Author='vasin228'`).
  - `build/app-switch/timeshift.elf`: `2026-11-23 09:08:57`, `560,407,760` B, SHA-256 `56633DC3EF3E9A348F30E5DC95019DCEB85882CE201092234C5D5A70978F2237`.
  - `build/app-switch/timeshift.nro`: `2026-11-23 09:09:00`, `91,636,165` B, SHA-256 `BDDCA24A56F9440F2B5A23AC7E9224E978CF5874A8AA9460A97CCDEA2D59C793`.
  - Uploaded `/switch/timeshift/timeshift.nro` (`PROCESS: {"running":false}`, `HTTP 201`, `91,636,165` B, remote `/files/hash` `match=True`).
- **Next**: User launches TimeShift via **Hold R Title Override**, plays 1-2 min in `city_part01` (indoors + outdoors), then pull `timeshift.log` + `perf_baseline.csv` to verify 4-core isolation (`cores=0xB`), `[cpu-pin]` worker logs, and lock-free CPU speedup before GPU Stage C.

## 2026-11-23 (Part 280) — Stages B.5, C.1–C.4 & B.6 (ZCull, Strip-Resolve Coalescing, 1-Target Depth Resolve, Multi-Core `0xB` Workers, 23x `-O3` Chunks)
- **Stage B.5 + C.1 (`stageB5_perf_baseline.csv`)**: Switched `TID 04`/`TID 09` to dual-core spillover (`mask=0x3, prio=0x24`, away from `AudioWorker`), equalized Core 3 workers at `prio=0x28`, added 3-tier backoff in `sub_8214F008` (`1..16` ARM64 `yield`, `17..64` `this_thread::yield`, `>64` `50 us` `svcSleepThread`), and set `kDepthWordsProbe = 0`. Eliminated `95–137 ms` spinlock spikes (`p95`: `94.84 -> 50.51 ms`, `1% low`: `7.55 -> 19.05 FPS` in `HIGH >=550` draws).
- **Stage C.2 (`stageC2_timeshift.log`)**: Coalesced full-width horizontal strip resolves `(0,0)-(1280,256/512/720)` in `ResolveStateLocked` (`draw.cpp`) into whole-surface Strip 0 handover + deferred Strip 2 clear (`flags & 0x200` depth propagation). Converted main scene depth & color resolves into zero-copy `HANDOVER`, cutting `resolves_drawn` `21.0 -> 15.7/f` and raising `ho[2]` `900 -> 1,500` per `300f` (`HIGH` `51.76 ms` / `19.32 FPS`, `avg_draws=667.4`).
- **Stage C.3 (`stageC3_perf_baseline.csv`, `stageC3_timeshift.log`, 100%-alive courtyard combat)**: Fixed missing `{}` braces in `plume_deko3d_commands.cpp` `invalidateZcullAfterCopy`, enabled Deko3D Hierarchical **ZCull** (`Deko3DZcullOn() = true` in `renut_tuning_shims.cpp`), and added 1-target `ResolveDepth` + tile-bounded `dst_rect` viewports in `draw.cpp`. Hardware GPU time fell **`47.21 ms (21.2 FPS) -> 30.90 ms (32.4 FPS)`**: main `1280x720` draws `30.68 -> 15.73 ms`, `1024x1024` shadows `8.19 -> 5.74 ms`, depth resolves `3.38 -> 2.52 ms`. Heavy `646`-draw combat (`HIGH >=550`) improved from `Stage B.3` `82.33 ms (12.15 FPS)` -> **`42.49 ms (23.54 FPS, p50=42.33, p95=46.42, 1%low=17.97 FPS)`**; `MID` `39.72 ms (25.17 FPS)`; `LOW3D` `31.94 ms (31.30 FPS)`.
- **Stage B.6 + C.4 Production Build (`stageB6_timeshift.log`, `stageB6_perf_baseline.csv`, `1428/921/1600 MHz`, `49°C`)**: Opened 5 Saber3D workers (`0x82264AC8 && hw!=3`) to `mask=0xB` (Cores `0, 1, 3`), compiled 23 hot guest chunks (`2, 3, 14, 19, 25, 29, 38, 40, 43, 45, 62, 63, 66, 68, 78, 79, 84, 104, 112, 130, 136, 145, 153`) at `-O3 -fno-strict-aliasing`, removed temporary `[census]` logging, and disabled `renut_gpu_profile = 0.0` / `banjo_renut_diag_gpu_shaders = false`. Startup combat windows trimmed slightly (`Window 9`: `51.45 -> 50.57 ms`, `Window 10`: `48.49 -> 48.34 ms`, `LOW3D`: `31.72 ms / 31.52 FPS`), confirming remaining heavy-combat ceiling (`~42–48 ms`) is single-thread `TID 09` guest scene CPU traversal at `1428 MHz` (`gpu = 30.9–32.3 ms`, Core 2 `render_busy = 11.45–12.05 ms`). Visuals verified via Switch screenshots at sharp `1280x720`.
- **Final Build, Audits & Switch Deployment (`192.168.0.7:4150`)**:
  - `ninja -C build/core-switch` + `build_switch_app.py` exit `0` (`220` objects: `138` `-O2` recomp + `23` `-O3` guest + `59` core/adapter/SDK; `26/26` hooks; `0` foreign hooks); `audit_private_trees.py` PASS; `check_profile.py` PASS.
  - `timeshift.elf`: `2026-11-23 10:33:52.521`, `629,414,568` B, SHA-256 `78345F423193EDABB9BF58DC8ACC0D93C29FA1A123FF121485DC17AA18FCA0A3`.
  - `timeshift.nro`: `2026-11-23 10:33:56.220`, `91,816,389` B, SHA-256 `CBFF4FB4EC94B5CD6E598312EA5DF13728DAF3AF310B11C370B8FF8137BEDB56`.
  - Deployed `timeshift.toml` (`483` B, `HTTP 201`) + `timeshift.nro` (`HTTP 201`, remote `/files/hash` `match=True`); terminated cleanly (`{"running": false}`).

## 2026-11-23 (Part 281) — Stage B.7 (Native ARM64 `o1heap` Guest Allocator & Core 3 Background Thread Isolation)
- **Root Cause (`user_b6_timeshift.log`, 9,659 frames)**: `mask=0xB` workers and unpinned background threads (`TID 0E..21`, `prio=0x2C`) were preempted on Cores `0–1` by `TID 04`/`TID 09` (`prio=0x24`) mid-dlmalloc while holding spinlock `0x82BC74C0`, plus PowerPC dlmalloc called `REX_ENTER_GLOBAL_LOCK()` on unlock. Across 38 snapshots, `TID 04` spent `60.5%` (`82150D34/1048/11AC`) and `TID 09` spent `34.2%` on the dlmalloc lock (`HIGH` `54.30 ms / 18.42 FPS`, `1% low 9.59 FPS`, `logic` spikes up to `63.86 ms`).
- **Stage B.7 Applied (`adapter/presentation_hooks.cpp` `6763d106...`, mirrored to `timeshift-renut`)**:
  - Replaced guest `malloc`/`free`/`realloc`/`realloc_internal` (`sub_82150CE0`/`1158`/`1D58`/`1A58`) with native ARM64 `O(1)` **`o1heap`** (`16 MiB` arenas on `v40000000` virtual heap + `>=2 MiB` dedicated allocs) protected by libnx `Mutex` (priority inheritance, zero `global_critical_region` locks).
  - Isolated all 5 Saber3D workers (`0x82264AC8 && hw!=3`) + background entries (`sub_828172A8`, `sub_8287F678`, `sub_82B08E80`) onto **Core 3 (`mask=0x8, prio=0x28`)**.
- **Measured Switch Results (`stageB7_timeshift.log`, `stageB7_perf_baseline.csv`, `6,659` frames, `1428/921/1600 MHz`, `53°C`, `0` bad frees)**:
  - `[heap-o1]`: `11` arenas (`176 MiB`), `329K–331K` live blocks, `~110–125 alloc/f`, `~110–124 free/f`, `0` invalid frees; `[threads]` dlmalloc lock contention fell `60.5% / 34.2% -> 0%`.
  - **`W10..W27` (`frames 2700..8100`) locked steady at `gframe = 35.50–36.49 ms` (`logic = 28.73–29.68`, `adp = 3.13–3.21`, `orig_d3d = 3.21–3.33 ms`, Core 2 `ren = 8.70–8.76 ms`)**.
  - CSV (`frames 6360..6659`, `n=300`): **`HIGH (>=550, avg 631.9 draws)`** `54.30 -> 35.86 ms` (**`18.42 -> 27.89 FPS`**, `p50=35.14`, `1%low 9.59 -> 22.08 FPS`, **`gpu=27.85 ms = 35.90 FPS`**); **`MID`** `34.48 ms` (**`29.00 FPS`**); **`LOW3D`** `32.33 ms` (**`30.93 FPS`**, `1%low 28.81`). Sharp `720p` visuals verified via `b7_step_1/6/9.jpg`.
- **Build & Switch Deployment (`192.168.0.7:4150`)**:
  - `ninja` + `build_switch_app.py` exit `0` (`220` objs, strong `T` hooks verified via `nm`, `audit_private_trees.py` PASS, `check_profile.py` PASS); `timeshift.elf`: `2026-11-23 11:16:54.434`, `629,595,736` B, `SHA256=EBBDCD18C0561128AB36B67F45D77B6CC0DA7D7257F734A29E92A28781863213`; `timeshift.nro`: `2026-11-23 11:16:58.252`, `91,816,389` B, `SHA256=08E9F1170DF6B6EEF1CF68E0BF6749B79D891FE4883069CAF589A1C21DCD188E`; uploaded `HTTP 201` (`/files/hash` `match=True`), terminated cleanly (`{"running":false}`).

## 2026-11-23 (Part 282) — Lighting Flicker Fix, Live In-Game FPS HUD, & Continuous 1s FPS Logging
- **Lighting Flicker Root Causes & Fixes (verified in `flicker_0..3.jpg` / `flicker_b_0..5.jpg`, ~150 ms / 4-frame oscillation)**:
  - **Colour Handover Read-Write Hazard** (`upstream/.../draw.cpp` `a04943c7...`): `TextureSlotMemo` and `TexturesReuse` only checked `targets.depth` before `BindTargets`, allowing a pass binding surface `S` as a color RT to reuse a handed-over slot of `S` and read+write simultaneously. Added `TargetsHaveSurface(g_handover_targets, m.fx.handover_surface)` and color RT tracking in `TexturesReuse`.
  - **Stale Float Constants on `DrawIndexed`** (`upstream/.../queued.cpp` `e9af78e6...`): Extended NEON `Line64Differs` scan over all 64 VS + 64 PS constant groups against `c.shadow` to ALL TimeShift chain draws (not just `up_vertices != nullptr`), preventing dropped lighting constants when guest `Clear`/`Resolve`/`BeginTiling` mutates `m_Pending[0..1]`.
  - **Small Linear Texture Stagger & Clear-Repeat** (`textures.cpp` `beb3cc19...`, `timeshift.toml` `2a5c730f...`): Removed 4-frame skip stagger on `<= 16 KB` linear textures (`RefreshLinearLocked`), disabled `sa2_renut_clear_repeat = false` to avoid EDRAM alias stale clears, and exposed `renut_deko3d_zcull` (default `true`).
- **Real-Time FPS HUD & Continuous 1s Logging**:
  - Added lock-free 0.25s HUD atomics (`adapter/perf_breakdown.h` `6d2c8e69...`) and `UpdateLiveFpsStats` (`adapter/renut_tuning_shims.cpp` `0513ea20...`) writing per-second rows (`time_s,frames,fps,min_fps,max_fps,avg_ms,p95_ms,max_ms,gpu_ms,ren_ms,draws`) to `sdmc:/switch/timeshift/fps_log.csv` + `[fps]` lines in `timeshift.log`.
  - Added self-contained `128x64` RGBA8 5x7 outlined bitmap font + top-center FPS/min/frametime/GPU HUD in `overlay/overlay_drawer.cpp` (`d5e751bb...`), gated by `renut_fps_overlay = true`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - Mirrored all 7 edited files to `timeshift-renut` (SHA-256 verified); `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs).
  - `timeshift.elf`: `2026-11-23 12:09:05`, `629,896,128` B, `SHA256=80CEE5F3F07EFFF9C4C1695FDFFF1FAB268FC89101807D96E96B76353DA8795F`.
  - `timeshift.nro`: `2026-11-23 12:09:08`, `91,832,773` B, `SHA256=542CD7C6209B6F914332BC12D8D494AACEE3C5D6E9DFEA5AA524B3C37DF76231`.
  - Uploaded `timeshift.nro` (`HTTP 201`, `91,832,773` B) and `timeshift.toml` (`HTTP 201`, `535` B) while user's active session (`PID 175`) remained running untouched; remote `/files/hash` `match=True` for both.

## 2026-11-23 (Part 283) — CPU+GPU Bottleneck Profiling, All-161 `-O3`, & Lighting Stability Proof
- **Lighting Flicker Proof (`d0_flicker_0..5.jpg`, 0.15s spacing)**: Mean scene luma `[39.57, 39.59, 39.62, 39.64, 39.67, 39.66]`, spread **`0.1`** (0% strobe); top-center FPS HUD (`FPS 15.8  MIN 15.2  63.2MS  GPU 27.8MS`) verified crisp and readable.
- **Live Profiling (`live_b8_fps_log.csv` 380s + `d0_timeshift.log` 1 kHz `LR` sampler & GPU pass profiler)**:
  - **Stock (`1020/460/1600 MHz`)**: `17.5 FPS` avg (`13–21 FPS`), CPU `logic=39–65 ms`, GPU `40–46 ms` (max `58.1 ms`).
  - **Overclock (`2193/1228/2266 MHz`)**: `29.8 FPS` avg (`21.2–43.4 FPS`), GPU `17–28 ms` (max `33.1 ms`), drops bound by single-thread `logic=35–42 ms`.
  - **GPU breakdown (`1428/921/1600 MHz`, `28.74 ms` @ `801 draws`)**: `1280x720 color+depth` `15.15 ms` (`536 draws`), `1024x1024 depth` `5.50 ms` (`255 draws`), `1280x720 color` `3.31 ms`, `depth resolves` `2.77 ms`, `resolves` `1.23 ms`. At stock `460 MHz`, GPU scales to `~46–56 ms` (> `33.3 ms` budget).
  - **CPU 1 kHz `LR` profile**: `TID 04` waits `15–41%` in `8280DCBC`; active `TID 04`/`TID 09` CPU cost (`logic=41–55 ms` at `800+ draws`) is distributed broadly across Saber3D/Havok (`1–3%` per site) plus `adp=6.4 ms` + `orig_d3d=5.0 ms`.
- **Stage D.1 Applied & Verified (`integration/CMakeLists.txt`, `tools/build_switch_app.py`, `timeshift.toml`)**:
  - Compiled all `161` `timeshift_recomp.*.cpp` chunks at `-O3 -fno-strict-aliasing` (`0` retained `-O2` objs), set `renut_anisotropic_filtering = "2x"`, removed temporary `CpuSampler`, restored `renut_gpu_profile = 0.0` / `banjo_renut_diag_gpu_shaders = false`.
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs, `26/26` hooks).
  - `timeshift.elf`: `2026-11-23 13:13:29`, `1,106,253,160` B, `SHA256=BC108F47F4BD6BA266A5C891966AD2DD80FCABFE7FCDB31C53E2BFAC484F6BAD`.
  - `timeshift.nro`: `2026-11-23 13:13:34`, `93,196,741` B, `SHA256=E74F9B8CEB6CC94312A3C10E1327B5B17B291187A7ADBF558C4490F78A589BE6`.
  - Deployed `timeshift.nro` (`HTTP 201`) & `timeshift.toml` (`HTTP 201`, `/files/hash` `match=True`); verified in live Switch run (`d0_timeshift.log`, terminated cleanly `{"running":false}`).

## 2026-11-23 (Part 284) — Stage D.2 (Depth Compression, Fast `DrawIndexed` Constants, `0xB` Workers, VSync Off, FMA)
- **User Playtest (`live_d1_fps_log.csv`, 516s)**: `LOW(<350)` `36.02 FPS`, `MID` `32.75 FPS`, `HIGH(>=550, avg 715 draws)` `24.54 FPS` (`gpu=28.05 ms`, `ren=11.66 ms`).
- **Stage D.2 Applied (`timeshift-nx` & `timeshift-renut`)**:
  - Enabled hardware depth compression (`renut_deko3d_depth_compression = true`, `Deko3DDepthCompressionOn()` in `renut_tuning_shims.cpp`).
  - Avoided redundant 8 KB NEON constant scans on ordinary `DrawIndexed` draws (`queued.cpp` `scan_all_consts`, set only on `Rebase`/`QueueClear`/`QueueResolve`/`up_vertices`).
  - Allowed 5 Saber3D workers (`0x82264AC8 && hw!=3`) onto `mask=0xB, prio=0x28` (`presentation_hooks.cpp`), disabled `renut_vsync = false` (`timeshift.toml`), and added `-fno-math-errno -ffp-contract=fast` (`integration/CMakeLists.txt`).
- **Build & Deployment**:
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs).
  - `timeshift.elf`: `2026-11-23 21:06:25`, `1,105,768,040` B, `SHA256=F8D145946DDE913BC4067601A66BC056C57CC3EB510F14D92A9E7EB9C0C1511A`.
  - `timeshift.nro`: `2026-11-23 21:06:30`, `93,151,685` B, `SHA256=6097C1C20D5BE0F27140987605734DBA53792BB0C7585384745F6ABE16D59EAA`.
  - Uploaded `timeshift.nro` (`HTTP 201`, `93,151,685` B) & `timeshift.toml` (`HTTP 201`, `574` B).

## 2026-11-23 (Part 285) — Stage D.3 (Lock-Free `DeviceRegistry` Fast-Path & Zero Per-Draw Telemetry)
- **Applied (`adapter/device_registry.h`, `adapter/device_registry.cpp`, `adapter/perf_breakdown.h`, mirrored to `timeshift-renut`)**:
  - Added `fast_addr_` / `fast_entry_` lock-free lookup in `DeviceRegistry::AcquireIfOwned` when single active device is bound, removing ~850 `mutex_` locks + ~1,700 `std::shared_ptr` refcount ops per frame while preserving `serial` lock protection against `Retire`/`Erase`.
  - Gated `ScopedTick` behind `TIMESHIFT_DETAILED_TELEMETRY` (`0` default), eliminating ~12,000 `cntpct_el0` reads + atomic `fetch_add` per frame in `draw.cpp` and `textures.cpp` while keeping FPS HUD, `fps_log.csv`, `gpu_ms`, `render_busy_ms`, and `draws` active.
- **Build & Switch Deployment (`192.168.0.7:4150`)**:
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs).
  - `timeshift.elf`: `2026-11-23 22:50:31`, `1,105,835,752` B, `SHA256=BFE5BDDBB7F1D49202FEDDC9FA75D383DF61E766E204C5CF565CCB702827131A`.
  - `timeshift.nro`: `2026-11-23 22:50:37`, `93,151,685` B, `SHA256=6B1A40A01B770DF4B3179EB1BC16D7B93454311D1F9E6FFE155C10A39F262E30`.
  - Uploaded `timeshift.nro` (`HTTP 201`, `93,151,685` B, `/files/hash` `match=True`). Ready for user playtest.

