# Third-Party Notices & Licenses

This repository (`timeshift-nx`) combines original Switch adapter code, a modified `renut_engine` / `deko3d` runtime, the ReXGlue Xbox 360 recompilation SDK, and third-party open-source libraries.

- **Repository & Adapter / Engine Code (`adapter/`, `tools/`, `upstream/sa2-nx/`)**: Licensed under the **GNU General Public License v3.0** ([LICENSE](LICENSE)).
- **ReXGlue SDK & Horizon OS Layer (`private/sdk/`)**: Licensed under the **BSD 3-Clause License** ([private/sdk/LICENSE](private/sdk/LICENSE)).
- **Copyrighted Game Material Policy**: Original Xbox 360 game assets, `default.xex`, statically recompiled game C++ output (`private/app/generated/`), and precompiled game shader caches (`deko3d_shaders_f0.bin`, `deko3d_glsl.bin`) are **not** included in this repository and must be generated locally from the user's own legal copy of TimeShift.

## Upstream Projects & Fork Attributions

| Project | Role in `timeshift-nx` | License | Link |
|---|---|---|---|
| **ReXGlue SDK** (Tom Clay) & **Xenia** (Ben Vanik & contributors) | Xbox 360 kernel/runtime, PPC codegen, VFS, XMA audio | BSD-3-Clause | [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk) · [xenia.jp](https://xenia.jp) |
| **sa2-nx** & **banjo-nx** (ChanseyIsTheBest) | `renut_engine` renderer, `plume` `deko3d` backend, shader pipeline | GPL-3.0 | [ChanseyIsTheBest/sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) |
| **Oleksa126/timeshift-nx** (Oleksandr Bazavliak) | Dynamic heap init, granule pooling, DoF `EncodeUnorm24` & storm occlusion fixes | GPL-3.0 | [Oleksa126/timeshift-nx](https://github.com/Oleksa126/timeshift-nx) |
| **reNut** (masterspike52) | Direct3D-level GPU translation runtime (`renut_engine`) | Author permission (see `upstream/sa2-nx/THIRD_PARTY_NOTICES.md`) | [masterspike52/reNut](https://github.com/masterspike52/reNut) |
| **nfsmw-nx** (StevensND) & **masseffect-nx** (NebadasSwifty) | Horizon OS SDK layer, tooling, and recompilation port foundations | GPL-3.0 / BSD-3-Clause (SDK) | [StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx) · [nebadasSwifty/masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) |
| **NFSMW-Recompiled** (madelrandel-blip) | Application hook structure and build scripts | GPL-3.0 | [madelrandel-blip/NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) |

## Third-Party Libraries & Tools

| Library / Tool | Purpose | License | Link |
|---|---|---|---|
| **plume** (renderbag & contributors) | Graphics API abstraction (`deko3d` / Vulkan / D3D12) | MIT | [renderbag/plume](https://github.com/renderbag/plume) |
| **XenosRecomp** (hedge-dev, reblue fork) | Xbox 360 Xenos shader microcode translator | MIT | [hedge-dev/XenosRecomp](https://github.com/hedge-dev/XenosRecomp) |
| **uam** (devkitPro / fincs, patched) | Tegra X1 `DKSH` shader compiler (`deko3d`) | zlib / MIT (Mesa) / MPL-2.0 | [devkitPro/uam](https://github.com/devkitPro/uam) |
| **SPIRV-Cross** (Khronos Group) | SPIR-V reflection & shader cross-compilation | Apache-2.0 / MIT | [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) |
| **libnx** & **deko3d** (devkitPro / switchbrew) | Nintendo Switch Horizon OS userland & native GPU API | ISC / zlib | [switchbrew/libnx](https://github.com/switchbrew/libnx) · [devkitPro/deko3d](https://github.com/devkitPro/deko3d) |
| **FFmpeg** (`libavcodec`, `libavutil`, `libswresample`) | XMA2 / WMA audio decoding & resampling | LGPL-2.1-or-later | [ffmpeg.org](https://ffmpeg.org) |
| **dav1d** (VideoLAN) | AV1 video decoding | BSD-2-Clause | [code.videolan.org/videolan/dav1d](https://code.videolan.org/videolan/dav1d) |
| **libmspack** (Stuart Caie) | LZX decompression | LGPL-2.1 | [cabextract.org.uk/libmspack](https://www.cabextract.org.uk/libmspack/) |
| **o1heap** (Pavel Kirienko) | Constant-time (`O(1)`) guest heap allocator | MIT | [pavel-kirienko/o1heap](https://github.com/pavel-kirienko/o1heap) |
| **Dear ImGui** (Omar Cornut) | In-game `Hold +` overlay UI | MIT | [ocornut/imgui](https://github.com/ocornut/imgui) |
| **FidelityFX FSR1** (AMD) | Spatial upscaling & RCAS sharpening (`present_ps.hlsl`) | MIT | [GPUOpen-Effects/FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) |
| **Vulkan-Headers**, **volk**, **VulkanMemoryAllocator** | Graphics headers & memory management | Apache-2.0 / MIT | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) · [GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| **fmt**, **spdlog**, **SIMDe**, **SMOL-V** | Formatting, logging, SIMD emulation, SPIR-V compression | MIT | [fmtlib/fmt](https://github.com/fmtlib/fmt) · [gabime/spdlog](https://github.com/gabime/spdlog) · [simd-everywhere/simde](https://github.com/simd-everywhere/simde) |
| **xxHash** & **Zstandard (`zstd`)** | Fast hashing & shader archive compression | BSD-2-Clause / BSD-3-Clause | [Cyan4973/xxHash](https://github.com/Cyan4973/xxHash) · [facebook/zstd](https://github.com/facebook/zstd) |

For full component-level details inherited from `sa2-nx`, see [upstream/sa2-nx/THIRD_PARTY_NOTICES.md](upstream/sa2-nx/THIRD_PARTY_NOTICES.md).
