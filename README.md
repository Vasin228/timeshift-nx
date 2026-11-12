[English](README.md) | [Русский](README_RU.md)

# timeshift-nx

**TimeShift** (Xbox 360 version) native port for **Nintendo Switch** using static recompilation and the native `deko3d` graphics API.

Releasing the project as-is.

I am not a professional programmer - I built this port with the help of AI tools. The project has reached a working, playable state on physical Nintendo Switch hardware: it compiles into a standalone `.nro` file, runs natively without emulators, plays cutscenes, operates menus, saves progress and checkpoints, loads campaign levels, and supports controls, physics, and time manipulation mechanics. However, performance currently hovers around 10-15 FPS on retail hardware (optimization needed).

If you have experience with low-level Switch development (deko3d, Nvidia Maxwell GM20B, Direct3D 9 / Xbox 360 Xenos) or Switch audio and would like to help improve the port or optimize performance, you are welcome to contribute! Send your improvements via Pull Requests: working PRs will be gladly reviewed and merged so the port continues to evolve.

---

## What Works Right Now

- **Static Recompilation:** Original Xbox 360 guest code compiled directly to 64-bit ARM (AArch64) for Horizon OS.
- **Native Renderer:** Runs via Nintendo Switch native `deko3d` API (direct hardware access to Tegra X1 GPU).
- **Save System & Checkpoints:** Fully implemented and working! Profile saves, new game autosaves, and in-game checkpoints are created and loaded properly on Horizon OS SD card storage (`sdmc:/switch/timeshift/user/`).
- **Intro Cutscenes:** Bink Video playback works without audio/video glitches.
- **UI & Menus:** Main menu, options, profile save/load, pause screen are all working.
- **In-Game Gameplay:** Levels load and play (tested on `city_part01`) without crashes.
- **Controls & Input:** Full support for Joy-Cons and Pro Controller (simultaneous dual-input for handheld and docked play).
- **Suit Abilities:** Slow time, time stop, and time rewind affect the world and physics correctly.
- **Weather & Storm Occlusion:** Atmospheric lightning flashes and volume light properly occluded inside rooms (fixed by Oleksa126).
- **Auto-Sleep Inhibition:** Screen dimming and auto-sleep are disabled during gameplay.
- **Memory Management:** Up to 2.4 GB dynamic process heap with backing memory granule recycling.
- **Performance:** Currently runs at ~10-15 FPS on retail Switch hardware (optimization needed).

---

## Known Issues (What Needs Fixing)

If anyone decides to pick this up, here are the main remaining areas that need attention:

### 1. Performance / Framerate Optimization (~10-15 FPS)
* **Symptom:** The game runs at approximately 10-15 FPS during gameplay on physical Switch hardware.
* **Technical details:** Needs command list batching, reducing redundant pipeline switches, optimizing vertex/index buffer uploads, and profiling Tegra X1 GPU bottlenecks.

### 2. Audio Stuttering During Combat and Explosions
* **Symptom:** Ambient music, footsteps, and quiet scenes sound clean. However, during heavy combat with grenade explosions and rapid gunfire, sound starts stuttering or causing frame hitches.
* **Technical details:** XMA2 decoder (`private/sdk/src/audio/xma_decoder.cpp`). When multiple sound channels trigger simultaneously, the engine attempts to decode them synchronously inside the `AudioWorker` thread, exceeding the frame deadline. Needs asynchronous offloading to a worker thread on an idle core (Core 0 or Core 3).
* **Relevant files:** `private/sdk/src/audio/xma_decoder.cpp`, `private/sdk/src/audio/audio_system.cpp`.

### 3. Textures Stuck at Low LOD / Mipmap Levels
* **Symptom:** Certain world and object textures (such as environment walls and crates) appear blurry or low-resolution up-close, displaying lower LOD/mip levels.
* **Technical details:** Saber3D dynamic texture streaming packs mipmaps using Xbox 360 XGraphics tiling. Some textures need proper mip level chain resolution and contiguous mip page mapping so the engine streams full-resolution textures instead of low-detail LODs.
* **Relevant files:** `upstream/sa2-nx/app/src/renut_engine/gpu/render/textures.cpp`, `upstream/sa2-nx/app/src/renut_engine/gpu/render/texture_layout.cpp`.

---

## Directory Structure

```
timeshift-nx/
|-- adapter/              # C++ adapter layer bridging game calls to deko3d
|   |-- timeshift_app.cpp          # Application lifecycle and initialization
|   |-- renut_tuning_shims.cpp     # Depth, ZCull, EDRAM format configuration
|   |-- draw_hooks.cpp             # Render pass interception
|   `-- tiling_hooks.cpp           # XGraphics texture tiling hooks
|-- upstream/sa2-nx/      # renut_engine runtime (deko3d backend, shader translator)
|-- private/              # SDK (audio, kernel, memory) and recompiled C++ sources
|   |-- app/generated/             # Recompiled game code (timeshift_recomp.*.cpp)
|   `-- sdk/                       # rexglue (audio, kernel, memory)
|-- tools/                # Build and deployment scripts
|   |-- build_switch_app.py        # Links timeshift.nro
|   |-- prepare_switch_cache.py    # Prepares shaders and assets
|   `-- deploy_candidate.py        # Deploys to Switch via FTP
|-- MODLOG.md             # Development chronicle (Parts 1-245)
|-- deko3d_shaders_f0.bin # Precompiled deko3d shader cache (4,283 shaders)
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
   - `deko3d_shaders_f0.bin`
   - `game/` folder with extracted TimeShift game data.
3. **Launch ONLY via Title Override:**  
   Hold **R** while launching any installed retail game on your Switch to open hbmenu with full memory access.  
   *Do NOT launch via the Album (Applet Mode), or the game will crash due to out-of-memory errors!*

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
