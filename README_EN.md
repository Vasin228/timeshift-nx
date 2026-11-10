[English](README_EN.md) | [Русский](README.md)

# timeshift-nx

**TimeShift** (Xbox 360 version) native port for **Nintendo Switch** using static recompilation and the native `deko3d` graphics API.

Releasing the project as-is.

I am not a programmer — I made this entire port using AI / neural networks. We managed to bring the game to a working state on physical hardware: it builds into a standalone `.nro` file, runs without an emulator, cutscenes play, menus work, levels load, controls, physics, and time manipulation mechanics function properly. However, the game currently runs at around 10–15 FPS on hardware (needs optimization).

Towards the end, we hit a wall with low-level renderer and audio bugs. I do not have access to frontier AI models, and trying to tackle complex low-level engine bugs with Gemini Flash is painful. I have simply run out of time and energy to keep working on this alone.

If you understand low-level graphics (deko3d, Nvidia Maxwell GM20B, Direct3D 9 / Xbox 360 Xenos) or Switch audio and want to finish the project — feel free to take the source code and work on it. Send your fixes via Pull Requests: I don't write code myself, but I will gladly review and merge working PRs so the game keeps improving.

---

## What Works Right Now

- **Static Recompilation:** Original Xbox 360 guest code compiled directly to 64-bit ARM (AArch64) for Horizon OS.
- **Native Renderer:** Runs via Nintendo Switch's native `deko3d` API (direct hardware access to Tegra X1 GPU).
- **Intro Cutscenes:** Bink Video playback works without audio/video glitches.
- **UI & Menus:** Main menu, options, profile save/load, pause screen are all working.
- **In-Game Gameplay:** Levels load and play (tested on `city_part01`) without crashes.
- **Controls & Physics:** Full support for Joy-Cons and Pro Controller, jumping, shooting, ragdoll, Havok physics, enemy AI.
- **Suit Abilities:** Slow time, time stop, and time rewind affect the world and physics correctly.
- **Performance:** Currently runs at ~10–15 FPS on retail Switch hardware (optimization needed).

---

## Known Bugs (What Needs Fixing)

If anyone decides to pick this up, here are the 4 main remaining issues:

### 1. Fullscreen Blur (Depth of Field Clamping)
* **Symptom:** The entire screen is covered by an intense blur effect (like a maximum radius Depth of Field), making everything appear out of focus — including weapons in hand and objects right in front of the camera.
* **Technical details:** The post-processing DOF shader reads the resolved EDRAM depth buffer (`dest->words` in `draw.cpp` / `renut_tuning_shims.cpp`). If depth values are read improperly or return 1.0 (far plane) due to tiling/format mismatches (`D24S8` vs `D32FS8`), the shader assumes 100% of the screen is at infinity and applies maximum blur everywhere.
* **Relevant files:** `adapter/renut_tuning_shims.cpp`, `upstream/sa2-nx/app/src/renut_engine/gpu/render/draw.cpp` (`ResolveDepth`), `upstream/sa2-nx/app/src/renut_engine/gpu/render/surfaces.cpp`.

### 2. Low-Resolution Textures Up-Close (Mipmap Clamping / LOD)
* **Symptom:** Walls, crates, wood, and character textures up-close remain blurry (low-res mip level), even though high-resolution textures exist in the game files.
* **Technical details:** Saber3D streams textures packed with Xbox 360 XGraphics tiling. Many texture headers have `max_mip_level == 0` or `fetch.MipPage() == 0`, which causes the renderer logic in `textures.cpp` / `texture_layout.cpp` to clamp available mip levels to 1, or skip uploading higher mip levels from `BasePage`.
* **Relevant files:** `upstream/sa2-nx/app/src/renut_engine/gpu/render/textures.cpp` (`BindTextureLocked`, `FillTextures`), `upstream/sa2-nx/app/src/renut_engine/gpu/render/texture_layout.cpp` (`GetTextureLevelLayout`), `adapter/tiling_hooks.cpp`.

### 3. Weather Lightning Bleeding Through Indoor Walls
* **Symptom:** During rain and thunderstorms on `city_part01`, lightning flashes and atmospheric volume light illuminate enclosed indoor rooms through roofs and solid walls.
* **Technical details:** Atmospheric weather passes use `zfunc == 7`. In `pipeline.cpp`, depth testing was being bypassed (`desc.depthEnabled = false`), or the indoor depth buffer is lost during EDRAM format transitions.
* **Relevant files:** `upstream/sa2-nx/app/src/renut_engine/gpu/render/pipeline.cpp`, `adapter/draw_hooks.cpp`.

### 4. Audio Stuttering During Combat and Explosions
* **Symptom:** Ambient music, footsteps, and quiet scenes sound clean. However, during heavy combat with grenade explosions and rapid gunfire, sound starts stuttering, buzzing, or causing frame hitches.
* **Technical details:** XMA2 decoder (`private/sdk/src/audio/xma_decoder.cpp`). When 8–16 sound channels trigger simultaneously, the engine attempts to decode them synchronously inside the `AudioWorker` thread. This takes 20–48 ms (far exceeding the 5.33 ms frame deadline), draining Horizon's circular `audout` buffer queue. Needs asynchronous offloading to a worker thread on an idle core (Core 0 or Core 3).
* **Relevant files:** `private/sdk/src/audio/xma_decoder.cpp`, `private/sdk/src/audio/audio_system.cpp`.

---

## Directory Structure

```
timeshift-nx/
├── adapter/              # C++ adapter layer bridging game calls to deko3d
│   ├── timeshift_app.cpp          # Application lifecycle and initialization
│   ├── renut_tuning_shims.cpp     # Depth, ZCull, EDRAM format configuration
│   ├── draw_hooks.cpp             # Render pass interception
│   └── tiling_hooks.cpp           # XGraphics texture tiling hooks
├── upstream/sa2-nx/      # renut_engine runtime (deko3d backend, shader translator)
├── private/              # SDK (audio, kernel, memory) and recompiled C++ sources
│   ├── app/generated/             # timeshift_recomp.*.cpp
│   └── sdk/                       # rexglue audio, kernel, memory
├── tools/                # Build and deployment scripts
│   ├── build_switch_app.py        # Links timeshift.nro
│   ├── prepare_switch_cache.py    # Prepares shaders and assets
│   └── deploy_candidate.py        # Deploys to Switch via FTP
├── MODLOG.md             # Development chronicle (Parts 1–241)
├── deko3d_shaders_f0.bin # Precompiled deko3d shader cache (4,283 shaders)
└── timeshift.toml        # Game configuration file for Switch
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
4. I will review the description and click **Merge** so your improvements get merged into the main project.

---

## License & Disclaimer

TimeShift is a registered trademark of Saber Interactive / Sierra / Activision. This repository is an unofficial non-commercial reverse-engineering research project.
