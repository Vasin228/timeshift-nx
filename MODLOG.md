# TimeShift ReXGlue Port — Chronicle (MODLOG)

> Note: Historical log entries are archived:
> - Parts 1–80: [MODLOG_ARCHIVE_1_80.md](file:///d:/Anti/timeshift-recomp/MODLOG_ARCHIVE_1_80.md) (Recompilation, CodeGen, VFS, PC level loading).
> - Parts 81–140: [MODLOG_ARCHIVE_81_140.md](file:///d:/Anti/timeshift-recomp/MODLOG_ARCHIVE_81_140.md) (Mesa NVK setup, initial hardware boot, draw sweeps).

> - Parts 141-157: [MODLOG_ARCHIVE_141_157.md](MODLOG_ARCHIVE_141_157.md) (Shader profiling, prior hypotheses, EDRAM diagnosis).

> - Parts 158-161: [MODLOG_ARCHIVE_158_161.md](MODLOG_ARCHIVE_158_161.md) (Native integration, early resolve fixes).

> - Part 162: [MODLOG_ARCHIVE_162.md](MODLOG_ARCHIVE_162.md) (Original investigation; later corrections are recorded in Parts 165-166).
> - Parts 215-218: [MODLOG_ARCHIVE_215_218.md](MODLOG_ARCHIVE_215_218.md) (LOD/Mipmap fixes, PPC thunk adjustors, XMA2 header fix).
> - Parts 219–225: [MODLOG_ARCHIVE_219_225.md](MODLOG_ARCHIVE_219_225.md) (Switch audio, I/O, renderer and hardware work).
> - Parts 226–265: [MODLOG_ARCHIVE_226_265.md](file:///d:/Anti/timeshift-nx/MODLOG_ARCHIVE_226_265.md) (Hardware OC, deko3d bringup, review fixes, `08034ba` sync, thunks, CSV telemetry, 162 captured shaders).

## Executive Milestone Summary

| Part Range | Milestone | Key Achievements |
|---|---|---|
| **Parts 1–80** | PC Boot & Mesa NVK | Recompilation complete, 60 FPS menu on PC, Mesa NVK cross-compiled for Tegra X1. |
| **Parts 81–120** | Hardware Bringup | 4.5 GB address space, 64KB page alignment fix, VI surface, hardware FMV & audio. |
| **Parts 121–140** | Shader & Pass Bisect | Identified 780 draws across 5 offscreen passes in menu. Native SPIR-V optimization pipeline. |
| **Parts 141–145** | SPIRV-Tools ARM64 | Cross-compiled Khronos optimizer into engine: eliminated 100% OpStores in guest shaders. |
| **Parts 146–150** | Autonomous Testing | Stabilized sys-autopilot (1.69 MB BSS, 0 crashes). Title Override autonomous loop working. |
| **Parts 158-162d** | Native Renderer and Hardware Tests | Native Vulkan integrated; oversized-copy path restricted, but stale composition and 0.8-2.5 swaps/s in heavy first-level rendering persist. |
| **Part 163** | Renderer Correctness Fixes | Three Vulkan regression checks pass and fail against prior code; new NRO uploaded and verified, awaiting user hardware test. |
| **Parts 165-168** | Vertex Bounds and Tiled Joins | Removed ~15.3 GiB/frame vertex copies; hardware menu full-scene assembly restored. |
| **Parts 170-175** | Memory, Gameplay and Shader Cache | Reached gameplay; persistent optimizer cache works; image corruption remains. |
| **Part 176** | Typed Color Resolves | Correct format/exponent conversion and explicit depth role pass GPU tests; user reports no visible benefit. |
| **Parts 177-180** | Refraction Capture / Constant Alias Clear | Located contaminated normal buffer; Part178 zero-only guard inactive. Part180 supports exact constant D24/stencil words, passes exhaustive/GPU checks; local ready. |
| **Parts 182-184** | ReNut/deko3d Adaptation | Frozen Vulkan reference;2720 shaders prepared; TimeShift read view and27 AArch64 core objects compile. No experimental game binary yet. |
| **Part 185** | Original-Object Resource Bridge | Four dormant TimeShift hooks; all2720shader identities retained through original construction; real-SDK bootstrap bridge and physical-alias routing verified offline. No new game NRO. |
| **Part 186** | Dormant Draw Bridge | Seven additional TimeShift entries; deferred vertex/index ownership and original clear bounds verified offline. Tiling/segment completion remains unimplemented; no new NRO. |
| **Part 187** | Native Tiling CPU Protocol |3624 original/native comparisons; real clear values, flags, rectangles and predicates retained. Production segment/clear/resolve services still missing; no NRO. |
| **Part 188** | Per-Device Source Ownership | Concrete SDK/ReNut source runtime; safe delayed reuse/close, fence3/1 bootstrap fix,13scenarios+sanitizers. Lifecycle/tiling hookup remains; no NRO. |
| **Part 189** | Provisional Device Registry | Five dormant lifecycle/allocator entries, safe creation rollback/deferred retirement;12integration cases+sanitizers. Full cleanup/routing/link still pending. |

| **Part 190** | Concrete SDK Tiling Host | Three dormant entries; retained ClearF/Resolve CPU closure, stack/read-only fixes; four integrated sanitizer scenarios,44 AArch64 units. No game NRO. |

| **Part 191** | Public Source-Fence Query | Correct one-fence/global-device ABI; callbacks/segments/lifetime retained;14 sanitizer cases/10 original comparisons,45 AArch64 units. No NRO. |

| **Part 192** | Retained Source Waits | Six locked reads, original metrics/callbacks, no false5s host deadline;10 sanitizer cases,50 AArch64 units/897 retained guest functions. No NRO. |

| **Part 193** | GPU-aware Retirement / Migration Pause | Original resource ownership retained after GPU barrier;16 sanitizer cases,52 AArch64 units. User stopped implementation for laptop migration; no NRO. |
| **Part 194** | Reachable Ownership Boundary (Ticket 1) | InsertCallback sub_8264F6B8 hooked; legacy worker 82652698 suppressed; in-order guest execution/post-run ack; open tiling protected; 7 callback scenarios, 53 AArch64 units. No NRO. |
| **Part 195** | Draw and Resource Leases (Ticket 2) | All 7 draw hooks and resource destroy hold DeviceRegistry::Lease; unowned fallback; UpScope nested single-queueing; reentrant Release quarantine; 12704B whole snapshots & dirty tags preserved; DrawVertices/tessellation/command-buffers proven absent; 0 foreign hooks; 53 AArch64 objects with 24 strong dormant hooks; all sanitizers PASS. |
| **Part 196** | Resolve/Clear Format & CPU Closure (Ticket 3) | 3-field ResolveParameters (+0/+4/+8, bounds < 64); stack forwarding `Word(frame_+100)`; pre-mutation degenerate rect guards; `std::ldexp` CRT helper `sub_828E80A8` & FPSCR host mask; real-CPU D24FS8 resolve; 3,624 protocol comparisons, 4 ASan/UBSan scenarios, 5 challenge scenarios PASS; 53 AArch64 objects with 24 strong dormant hooks; gate PASS. |
| **Part 197** | Full CPU Setup/Cleanup Boundary (Ticket 4) | Real SDK closures 82653DF8/82653738/826539A0/826540C8, 22.4KB device, 128B align, 4800B side buffer at 16712, auto-surface map 14812/14816/14808; staged failure rollback & previous-global restoration; temporary vs title device isolation; aliased refcounts & no-replay quarantine; two-stage quiescence (guest source/callback vs render-thread GPU barrier); all 16 cleanup scenarios PASS under ASan/UBSan; 14 lifecycle scenarios PASS; 53 AArch64 objects with 24 strong dormant hooks; 0 foreign hooks. |

---

## Active Log: Hardware Verification (User-Controlled Launches)

Earlier entries: [Parts 162c-163](MODLOG_ARCHIVE_162c_163.md).

Earlier entry: [Part164](MODLOG_ARCHIVE_164.md).

Earlier entry: [Part165](MODLOG_ARCHIVE_165.md).

Earlier entry: [Part166](MODLOG_ARCHIVE_166.md).

Earlier entry: [Part167](MODLOG_ARCHIVE_167.md).

Earlier entry: [Part168](MODLOG_ARCHIVE_168.md).

Earlier entry: [Part169](MODLOG_ARCHIVE_169.md).

Earlier entry: [Part170](MODLOG_ARCHIVE_170.md).

Earlier entry: [Part171 and follow-up](MODLOG_ARCHIVE_171.md).

Earlier entry: [Part172](MODLOG_ARCHIVE_172.md).

Earlier entry: [Part173 and hardware follow-ups](MODLOG_ARCHIVE_173.md).

Earlier entry: [Part174](MODLOG_ARCHIVE_174.md).

Earlier entry: [Part175](MODLOG_ARCHIVE_175.md).

Earlier entry: [Part176](MODLOG_ARCHIVE_176.md).

Earlier entry: [Part177](MODLOG_ARCHIVE_177.md).

Earlier entry: [Part178](MODLOG_ARCHIVE_178.md).

Earlier entry: [Part179](MODLOG_ARCHIVE_179.md).

Earlier entry: [Part180](MODLOG_ARCHIVE_180.md).

Earlier entry: [Part181](MODLOG_ARCHIVE_181.md).

Earlier entry: [Part182](MODLOG_ARCHIVE_182.md).

Earlier entry: [Part183](MODLOG_ARCHIVE_183.md).

Earlier entry: [Part184](MODLOG_ARCHIVE_184.md).

Earlier entry: [Part185](MODLOG_ARCHIVE_185.md).

Earlier entry: [Part186](MODLOG_ARCHIVE_186.md).

Earlier entry: [Part187](MODLOG_ARCHIVE_187.md).

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
