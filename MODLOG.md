# TimeShift ReXGlue Port — Chronicle (MODLOG)

> Note: Historical log entries are archived:
> - Parts 1–80: [MODLOG_ARCHIVE_1_80.md](file:///d:/Anti/timeshift-recomp/MODLOG_ARCHIVE_1_80.md) (Recompilation, CodeGen, VFS, PC level loading).
> - Parts 81–140: [MODLOG_ARCHIVE_81_140.md](file:///d:/Anti/timeshift-recomp/MODLOG_ARCHIVE_81_140.md) (Mesa NVK setup, initial hardware boot, draw sweeps).

> - Parts 141-157: [MODLOG_ARCHIVE_141_157.md](MODLOG_ARCHIVE_141_157.md) (Shader profiling, prior hypotheses, EDRAM diagnosis).

> - Parts 158-161: [MODLOG_ARCHIVE_158_161.md](MODLOG_ARCHIVE_158_161.md) (Native integration, early resolve fixes).

> - Part 162: [MODLOG_ARCHIVE_162.md](MODLOG_ARCHIVE_162.md) (Original investigation; later corrections are recorded in Parts 165-166).
> - Parts 215-218: [MODLOG_ARCHIVE_215_218.md](MODLOG_ARCHIVE_215_218.md) (LOD/Mipmap fixes, PPC thunk adjustors, XMA2 header fix).
> - Parts 219–225: [MODLOG_ARCHIVE_219_225.md](MODLOG_ARCHIVE_219_225.md) (Switch audio, I/O, renderer and hardware work).
> - Parts 226–265: [MODLOG_ARCHIVE_226_265.md](MODLOG_ARCHIVE_226_265.md) (Hardware OC, deko3d bringup, review fixes, `08034ba` sync, thunks, CSV telemetry, 162 captured shaders).
> - Parts 266–285: [MODLOG_ARCHIVE_266_285.md](MODLOG_ARCHIVE_266_285.md) (`08034ba` secondary-stage/mip fixes, `GetTextureGradients` 4,887 rebuild, EDRAM 4xMSAA particles, 4-core pinning, `o1heap`, ZCull, `-O3` Stages A–D.3).

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
| **Parts 266–285** | Shaders, Soft Particles & FPS Scaling | Secondary-stage/mip fixes, `GetTextureGradients` (`850` PS), `4,887`-shader rebuild, conditional EDRAM `4xMSAA` particles, strict 4-core pinning, lock-free atomics, `o1heap`, ZCull, `renut` chain reuse/handover, depth compression, FPS HUD & `-O3` (`~25-35+ FPS`). |
| **Parts 286–291** | Audio Fix & Complete 15,961 Shaders | `renut_skip_orig_d3d`, `Hold +` `LEVEL JUMP`, `4.6 ms` `AudioWorker` pacing + XMA/FMOD Core 1/2 pinning (`rep4` `42% -> 0.03%`, clean audio), all `26` `.s3dpak` + `4` `.sdrc` `15,961`-shader rebuild + `ShaderCacheEntry` metadata fix. |
| **Part 292** | Stages 1–7b Perf & Live FMOD Abort Fix | Zero-offset Switch views, `ppc_inline_helpers.h`, native CRT/FMOD DSP/mixers, FSR1/resolution scale, Horizon spinlock backoff, Black-Box Watchdog, and live EL0 Data Abort fix (`TryNativeFmodResampler` 32-bit VA wrap + `sub_828C80D0` NaN clamp). Verified 13.5 min across 3 levels: 0 freezes, 0 audio drops, 29–30 FPS skyscraper, 20–23 FPS heavy explosions (`sim` thread barrier). |

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

Earlier entries: [Parts 266–285](MODLOG_ARCHIVE_266_285.md).

## 2026-11-23 (Part 286) — Stage D.4 (`renut_skip_orig_d3d` Bypass for Redundant Ring Writes & UP Copies)
- **Stage D.3 Playtest (`live_d3_fps_log.csv`, 182s @ `1428/921/1600 MHz`)**: `LOW` `31.34 FPS`, `MID` `25.43 FPS`, `HIGH` (`avg 689 draws`) `22.97 FPS` (`logic=32-35 ms`, `adp=3.0-3.8 ms`, `orig_d3d=3.5-4.7 ms`, `gpu=26-33 ms`).
- **Stage D.4 Applied (`adapter/draw_hooks.cpp`, `timeshift.toml`, mirrored to `timeshift-renut`)**:
  - Added CVar `renut_skip_orig_d3d = true`: skips redundant `__imp__sub_82655E00` ring-buffer packet writes in `DrawIndexed` (since `draw.cpp`/`surfaces.cpp` never read `m_DestinationPacket`, `m_ProgramPacket`, or `EdramModeControl`), and bypasses redundant Xbox ring-buffer vertex/index copies in `DrawVerticesUP` (`sub_8264FFD8`) and `DrawIndexedUP` (`sub_82650550`), replacing them with direct range validation + `ConsumeImmediatePending`.
  - Note: Deferred arbitrary `1024x1024 -> 512x512` shadow target downscale in `surfaces.cpp` because `deko3d_shaders_f0.bin` lacks `PackageScales()` and `ResolveDepth`/`HandOver` assert equal scale between surface and texture.
- **Build & Switch Deployment (`192.168.0.7:4150`)**:
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs).
  - `timeshift.elf`: `2026-11-23 23:04:38`, `1,105,917,968` B, `SHA256=E2925ADBB4D26B79C88AB5BB172E91D2ECA8D73DF34D99D53E51AD49D8D20551`.
  - `timeshift.nro`: `2026-11-23 23:04:44`, `93,151,685` B, `SHA256=A093F05D647CB9D40E0D39642B44A6C66EE267237E79DFC7B4E069F94F92AC52`.
  - Uploaded `timeshift.nro` (`HTTP 201`, `93,151,685` B) & `timeshift.toml` (`HTTP 201`, `601` B); remote `/files/hash` `match=True` for both. Ready for user playtest.

## 2026-11-24 (Part 287) — 43 Captured Pixel Shaders Rebuilt + AF Menu Fix
- **Root Cause (Black Soldier Uniforms, Floors, Chairs, Tires)**: `timeshift.log` (`3,928` lines) logged 13 missing PS draws (`F653FAA645AA4AAC`, `931DEEBBE88545EE`, etc.) and `CaptureMissingShader` dumped `43` new `ps_*.bin` containers to `sdmc:/switch/timeshift/shader_capture/`. Missing PS with `cmask != 0` skipped color writes after Z-prepass (`draw.cpp:4265`), leaving `#000000` silhouettes.
- **Fixes Applied (`timeshift-nx` & `timeshift-renut`)**:
  - Downloaded all `43` `.bin` containers into `analysis/all-containers/`, ran clean single-pass `XenosRecomp` (`4,930` SPIR-V), `deko3d_glsl.py` (`4,930` BDKG v1 entries, `16,523,792` B), and `uam` (`+43` -> `5,815` BDKC v1 DKSH records, `36,730,028` B).
  - Fixed invalid `"off"` -> `"Game"` in `adapter/renut_tuning_shims.cpp` (`kAfModes`) and Switch `timeshift.toml`.
- **Build, Audits & Deployment (`192.168.0.7:4150`)**:
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `ninja` + `build_switch_app.py` exit `0` (`220` objs, `26/26` hooks).
  - `timeshift.elf`: `2026-11-24 05:40:47`, `1,106,314,968` B, `SHA256=8E283F62D88B51C8D1C023D6A236C0727D58CA926C348302EE419543E20285AD`.
  - `timeshift.nro`: `2026-11-24 05:40:56`, `93,385,157` B, `SHA256=175E72E0A0C78B535A56E8F6D46D20AC0263A554CCBDA0A4EBA1283A37A065C5`.
  - Uploaded `timeshift.nro`, `deko3d_glsl.bin`, `deko3d_shaders_f0.bin`, and `timeshift.toml` (`HTTP 201`, `/files/hash` `match=True`). Next: close old session on Switch, relaunch via Hold R Title Override, verify black objects fixed.

## 2026-11-24 (Parts 288–290) — Depth Cmp Off, `LEVEL JUMP`, & All-Level Shader Harvest (`5,403` / `6,287`)
- **Level Jump (`perf_breakdown.h`, `renut_tuning_shims.cpp`, `overlay_drawer.cpp`, `build_switch_app.py`)**: Replaced `sub_821EFC30` wrap with `sub_821EFB10` (intercepts sync & async `GS_MSG_LOAD_LEVEL(1004)`, skips `menu`/`menu.lg`, preserves `.lg`), added `LEVEL JUMP` selector (`OFF` + all 24 `.s3dpak` levels `01 CITY 1`..`24 FINAL`) to `Hold +` menu; synced `prepare_switch_cache.py` & `package_shaders.py` to `timeshift-renut`.
- **Full Shader Bake (`build/uam-host/uam`)**: Synced `+260` user-harvested captures (`516` total in `shader_capture/`) -> `XenosRecomp` (`5,403` SPIR-V) -> patched `uam` (`+299` DKSH) -> `deko3d_glsl.bin` (`18,518,456` B, `SHA256=DF7FBE59...`), `deko3d_shaders_f0.bin` (`6,287` DKSH, `39,984,076` B, `SHA256=58879372...`, `5,403/5,403` GLSL hit).
- **Build & Deploy (`192.168.0.7:4150`)**: Audits PASS, `26/26` hooks (`0` foreign); `timeshift.elf` (`2026-11-24 09:14:13`, `1,109,114,128` B, `SHA256=8825AEF3...`), `timeshift.nro` (`2026-11-24 09:14:19`, `96,055,749` B, `SHA256=93BE3363...`); uploaded & SHA-verified; user save (`ts2_skyscraper_part2`) restored (`HTTP 201`).

## 2026-11-25 (Part 291) — Real-Time Audio Fix & Complete 15,961-Shader Archive Extraction
- **Audio Fix (Verified on Hardware; `rep4` `42% -> 0.03%`, `0` Drops)**:
  - Root cause: `SwitchAudoutDriver` released burst batches while `AudioSystem::WorkerThreadMain` (`prio 0x20`) spun `4-8` callbacks in `5-10 ms` (`4.4x` real-time), lapping FMOD Ex's `4`-slot (`1024`-sample) ring buffer (`81` `RRRR` loops in PCM probe).
  - Applied across `private/sdk/src/audio/` & `adapter/presentation_hooks.cpp` (mirrored to `timeshift-renut` & `/root/lab/rexglue-sdk`): `4.6 ms` (`4600 us`) `svcSleepThread` pacer in `audio_system.cpp`; `2048 B` (`2`-frame) `audout` buffers + Core `1/2` (`mask=0x6, prio=0x20`) in `nop_audio_system.cpp`; Core `2/1` (`mask=0x6, prio=0x26`) + inline `<= 8` in `xma_decoder.cpp`; FMOD thread pinning (`828172A8` Core `1/2`, `"FMOD mixer"` `8287F678` `prio=0x22` Core `2/1`, `82B08E80` `prio=0x25`) in `presentation_hooks.cpp`; `audio_maxqframes = 32` in `timeshift.toml`.
- **Black Rooftop / Level Shaders Fixed (`15,961` GLSL / `16,827` `uam` Variants)**:
  - Root causes: (1) `dump_shaders.py` previously rejected zero-length resource names in level archives, so `10,558` shaders across `26` `.s3dpak` levels + `4` `.sdrc` caches (including rooftop `PS 0DFABC2C953B1089` in `ts2_cityfinal_part01.s3dpak`) were never extracted; (2) patched `deko3d_glsl.bin` entries missing from `shader_cache_switch.cpp` hit empty `ShaderCacheEntry{}` in `s_dyn_entries` (`textureFetchMask = 0`, `floatConstants = 0`), rendering meshes `#000000` black.
  - Extracted & compiled `10,558/10,558` missing shaders via `tools/extract_all_pak_shaders.py` + `XenosRecomp` + `spirv-cross` + `deko3d_glsl.py` + `build/uam-host/uam` (`0` failures); merged `15,961`-entry `shader_cache_switch.cpp` metadata + external `deko3d_glsl.bin` (`59,384,072` B, `0` missing metadata) + `deko3d_shaders_f0.bin` (`16,827` DKSH, `107,205,340` B); switched `deko3d_glsl_switch.cpp` to external-only stub; initialized `textureFetchMask = ~0u` & `floatConstants = ~0u` in `shader_library.cpp` `s_dyn_entries` (`banjo_renut_deko3d.{h,cpp}` `HasGameShader`).
- **Pre-Commit Clean Build & Verification (`192.168.0.7:4150`)**:
  - Stripped temporary `N182`/`N183`/`N185`/`N186` diagnostics, reverted `xma_context.cpp`, generalized `extract_all_pak_shaders.py`, updated `audit_private_trees.py` hashes (`PASS`), rebuilt `librexruntimerd.a` (`ninja -C /root/lab/switch-build rexaudio rexruntime`) + `timeshift.nro` (`220` objs, `26/26` hooks, `0` foreign hooks).
  - Clean `timeshift.nro`: `2026-11-25 00:50:52`, `88,195,069` B, `SHA256=04B0BD3A9E3F504B8615113F337A9892F7294013691991F959DAD50980723035`; deployed via HTTP `PUT` (`HTTP 201`, remote `/files/hash` verified).

## 2026-11-25 (Part 292) — Stages 1–7b CPU/GPU/Audio Optimization, Black-Box Watchdog & Live FMOD Data Abort Fix
- **CPU / Memory / Audio Optimizations (Stages 1–7b)**:
  - Zero-offset Switch guest views (`REX_PHYS_HOST_OFFSET=0`, `xmemory.{h,cpp}`, `guest_memory_switch.cpp`, `guest_memory.h`), `timeshift_pch.h` (`__restrict` + `adapter/ppc_inline_helpers.h`), `timeshift_setter_marks_tags.h` (`tools/gen_timeshift_setter_marks_tags.py`).
  - Native ARM64 overrides (`adapter/presentation_hooks.cpp`): CRT math (`cos/sin/tan/atan2/exp/log/pow/floor/ceil/u64->f64/isfinite`), string/memory ops, FMOD resamplers (`828DEE18`/`828D7818`/`828DC538`/`828DA770`), converter (`828DFCC0`), channel mixers (`828D6248`/`6328`/`6418`/`65E0`, `828C05A8`), and default-off software reverb bypass (`renut_audio_reverb=false`, `828C6470`).
  - GPU & Horizon OS fixes: resolution scaling + FSR1/EASU/RCAS present (`present_ps.hlsl`, `surfaces.cpp`, `present.cpp`, `overlay_drawer.cpp`), `plume_deko3d` tweaks, `svcSleepThread(50000ULL)` spinlock backoff (`sub_8214F008`, `queued.cpp::ChainLock::lock`, `render_thread.cpp::PushRing`), `_IONBF` `stdout` (`windowed_app_main_switch.cpp`), and zero-malloc Black-Box Freeze Watchdog (`WatchdogThreadMain` `prio=0x20` + `rex_kernel_snapshot_threads_trylock` in `kernel_state.cpp` + stage breadcrumbs).
- **Live Freeze Root Cause & Fix (`pid=142` Dissection via `sys-autopilot-plus`)**:
  - Captured Horizon OS EL0 Data Abort dump at `0x4acad000` (`NRO_BASE=0xf4db59000`, `GUEST_BASE=0x99bd54000`, `PC=NRO+0x2a8b104`, `ESR=0x92000006` Level 2 Translation Fault, `FAR=0x992724470 = GUEST_BASE - 0x962FB90`).
  - Root cause: game passed `NaN` (`0x7FFFFFFF`) to `sub_828C80D0` (`FMOD::DSPResampler::setFrequency`), PowerPC `fctidz` emitted `mSpeed=0x80000000_00000000` (`INT64_MIN`), and Stage 6 `TryNativeFmodResampler<true>` used signed 64-bit `ptrdiff_t idx` (`in + idx * 2 = base + in_va - 4 GiB`) instead of 32-bit unsigned PowerPC VA wrapping.
  - Fixed in `adapter/presentation_hooks.cpp`: strict unsigned 32-bit `uint32_t` guest EA wrapping (`GuestReadPtr`, verified `0` `asr`/`sxtw` instructions across all `730` instructions of `TryNativeFmodResampler<true>`), `spd == 0x8000000000000000ULL` silence guard, and `sub_828C80D0` `!std::isfinite` clamp to `0.0`.
- **Build, Deploy & 13.5-Min Hardware Verification (`192.168.0.7:4150`)**:
  - `audit_private_trees.py` PASS, `check_profile.py` PASS, `build_switch_app.py` exit `0` (`220` objects, `26/26` hooks, `0` foreign hooks).
  - `timeshift.elf`: `1,150,042,480` B, `SHA256=6D8494B6A7744E5B4AE80BEBECAAB61F4A87C13EEC86A2F73DC86154FBC87EB4`.
  - `timeshift.nro`: `2026-11-25 08:02:24`, `82,259,965` B, `SHA256=DFD48B8830EC1F88BA31FAEF8E97BF148542CD6D94CD7DD31C275B0C1640FCF1` (`HTTP 201`, remote `/files/hash` matched).
  - Playtest (`s8_timeshift.log`, `807.7s` / `13.5 min` @ `1428/921/1600 MHz`) across `ts2_skyscraper_part4` -> `ts2_zepride1_part01` -> `ts2_zepcrashsite_part01`: **0 freezes**, **0 crashes**, **0 audio drops** (`75,520` `audout` buffers); `sub_828C80D0` caught & neutralized **4 live `NaN` frequencies** (`0x7FFF0000` x2, `0x7FC00000` x2).
  - Performance: `ts2_skyscraper_part4` `29.0–30.0 FPS` (`1% low` up to `26.9`); `ts2_zepcrashsite_part01` `24.6–27.5 FPS`; heavy explosion `ts2_zepride1_part01` `20.2–23.9 FPS` (`avg=44–50 ms`, `GPU=14–18 ms`, `ren=8–9 ms`, `drawCPU=19–27 ms`, `logic - drawCPU GAP = +20..27 ms` proving single-threaded `sim` physics/particles barrier, NOT GPU). GPU texture pool hit `2905/3083 MB` after 3 levels (`28` warnings, cold eviction prevented OOM).
- **Next Step (Stage 8)**: Profile & natively accelerate `sim` thread (`TID 04`) Havok physics/particles CPU hotspots and tune earlier cross-level texture cache eviction.

## 2026-11-26 (Part 293) — `540p` Mode, Fast FidelityFX CAS / FSR 1, & Release Package
- **480p Darkening/Fire Root Cause & Fix (`draw.cpp`, `graphics_options.cpp`)**:
  - Pixel probe (`s8_pixtrace2.log`) proved HDR `256->64->16->4->1` luminance downsampling (`40007820` `853x480` + `4018E3E0`/`4018E520` `427x240`) left stale border texels at scaled resolution (`16x16` -> `4x4` only covered `[0..2]`), causing `1x1` `24EE51C1` (`0x31 -> 0x8B`) and eye adaptation `4018F920` (`0x2A -> 0x76`) to crush exposure `~2x`.
  - Fixed via depthless half-res-or-smaller `1x` viewport/resolve rule (`ComputeViewportSlow`, `ResolveStateLocked`, `SurfaceRenderScale`, keeping square shadow depth maps `>= 1x`). Verified clean on hardware (`480p` darkening/fire resolved).
- **AMD FSR 1 -> Fast Single-Pass FidelityFX CAS + `540p` (`960x540`) Upgrade**:
  - Replaced `480p` (`2/3`, `853x480`) with `540p` (`3/4 = 0.75`, `960x540`, alias kept for saved `"480p"` configs) in `render_scale.h`, `shader_common.h`, `surfaces.cpp`, `overlay_drawer.cpp`, `renut_tuning_shims.cpp`, all `10,447/15,961` affected shaders in embedded `deko3d_glsl.bin` (`70,100,864` B), and `3,534` updated DKSH entries in `deko3d_shaders_f0.bin` (`107,205,340` B, `16,827` records, `SHA256=89764269CCB8AA5AA745E98C8A9B2B5824328B2BD02B4DDCA8FA62C75751FC32`).
  - Added AMD FSR 1 (`fsr_easu_ps.hlsl`, `VideoState` fields appended safely at end of `video.h` after fixing `VideoState` ABI `EINVAL` crash on level load); switched `FSR 1` `Low`/`Med`/`High` to zero-extra-RT single-pass FidelityFX CAS in `present_ps.hlsl` to eliminate the 2-pass `1280x720` GPU ms spike, keeping 2-pass `EASU + RCAS` as optional `"EASU"` mode (`13/13` host shaders passed SPIR-V + `uam`).
  - Disabled Stage 8 `renut_guest_profiler` (`false` in `adapter/presentation_hooks.cpp`), stripped debug probes (`[480t]`/`[480p-pix]`, `RGPU_INFO("FSR 1 EASU...")`), synced `timeshift-nx` <-> `timeshift-renut` (`16/16` files SHA-identical), passed `audit_private_trees.py` & `check_profile.py`.
- **Clean Candidate Build (`timeshift.nro`)**:
  - `timeshift.elf` (`1,220,767,880` B, `SHA256=F4933CF3F93B87F19A89B6EE045B08BD01BB1A59D2904E90635BACC412341EC5`), `timeshift.nro` (`152,453,117` B, `SHA256=06243BE70640881FB2DBC136C608A41ED2BE3984CC0DC28BB8705E232CCC86E7`, `220` objs, `26/26` hooks, `0` foreign hooks, embedded `deko3d_glsl.bin`).
