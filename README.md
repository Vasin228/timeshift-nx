<div align="center">

[English](README.md) | [Русский](README_RU.md)

<br />

<img src="assets/banner.jpg" alt="TimeShift for Nintendo Switch" width="100%" />

# TimeShift NX

**Native Xbox 360 Static Recompilation Port of TimeShift for Nintendo Switch (`deko3d`)**

<p align="center">
  <img src="https://img.shields.io/badge/Nintendo%20Switch-Horizon%20OS-E60012?style=for-the-badge&logo=nintendo-switch&logoColor=white" height="28" alt="Nintendo Switch" />
  <img src="https://img.shields.io/badge/Renderer-deko3d%20(Tegra%20X1)-76B900?style=for-the-badge&logo=nvidia&logoColor=white" height="28" alt="deko3d" />
  <img src="https://img.shields.io/badge/Arch-AArch64%20%7C%20C%2B%2B23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" height="28" alt="AArch64 C++23" />
  <img src="https://img.shields.io/badge/License-BSD--3--Clause-4A5568?style=for-the-badge" height="28" alt="BSD-3-Clause" />
</p>

<p align="center">
  <a href="https://boosty.to/vasin228">
    <img src="https://img.shields.io/badge/Support%20the%20Project-Boosty%20Donate-F15F2C?style=for-the-badge&logo=boosty&logoColor=white" height="32" alt="Support on Boosty" />
  </a>
</p>

</div>

---

The project has reached a working, playable state on physical Nintendo Switch hardware: it compiles into a standalone `.nro` file, runs natively via `deko3d` without emulators, plays cutscenes, operates menus, saves progress and checkpoints, loads campaign levels, and supports controls, physics, time manipulation mechanics, clean XMA2/FMOD audio, and complete campaign shaders (`15,961` translated / `16,827` `uam` variants). Gameplay runs at ~25-35+ FPS at 720p on retail hardware after CPU/GPU optimization passes.

Contributions and Pull Requests are always welcome.

---

## Project Completion Status

Overall Port Readiness: **`[█████████░] 95%`**

| Subsystem | Progress | Status |
|---|---|---|
| **Core, Memory, Saves & Controls** | `█████████░` **95%** | Static recompilation, 2.4–3.0 GB heap, `o1heap`, saves/checkpoints, Joy-Con/Pro, time powers |
| **Graphics & Shaders (`deko3d`)** | `██████████` **100%** | `15,961` GLSL / `16,827` `uam`-compiled shaders across all 26 `.s3dpak` + 4 `.sdrc` archives, sharp mipmaps/POM, soft particles, `Hold +` overlay menu |
| **Audio (`XMA2` / `audout`)** | `█████████░` **95%** | Real-time XMA2 + FMOD Ex + `audout` audio fixed via 4.6 ms inter-frame pacer, 2048 B buffers, 8-context inline decode, and Core 1/2 pinning |
| **Performance (Switch Hardware)** | `████████░░` **80%** | Playable `~25-35+ FPS` (`1280x720`) via `-O3`, 4-core pinning, ZCull, lock-free registry & D3D ring bypass |

---

## What Works Right Now

- **Static Recompilation:** Original Xbox 360 guest code compiled directly to 64-bit ARM (AArch64) for Horizon OS.
- **Native Renderer:** Runs via Nintendo Switch native `deko3d` API (direct hardware access to Tegra X1 GPU).
- **Clean Real-Time Audio (`XMA2` / `FMOD` / `audout`):** 4.6 ms `AudioWorker` inter-frame pacer, 2048 B `audout` buffers (`2` guest frames), `8`-context inline XMA decoding, and Core 1/2 thread pinning eliminate FMOD ring-buffer lapping and distortion in dialogue, music, and combat SFX.
- **Complete All-Archive Shader Cache & Sharp Textures:** `15,961` translated GLSL shaders and `16,827` patched-`uam` `DKSH` binaries across all 26 `.s3dpak` archives + 4 `.sdrc` caches (`shader_cache_switch.cpp` metadata + `deko3d_glsl.bin` + `deko3d_shaders_f0.bin`), Xenos `GetTextureGradients` support, and non-zero fallback metadata in `shader_library.cpp` prevent missing/black meshes and glowing objects.
- **Fire, Smoke & Explosions (Soft Particles):** Conditional EDRAM 4xMSAA aliasing (`640x360` soft-particle buffers onto `1280x720` targets) restores campfires, volumetric smoke, grenades, and explosion effects with intact 1:1 camera framing.
- **In-Game Graphics & Level Menu (`Hold +`):** Real-time FPS/GPU HUD, toggles for shadows, draw distance, VSync, anisotropic filtering, D3D ring bypass, ZCull, depth compression, and a 24-level campaign `LEVEL JUMP` selector.
- **Save System & Checkpoints:** Fully implemented and working! Profile saves, new game autosaves, and in-game checkpoints are created and loaded properly on Horizon OS SD card storage (`sdmc:/switch/timeshift/user/`).
- **Intro Cutscenes:** Bink Video playback works.
- **UI & Menus:** Main menu, options, profile save/load, pause screen are all working.
- **In-Game Gameplay:** Campaign levels load and play cleanly.
- **Controls & Input:** Full support for Joy-Cons and Pro Controller (simultaneous dual-input for handheld and docked play).
- **Suit Abilities:** Slow time, time stop, and time rewind affect the world and physics correctly.
- **Weather & Storm Occlusion:** Atmospheric lightning flashes and volume light properly occluded inside rooms (fixed by Oleksa126).
- **Auto-Sleep Inhibition:** Screen dimming and auto-sleep are disabled during gameplay.
- **Memory Management:** Dynamic process heap with native ARM64 `o1heap` guest allocator and backing memory granule recycling.
- **Performance:** Playable ~25-35+ FPS at 720p on retail Switch hardware.

---

## Known Issues (What Needs Fixing)

If anyone decides to pick this up, here is the main remaining area for further improvement:

### 1. Further Performance Headroom in Heavy Scenes
* **Symptom:** Heavy outdoor combat scenes with 600–700+ draw calls can still dip below 30 FPS when full shadows are enabled (toggleable via `Hold +` menu).

*(Previously listed audio distortion/stuttering, remaining black level geometry/rooftop shaders, texture LOD, glowing/flickering objects, shadow dashes, and `10-15 FPS` bottlenecks were addressed in Parts 270–291 via `4.6 ms` `AudioWorker` pacing + XMA/FMOD Core 1/2 pinning, full `15,961`-shader `.s3dpak`/`.sdrc` extraction + `ShaderCacheEntry` metadata bake, `GetTextureGradients`, conditional EDRAM 4xMSAA aliasing, depth-compression control, `-O3`, 4-core pinning, ZCull, lock-free `DeviceRegistry`, and `renut_skip_orig_d3d`.)*

---

## Directory Structure

```
timeshift-nx/
|-- adapter/              # C++ adapter layer bridging game calls to deko3d & Switch
|   |-- timeshift_app.cpp          # Application lifecycle and initialization
|   |-- renut_tuning_shims.cpp     # Depth, ZCull, Hold+ menu, level-jump & EDRAM hooks
|   |-- perf_breakdown.h           # Telemetry HUD & campaign level table
|   |-- draw_hooks.cpp             # Render pass interception & D3D bypass
|   `-- tiling_hooks.cpp           # XGraphics texture tiling hooks
|-- upstream/sa2-nx/      # renut_engine runtime (deko3d backend, shader translator, overlay)
|-- private/              # SDK (audio, kernel, memory) and recompiled C++ sources
|   |-- app/generated/             # Recompiled game code (timeshift_recomp.*.cpp)
|   `-- sdk/                       # rexglue (audio, kernel, memory)
|-- tools/                # Build and deployment scripts
|   |-- build_switch_app.py        # Links timeshift.nro
|   |-- extract_all_pak_shaders.py # Extracts shaders across all .s3dpak and .sdrc archives
|   |-- prepare_switch_cache.py    # Prepares shaders and assets
|   `-- deploy_candidate.py        # Deploys to Switch via FTP
|-- MODLOG.md             # Development chronicle (Parts 286-291; earlier in archives)
|-- deko3d_shaders_f0.bin # Precompiled deko3d shader cache
`-- timeshift.toml        # Game configuration file for Switch
```

---

## How to Build

### Prerequisites:
- **devkitPro** with packages:
  - `devkitA64` (`aarch64-none-elf-g++`)
  - `libnx`
  - `deko3d`
- **Linux or WSL2** (Ubuntu/Debian).
- **Python 3.10+**, **CMake**, **Ninja**.

### Build Commands:

1. **Build internal runtime (`rexruntime`):**
   ```bash
   cd private/sdk
   ninja -C /path/to/switch-build rexaudio rexruntime
   ```

2. **Build Switch binary (`timeshift.nro`):**
   ```bash
   python3 tools/build_switch_app.py
   ```
   The output binary will be created at: `build/app-switch/timeshift.nro`.

---

## Running on Nintendo Switch

> **Important:** Original game assets are copyrighted and NOT included in this repository. You must supply your own extracted TimeShift game files (Xbox 360 version).

1. Create a folder on your Switch SD card:
   ```
   /switch/timeshift/
   ```
2. Copy the following files into that folder:
   - `timeshift.nro`
   - `timeshift.toml`
   - `deko3d_glsl.bin` (if built externally from your game dump)
   - `deko3d_shaders_f0.bin`
   - `game/` folder with extracted TimeShift game data.
3. **Launch ONLY via Title Override:**  
   Hold **R** while launching any installed retail game on your Switch to open hbmenu with full memory access.  
   *Do NOT launch via the Album (Applet Mode), or the game will crash due to out-of-memory errors!*

---

## In-Game Graphics & Level Menu (`Hold +`)

Hold **`+`** for ~0.5 seconds at any time (main menu or in-game) to open the **RENUT GRAPHICS MENU**.  
*(A quick tap of `+` still opens the normal game pause menu.)*

- **Controls:** `D-Pad Up/Down` to move, `D-Pad Left/Right` or `A` to change, `B` or `Hold +` to close.

| Option | Values | Description |
|---|---|---|
| **FPS HUD** | `ON` / `OFF` | Shows/hides real-time FPS, min FPS, frame time, and GPU time at the top |
| **SHADOWS 1024** | `ON` / `OFF` | Toggles dynamic 1024x1024 shadow maps (turn `OFF` for extra FPS in heavy scenes) |
| **DRAW DIST** | `MAX`, `200M`, `120M`, `80M`, `55M`, `40M` | Caps camera far-plane draw distance |
| **VSYNC** | `ON` / `OFF` | Toggles vertical synchronization |
| **ANISO FILTER** | `Game`, `2x`, `4x`, `8x` | Anisotropic texture filtering level |
| **SKIP D3D COPY** | `ON` / `OFF` | Bypasses redundant original Xbox D3D ring-buffer writes/copies |
| **ZCULL RST** | `ON` / `OFF` | Hardware ZCull optimization *(restart required)* |
| **DEPTH CMP RST** | `ON` / `OFF` | Hardware depth compression *(restart required; keep `OFF` if shadow grid dashes appear)* |
| **LEVEL JUMP** | `OFF`, `01 CITY 1` .. `24 FINAL` | Replaces the next loaded campaign map with the selected level (via New Game / Continue / Restart) |
| **SAVE CONFIG** | `APPLY` | Saves current settings to `sdmc:/switch/timeshift/timeshift.toml` |

---

## How to Contribute (Pull Requests)

If you manage to fix any of the remaining issues or improve performance:
1. **Fork** this repository to your own GitHub account.
2. Commit your fixes in your fork.
3. Submit a **Pull Request** to this repository.
4. Working pull requests will be reviewed and merged promptly.

---

## Credits & Acknowledgments

- **[@Oleksa126](https://github.com/Oleksa126) (Oleksandr Bazavliak):**  
  Special thanks and deep gratitude for his fork ([Oleksa126/timeshift-nx](https://github.com/Oleksa126/timeshift-nx)), which contributed:
  - Dynamic heap initialization via `__libnx_initheap` (~2.4 GB process RAM) preventing out-of-memory crashes.
  - Backing memory granule pooling and decommit rounding in `guest_memory_switch.cpp`.
  - Fix for `EncodeUnorm24` depth overflow in `depth_resolve.hlsli`, which resolved the fullscreen depth-of-field blur issue.
  - Fix for storm and weather lightning occlusion through indoor walls in `pipeline.cpp`.
  - Texture header protection for GPU-written surfaces during dynamic streaming.
  - GPU submit telemetry and draw history tracking.
- **ReX / xenia team:** For foundational Xbox 360 static recompilation and emulation research.
- **devkitPro & switchbrew team:** For `libnx`, `deko3d`, and the homebrew toolchain.

---

## License & Disclaimer

TimeShift is a registered trademark of Saber Interactive / Sierra / Activision. This repository is an unofficial, non-commercial reverse-engineering and static recompilation research project.

The source code in this repository is distributed for educational and research purposes under the terms of the BSD 3-Clause License and applicable upstream licenses. Original game assets (models, textures, audio, game scripts) are copyrighted by their respective owners and are NOT included in this repository.
