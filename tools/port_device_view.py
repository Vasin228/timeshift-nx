"""Apply the scoped TimeShift read-view port to the pinned local ReNut tree.

This does not register any title hook or enable a renderer. Original guest
setters own the state; N&B's native setters must not be used for TimeShift.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU = ROOT / 'upstream/sa2-nx/app/src/renut_engine/gpu'

def replace_once(text, old, new):
    if new in text:
        return text
    if text.count(old) != 1:
        raise ValueError(f'Unexpected pinned source: {old[:90]!r}')
    return text.replace(old, new)

def edit(name, replacements):
    path = GPU / name
    original = text = path.read_text()
    for old, new in replacements:
        text = replace_once(text, old, new)
    if text != original:
        path.write_text(text, newline='\n')

device_constants = {
    'kDeviceOffsetVertexDeclaration': 'vertex_declaration',
    'kDeviceOffsetIndexBuffer': 'index_buffer',
    'kDeviceOffsetRenderTargets': 'render_targets',
    'kDeviceOffsetDepthStencil': 'depth_stencil',
    'kDeviceOffsetStreamSources': 'stream_sources',
    'kDeviceOffsetStreamStrides': 'stream_strides',
    'kDeviceOffsetTextures': 'textures',
    'kDeviceOffsetPixelShader': 'pixel_shader',
    'kDeviceOffsetVertexShader': 'vertex_shader',
    'kDeviceOffsetPredication': 'predication',
    'kDeviceOffsetCommandBuffer': 'command_buffer',
    'kDeviceSnapshotSize': 'snapshot_bytes',
    'kDeviceOffsetVerticesRingEnd': 'inline_ring_end',
    'kDeviceOffsetSwapCount': 'swap_count',
    'kDeviceOffsetFenceTag': 'fence_tag',
    'kDeviceOffsetSwapFenceTag': 'swap_fence_tag',
    'kDeviceOffsetPresentFenceTag': 'present_fence_tag',
    'kDeviceOffsetSwapFlags': 'swap_flags',
}
import re
path = GPU / 'd3d/d3d.h'
text = path.read_text()
if '// TimeShift: selected read-view fields only.' not in text:
    text = replace_once(text, '#include <rex/types.h>', '''#include <rex/types.h>
#if defined(TIMESHIFT_RECOMP)
#include "timeshift_device_layout.h"
#endif''')
    for name, field in device_constants.items():
        match = re.search(r'^constexpr u32 ' + name + r' = [^\n]+', text, re.M)
        if not match:
            raise ValueError(name)
        old = match[0]
        new = ('#if defined(TIMESHIFT_RECOMP)\nconstexpr u32 ' + name +
               ' = timeshift::d3d6274::layout::' + field + ';\n#else\n' + old + '\n#endif')
        text = replace_once(text, old, new)
    text = replace_once(text, 'constexpr u32 kDeviceAllocSize = 0x6080;', '''// TimeShift: selected read-view fields only.
#if defined(TIMESHIFT_RECOMP)
constexpr u32 kDeviceAllocSize = timeshift::d3d6274::layout::allocation_bytes;
#else
constexpr u32 kDeviceAllocSize = 0x6080;
#endif''')
    old = '  u8 m_MiscPacket[152];'
    start = text.index(old)
    end = text.index('\n};', start)
    text = text[:start] + '''#if defined(TIMESHIFT_RECOMP)
  u8 unmodeled_tail[kDeviceAllocSize - 0x29B8];
#else
''' + text[start:end] + '\n#endif' + text[end:]
    start = text.index('static_assert(offsetof(Device, m_MiscPacket)')
    end = text.index('\n', text.index('static_assert(offsetof(Device, m_ZFilter)', start))
    text = text[:start] + '#if !defined(TIMESHIFT_RECOMP)\n' + text[start:end] + '\n#endif' + text[end:]
    # These are not consumed by the read view; do not claim their SA2 semantics
    # apply to this title. Device creation uses the original 6274 constructor.
    start = text.index('constexpr u32 kDeviceOffsetCreationThreadId')
    end = text.index('\n', text.index('constexpr u32 kDeviceOffsetBehaviorFlags', start))
    text = text[:start] + '#if !defined(TIMESHIFT_RECOMP)\n' + text[start:end] + '\n#endif' + text[end:]
    marker = 'constexpr u32 kMaxStreams = 16;'
    text = replace_once(text, marker, '''#if defined(TIMESHIFT_RECOMP)
constexpr u32 kDeviceOffsetViewport = timeshift::d3d6274::layout::viewport;
#else
constexpr u32 kDeviceOffsetViewport = 0x3168;
#endif

''' + marker)
    path.write_text(text, newline='\n')

edit('render/draw.cpp', [
    ('!ChainChanged(chain, 12920, 13023)',
     '!ChainChanged(chain, d3d::kDeviceOffsetTextures, d3d::kDeviceOffsetTextures + d3d::kTextureFetchSlots * 4 - 1)'),
    ('const float vx = device.F32(0x3168), vy = device.F32(0x316C);',
     'const float vx = device.F32(d3d::kDeviceOffsetViewport), vy = device.F32(d3d::kDeviceOffsetViewport + 4);'),
    ('const float vw = device.F32(0x3170), vh = device.F32(0x3174);',
     'const float vw = device.F32(d3d::kDeviceOffsetViewport + 8), vh = device.F32(d3d::kDeviceOffsetViewport + 12);'),
    ('        rex::memory::MarkStoreCardsAt(StoreCards(), 0xA0000000u + (p.base & 0x1FFFFFFFu), p.size);',
     '''#if !defined(TIMESHIFT_RECOMP)
        rex::memory::MarkStoreCardsAt(StoreCards(), 0xA0000000u + (p.base & 0x1FFFFFFFu), p.size);
#endif  // TimeShift's generated stores do not implement this SDK extension.'''),
])
edit('sa2_setter_marks.h', [
    ('constexpr uint32_t kMarkHeadEnd = 0x780, kMarkTailBegin = 0x2780, kMarkSnapshotEnd = 13088;',
     '''constexpr uint32_t kMarkHeadEnd = 0x780, kMarkTailBegin = 0x2780;
#if defined(TIMESHIFT_RECOMP)
constexpr uint32_t kMarkSnapshotEnd = 12704;
#else
constexpr uint32_t kMarkSnapshotEnd = 13088;
#endif'''),
])
edit('backend/queued.cpp', [
    ('  k.ready = false;\n  g_keys = k;', '''#if defined(TIMESHIFT_RECOMP)
  // The SA2 setter census and dirty-tag map are not TimeShift evidence.
  // Keep complete snapshots until the 6274 delta/mark path is proved.
  k.deltas = k.constants = k.deferred = k.narrow = false;
  k.marks = k.copy = k.precise = k.verify = false;
#endif
  k.ready = false;
  g_keys = k;'''),
    ('constexpr u32 kDeltaPrivateUnreadBegin = 11008, kDeltaPrivateUnreadEnd = 12224;',
     '''#if defined(TIMESHIFT_RECOMP)
constexpr u32 kDeltaPrivateUnreadBegin = 10880, kDeltaPrivateUnreadEnd = 11776;
#else
constexpr u32 kDeltaPrivateUnreadBegin = 11008, kDeltaPrivateUnreadEnd = 12224;
#endif'''),
    ('static_assert(kDeltaReadChunks.count == 40);', '''#if defined(TIMESHIFT_RECOMP)
static_assert(kDeltaReadChunks.count == 39);
#else
static_assert(kDeltaReadChunks.count == 40);
#endif'''),
    ('static_assert(kMarksAlways.w[0] == uint64_t(1) << 63 && kMarksAlways.w[1] == 0);',
     '''#if defined(TIMESHIFT_RECOMP)
static_assert(kMarksAlways.w[0] == uint64_t(1) << 56 && kMarksAlways.w[1] == 0);
#else
static_assert(kMarksAlways.w[0] == uint64_t(1) << 63 && kMarksAlways.w[1] == 0);
#endif'''),
])
edit('backend/render_thread.cpp', [
    ('#include <rex/input/read_hook.h>', '''#if !defined(TIMESHIFT_RECOMP)
#include <rex/input/read_hook.h>
#endif'''),
    ('void InstallLateInput() {\n  rex::latency::SetFrameReadDelayFunction(&LateInputDelay);',
     '''void InstallLateInput() {
#if !defined(TIMESHIFT_RECOMP)
  rex::latency::SetFrameReadDelayFunction(&LateInputDelay);'''),
    ('    rex::latency::NoteInputRead();\n  });\n}', '''    rex::latency::NoteInputRead();
  });
#endif  // Keep the existing TimeShift input path; this SDK has no SA2 read hook.
}'''),
])
edit('banjo_renut_tuning.h', [
    ('inline bool StoreCardsOn() { return Latch().store_cards; }', '''inline bool StoreCardsOn() {
#if defined(TIMESHIFT_RECOMP)
    return false;  // No store-card instrumentation in the frozen guest code/SDK.
#else
    return Latch().store_cards;
#endif
  }'''),
    ('inline bool StoreCardsVerifyOn() { return Latch().store_cards_verify; }', '''inline bool StoreCardsVerifyOn() {
#if defined(TIMESHIFT_RECOMP)
    return false;
#else
    return Latch().store_cards_verify;
#endif
  }'''),
])
edit('render/surfaces.cpp', [
    ('#include "me/sa2_render_mode.h"', '''#if !defined(TIMESHIFT_RECOMP)
#include "me/sa2_render_mode.h"
#endif'''),
    ('  const std::string resolution = REXCVAR_GET(renut_resolution);', '''#if defined(TIMESHIFT_RECOMP)
  // Establish correctness at the title's native scale before adding upscaling.
  RenderScale scale{};
  constexpr bool follow_mode = false, docked = false;
#else
  const std::string resolution = REXCVAR_GET(renut_resolution);'''),
    ('    scale = mode;\n  }\n  // banjo-nx (2026-10-18):', '''    scale = mode;
  }
#endif
  // banjo-nx (2026-10-18):'''),
])
edit('render/graphics_options.cpp', [
    ('RenderScale SurfaceRenderScale(const d3d::SurfaceDesc &desc, RenderScale resolution) {',
     '''RenderScale SurfaceRenderScale(const d3d::SurfaceDesc &desc, RenderScale resolution) {
#if defined(TIMESHIFT_RECOMP)
  return resolution;  // N&B's surface-size/EDRAM classifiers are not title evidence.
#else'''),
    ('  return resolution;\n}\n\nplume::RenderFormat SurfaceColorFormat',
     '  return resolution;\n#endif\n}\n\nplume::RenderFormat SurfaceColorFormat'),
    ('  if (g_latched.high_precision_post && IsLowPrecisionPostTarget(desc))',
     '''#if !defined(TIMESHIFT_RECOMP)
  if (g_latched.high_precision_post && IsLowPrecisionPostTarget(desc))'''),
    ('    return plume::RenderFormat::R16G16B16A16_UNORM;\n  return ColorTargetFormat',
     '    return plume::RenderFormat::R16G16B16A16_UNORM;\n#endif\n  return ColorTargetFormat'),
])
plume = ROOT / 'upstream/sa2-nx/app/thirdparty/plume/CMakeLists.txt'
original = plume.read_text()
text = replace_once(original, 'if(NOT EXISTS "${PLUME_SWITCH_NVK_LIBRARY}")',
                    'if(NOT PLUME_SWITCH_NO_NVK AND NOT EXISTS "${PLUME_SWITCH_NVK_LIBRARY}")')
if text != original:
    plume.write_text(text, newline='\n')
print('TimeShift read view applied; no hooks installed; title-specific deltas disabled')
