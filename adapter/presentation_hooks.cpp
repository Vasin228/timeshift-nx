#include "presentation_hooks.h"
#include "device_registry.h"
#include "draw_runtime_bridge.h"
#include "shader_runtime_bridge.h"
#include "timeshift_device_layout.h"
#include "render_drop_telemetry.h"
#include "perf_breakdown.h"

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/backend/memory_report.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/render/surfaces.h"
#include "renut_engine/gpu/render/pipeline.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/callbacks.h"

#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <o1heap.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(__SWITCH__)
#include <switch.h>
#endif

#if defined(__SWITCH__)
REXCVAR_DEFINE_BOOL(renut_audio_reverb, false, "Renut",
                    "Enable FMOD software SFX reverb DSP (off by default on Switch to save 10-15ms CPU in combat)");
#else
REXCVAR_DEFINE_BOOL(renut_audio_reverb, true, "Renut",
                    "Enable FMOD software SFX reverb DSP");
#endif

REX_EXTERN(__imp__sub_8264B1A8);
REX_EXTERN(__imp__sub_8264AD00);
REX_EXTERN(__imp__sub_8264DCF0);
REX_EXTERN(__imp__sub_82145740);
REX_EXTERN(__imp__sub_821455E8);
REX_EXTERN(__imp__sub_828172A8);
REX_EXTERN(__imp__sub_8281AE38);
REX_EXTERN(__imp__sub_8287F678);
REX_EXTERN(__imp__sub_828C05A8);
REX_EXTERN(__imp__sub_828C6470);
REX_EXTERN(__imp__sub_828C80D0);
REX_EXTERN(__imp__sub_828D6248);
REX_EXTERN(__imp__sub_828D6328);
REX_EXTERN(__imp__sub_828D6418);
REX_EXTERN(__imp__sub_828D65E0);
REX_EXTERN(__imp__sub_828D7818);
REX_EXTERN(__imp__sub_828DA770);
REX_EXTERN(__imp__sub_828DC538);
REX_EXTERN(__imp__sub_828DEE18);
REX_EXTERN(__imp__sub_828DFCC0);
REX_EXTERN(__imp__sub_82B08E80);

#if defined(__SWITCH__)
extern "C" {
void rex_watchdog_note_swap(uint64_t swaps);
void rex_watchdog_set_heap_owner(uint32_t role_plus_one);
void rex_watchdog_set_heap_op(uint32_t op);
void rex_watchdog_set_draw_stage(uint32_t stage);
void rex_watchdog_set_render_stage(uint32_t stage);
}
#endif

namespace {

thread_local bool g_thread_core_pinned = false;
thread_local uint64_t g_prev_thread_cpu_ticks = 0;

inline uint32_t CrtPhysOff(uint32_t ea) noexcept {
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))
  return (ea >= 0xE0000000u) ? 0x1000u : 0u;
#else
  (void)ea;
  return 0u;
#endif
}

inline uint32_t* GuestWordPtr(uint8_t* base, uint32_t ea) {
  return reinterpret_cast<uint32_t*>(base + ea + CrtPhysOff(ea));
}

#if defined(__SWITCH__)
uint32_t QueryAllowedCoreMask() {
  static const uint32_t allowed = [] {
    u64 val = 0;
    const Result rc = svcGetInfo(&val, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    const uint32_t mask = (R_SUCCEEDED(rc) && (val & 0xFu) != 0u) ? static_cast<uint32_t>(val & 0xFu) : 0x7u;
    timeshift::telemetry::g_allowed_core_mask.store(mask, std::memory_order_relaxed);
    return mask;
  }();
  return allowed;
}

Result PinCurrentThread(int32_t ideal_core, uint32_t core_mask, int32_t prio,
                        Result* out_prio_rc = nullptr, int32_t* out_actual_core = nullptr,
                        uint32_t* out_applied_mask = nullptr) {
  const uint32_t allowed = QueryAllowedCoreMask();
  uint32_t eff_mask = core_mask & allowed;
  if (eff_mask == 0u) {
    eff_mask = allowed;
  }
  int32_t eff_ideal = ideal_core;
  if (eff_ideal < 0 || eff_ideal >= 4 || (eff_mask & (1u << eff_ideal)) == 0u) {
    if (eff_mask & 0x4u) eff_ideal = 2;
    else if (eff_mask & 0x2u) eff_ideal = 1;
    else eff_ideal = 0;
  }
  const Result rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, eff_ideal, eff_mask);
  const Result rc_prio = svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
  if (out_prio_rc) *out_prio_rc = rc_prio;
  if (out_actual_core) *out_actual_core = svcGetCurrentProcessorNumber();
  if (out_applied_mask) *out_applied_mask = eff_mask;
  g_thread_core_pinned = true;
  return rc_mask;
}
#endif

// Native ARM64 O(1) guest heap (o1heap) replacing PowerPC dlmalloc (sub_82150CE0/1158/1D58/1A58).
namespace o1_guest_heap {

constexpr uint32_t kMagic = 0x54534D41u;
constexpr uint32_t kFreedMagic = 0xDEADBEEFu;
constexpr uint32_t kDedicatedIdx = 0xFFFFu;
constexpr uint32_t kArenaSize = 16u * 1024u * 1024u;
constexpr uint32_t kHugeThreshold = 2u * 1024u * 1024u;
constexpr uint32_t kMaxArenas = 48u;
constexpr uint32_t kMaxValidReq = 0x3F000000u;

struct alignas(16) AllocHdr {
  uint32_t magic;
  uint32_t size;
  uint32_t arena_idx;
  uint32_t base_va;
};
static_assert(sizeof(AllocHdr) == 16);

struct Arena {
  uint32_t va_start = 0;
  uint32_t va_end = 0;
  O1HeapInstance* inst = nullptr;
  uint32_t live_allocs = 0;
};

#if defined(__SWITCH__)
Mutex g_mutex = 0;
struct HeapGuard {
  HeapGuard() {
    mutexLock(&g_mutex);
    rex_watchdog_set_heap_owner(static_cast<uint32_t>(timeshift::telemetry::t_thread_role + 1));
  }
  ~HeapGuard() {
    rex_watchdog_set_heap_op(0u);
    rex_watchdog_set_heap_owner(0u);
    mutexUnlock(&g_mutex);
  }
};
inline void SetHeapOp(uint32_t op) { rex_watchdog_set_heap_op(op); }
#else
std::mutex g_mutex;
using HeapGuard = std::lock_guard<std::mutex>;
inline void SetHeapOp(uint32_t) {}
#endif

Arena g_arenas[kMaxArenas]{};
uint32_t g_last_arena = 0;
uint32_t g_huge_live = 0;
uint64_t g_alloc_cnt = 0;
uint64_t g_free_cnt = 0;
uint64_t g_realloc_cnt = 0;

inline rex::memory::BaseHeap* GetVirtualHeap64K() {
  auto* rt = rex::Runtime::instance();
  auto* mem = rt ? rt->memory() : nullptr;
  return mem ? mem->LookupHeapByType(false, 64u * 1024u) : nullptr;
}

uint32_t MallocLocked(uint8_t* base, uint32_t req_size) {
  SetHeapOp(1u);
  ++g_alloc_cnt;
  if (req_size > kMaxValidReq) return 0u;
  const uint32_t req = (req_size == 0u) ? 16u : req_size;
  auto* vheap = GetVirtualHeap64K();
  if (!vheap) return 0u;

  if (req >= kHugeThreshold) {
    const uint32_t total = (req + 16u + 65535u) & ~65535u;
    uint32_t va = 0u;
    SetHeapOp(4u);
    const bool ok = vheap->Alloc(total, 65536u,
                                 rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                                 rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &va);
    SetHeapOp(1u);
    if (!ok) {
      return 0u;
    }
    auto* hdr = reinterpret_cast<AllocHdr*>(base + va);
    hdr->magic = kMagic;
    hdr->size = req;
    hdr->arena_idx = kDedicatedIdx;
    hdr->base_va = va;
    ++g_huge_live;
    return va + 16u;
  }

  for (uint32_t step = 0; step < kMaxArenas; ++step) {
    const uint32_t idx = (g_last_arena + step) % kMaxArenas;
    Arena& a = g_arenas[idx];
    if (!a.inst) continue;
    if (void* raw = o1heapAllocate(a.inst, size_t(req) + 16u)) {
      auto* hdr = static_cast<AllocHdr*>(raw);
      hdr->magic = kMagic;
      hdr->size = req;
      hdr->arena_idx = idx;
      hdr->base_va = 0u;
      ++a.live_allocs;
      g_last_arena = idx;
      return static_cast<uint32_t>(static_cast<uint8_t*>(raw) - base) + 16u;
    }
  }

  for (uint32_t idx = 0; idx < kMaxArenas; ++idx) {
    Arena& a = g_arenas[idx];
    if (a.inst) continue;
    uint32_t va = 0u;
    SetHeapOp(4u);
    const bool ok = vheap->Alloc(kArenaSize, 65536u,
                                 rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                                 rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &va);
    SetHeapOp(1u);
    if (!ok) {
      return 0u;
    }
    O1HeapInstance* inst = o1heapInit(base + va, kArenaSize);
    if (!inst) {
      SetHeapOp(4u);
      vheap->Release(va);
      SetHeapOp(1u);
      return 0u;
    }
    a.va_start = va;
    a.va_end = va + kArenaSize;
    a.inst = inst;
    a.live_allocs = 0u;
    REXLOG_INFO("[heap-o1] arena #{} va=0x{:08X}..0x{:08X} ({} MiB)", idx, a.va_start, a.va_end, kArenaSize >> 20);
    if (void* raw = o1heapAllocate(inst, size_t(req) + 16u)) {
      auto* hdr = static_cast<AllocHdr*>(raw);
      hdr->magic = kMagic;
      hdr->size = req;
      hdr->arena_idx = idx;
      hdr->base_va = 0u;
      ++a.live_allocs;
      g_last_arena = idx;
      return static_cast<uint32_t>(static_cast<uint8_t*>(raw) - base) + 16u;
    }
    return 0u;
  }
  return 0u;
}

void FreeLocked(uint8_t* base, uint32_t ptr_va) {
  SetHeapOp(2u);
  ++g_free_cnt;
  if (ptr_va < 0x40000000u || ptr_va >= 0x7F000000u) return;
  auto* hdr = reinterpret_cast<AllocHdr*>(base + ptr_va - 16u);
  if (hdr->magic != kMagic) {
    static std::atomic<uint32_t> s_bad_frees{0};
    if (s_bad_frees.fetch_add(1, std::memory_order_relaxed) < 8u) {
      REXLOG_WARN("[heap-o1] ignoring free of non-o1heap ptr=0x{:08X} (magic=0x{:08X})", ptr_va, hdr->magic);
    }
    return;
  }
  const uint32_t idx = hdr->arena_idx;
  const uint32_t base_va = hdr->base_va;
  hdr->magic = kFreedMagic;
  auto* vheap = GetVirtualHeap64K();
  if (idx == kDedicatedIdx) {
    if (vheap) {
      SetHeapOp(4u);
      vheap->Release(base_va);
      SetHeapOp(2u);
    }
    if (g_huge_live > 0u) --g_huge_live;
    return;
  }
  if (idx < kMaxArenas && g_arenas[idx].inst) {
    o1heapFree(g_arenas[idx].inst, hdr);
    if (g_arenas[idx].live_allocs > 0u) --g_arenas[idx].live_allocs;
    if (idx >= 2u && g_arenas[idx].live_allocs == 0u) {
      uint32_t empty_spare = 0u;
      for (uint32_t j = 2u; j < kMaxArenas; ++j) {
        if (g_arenas[j].inst && g_arenas[j].live_allocs == 0u) ++empty_spare;
      }
      if (empty_spare > 2u) {
        if (vheap) {
          SetHeapOp(4u);
          vheap->Release(g_arenas[idx].va_start);
          SetHeapOp(2u);
        }
        g_arenas[idx] = Arena{};
        if (g_last_arena == idx) g_last_arena = 0u;
      }
    }
  }
}

uint32_t ReallocLocked(uint8_t* base, uint32_t old_va, uint32_t new_size) {
  SetHeapOp(3u);
  ++g_realloc_cnt;
  if (old_va == 0u) return MallocLocked(base, new_size);
  if (new_size > kMaxValidReq) return 0u;
  if (old_va < 0x40000000u || old_va >= 0x7F000000u) return 0u;
  const uint32_t req = (new_size == 0u) ? 16u : new_size;
  auto* hdr = reinterpret_cast<AllocHdr*>(base + old_va - 16u);
  if (hdr->magic != kMagic) return 0u;
  const uint32_t old_size = hdr->size;
  const uint32_t idx = hdr->arena_idx;
  if (req <= old_size && (old_size - req) < 4096u && idx != kDedicatedIdx) {
    hdr->size = req;
    return old_va;
  }
  if (idx < kMaxArenas && g_arenas[idx].inst && req < kHugeThreshold) {
    if (void* new_raw = o1heapReallocate(g_arenas[idx].inst, hdr, size_t(req) + 16u)) {
      auto* nh = static_cast<AllocHdr*>(new_raw);
      nh->magic = kMagic;
      nh->size = req;
      nh->arena_idx = idx;
      nh->base_va = 0u;
      return static_cast<uint32_t>(static_cast<uint8_t*>(new_raw) - base) + 16u;
    }
  }
  const uint32_t new_va = MallocLocked(base, req);
  if (new_va != 0u) {
    std::memcpy(base + new_va, base + old_va, std::min(old_size, req));
    FreeLocked(base, old_va);
  }
  SetHeapOp(3u);
  return new_va;
}

void LogWindowSummary() {
  uint32_t active_arenas = 0;
  uint64_t live = 0;
  uint64_t allocs = 0, frees = 0, reallocs = 0;
  uint32_t huge = 0;
  {
    HeapGuard lock;
    for (const Arena& a : g_arenas) {
      if (a.inst) {
        ++active_arenas;
        live += a.live_allocs;
      }
    }
    huge = g_huge_live;
    allocs = g_alloc_cnt;
    frees = g_free_cnt;
    reallocs = g_realloc_cnt;
    g_alloc_cnt = 0;
    g_free_cnt = 0;
    g_realloc_cnt = 0;
  }
  REXLOG_INFO("[heap-o1] 300f: arenas={} ({} MiB) live={} alloc/f={:.1f} free/f={:.1f} realloc/f={:.1f} huge={}",
              active_arenas, active_arenas * (kArenaSize >> 20), live,
              double(allocs) / 300.0, double(frees) / 300.0, double(reallocs) / 300.0, huge);
}

}  // namespace o1_guest_heap

}  // namespace

namespace timeshift::renderer {
namespace {

using namespace d3d6274::layout;

inline constexpr uint32_t kPresentRetiredCount = 16552;
inline constexpr uint32_t kMaxFramesInFlight = 2;

uint32_t Word(std::span<const uint8_t> p, uint32_t at) {
  if (uint64_t(at) + 4 > p.size()) throw std::out_of_range("presentation guest word");
  return uint32_t(p[at]) << 24 | uint32_t(p[at + 1]) << 16 | uint32_t(p[at + 2]) << 8 | p[at + 3];
}

void Put(std::span<uint8_t> p, uint32_t at, uint32_t value) {
  if (uint64_t(at) + 4 > p.size()) throw std::out_of_range("presentation guest word");
  for (unsigned i = 0; i < 4; ++i) {
    p[at + i] = uint8_t(value >> (24 - 8 * i));
  }
}

bool Owned(uint32_t device) {
  return PresentationHooksEnabled() &&
         DeviceLifecycleHooksEnabled() &&
         DeviceRegistryRuntime().Contains(device);
}

std::atomic<uint64_t> g_swap_calls{0};
std::atomic<uint64_t> g_sync_calls{0};

}  // namespace

bool PresentationHooksEnabled() {
  return ResourceHooksEnabled();
}

void NativeSwap(PPCContext& ctx, uint8_t* base) {
  const uint32_t device_va = ctx.r3.u32;
  const uint32_t front_buffer_va = ctx.r4.u32;
  const uint32_t parameters_va = ctx.r5.u32;
  (void)parameters_va;
  const bool is_owned = Owned(device_va);
  static std::atomic<uint64_t> s_swap_entry_count{0};
  const uint64_t entry_num = ++s_swap_entry_count;
  if (entry_num == 1 || entry_num % 60 == 0) {
    REXLOG_INFO("[present] NativeSwap entry #{}: dev=0x{:08X}, fb=0x{:08X}, owned={}",
                entry_num, device_va, front_buffer_va, is_owned);
    std::fflush(stdout);
  }

  try {
    if (!is_owned) {
      __imp__sub_8264B1A8(ctx, base);
      return;
    }

    const uint64_t t_swap_start = timeshift::telemetry::NowTicks();
#if defined(__SWITCH__)
    struct WatchdogSwapStageGuard {
      ~WatchdogSwapStageGuard() { rex_watchdog_set_draw_stage(0u); }
    } wd_swap_guard;
    rex_watchdog_set_draw_stage(1u);
    timeshift::telemetry::t_thread_role = 1;
    if (!g_thread_core_pinned) {
      Result rc_prio = 0;
      int32_t actual_core = -1;
      uint32_t applied_mask = 0;
      const Result rc_mask = PinCurrentThread(0, 0x3u, 0x24, &rc_prio, &actual_core, &applied_mask);
      REXLOG_INFO("[cpu-pin] swap thread pinned ideal=0 mask=0x{:X} (allowed=0x{:X}) cur_core={} rc=0x{:X} prio=0x24 rc=0x{:X}",
                  applied_mask, QueryAllowedCoreMask(), actual_core, uint32_t(rc_mask), uint32_t(rc_prio));
    }
    timeshift::telemetry::AccountThreadCpuTicks(1);
    timeshift::telemetry::g_game_core_mask.fetch_or(1u << svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
#endif

    auto& registry = DeviceRegistryRuntime();
    auto lease = registry.Acquire(device_va, false);
    if (!lease) {
      REXLOG_ERROR("[present] Swap failed: device 0x{:08X} lease unavailable", device_va);
      ctx.r3.u64 = 0x80004005u;  // E_FAIL
      return;
    }

    auto dev_span = registry.Memory().Map(device_va, allocation_bytes);
    if (dev_span.size() < allocation_bytes) {
      REXLOG_ERROR("[present] Swap failed: device 0x{:08X} memory map failed", device_va);
      ctx.r3.u64 = 0x80004005u;
      return;
    }

    // 1. Flush any pending vertex sources and callbacks
    lease->FlushSources(device_va);
    FlushPendingDraw(device_va);
    registry.Memory().RunCallbacks();
    renut::gpu::SnapshotDeltasNewFrame();

    // 2. Preserve CPU swap counters and fence tags in guest device state
    const uint32_t current_fence = Word(dev_span, fence_tag);
    Put(dev_span, present_fence_tag, current_fence);
    Put(dev_span, swap_fence_tag, current_fence);

    // Clear pending swap flag 0x10 in swap_flags (byte 10941)
    dev_span[swap_flags] &= ~0x10;

    // Increment CPU swap counter at offset 16544
    const uint32_t swap_count_val = Word(dev_span, swap_count) + 1;
    Put(dev_span, swap_count, swap_count_val);
#if defined(__SWITCH__)
    rex_watchdog_note_swap(swap_count_val);
#endif

    // 3. Process display gamma ramp at offset 14988 (768 big-endian 16-bit words = 1536 bytes)
    const uint8_t* gamma_ptr = dev_span.data() + gamma_ramp;
    bool gamma_nonzero = false;
    for (size_t i = 0; i < 1536; ++i) {
      if (gamma_ptr[i] != 0) {
        gamma_nonzero = true;
        break;
      }
    }
    if (gamma_nonzero) {
      renut::gpu::Video::SetGammaRamp(gamma_ptr);
    }

    // 4. Real GPU frame backpressure:
    // If too many frames are queued ahead of presentation, wait on fence progress.
    if (swap_count_val > Word(dev_span, kPresentRetiredCount) + kMaxFramesInFlight) {
#if defined(__SWITCH__)
      rex_watchdog_set_draw_stage(2u);
#endif
      timeshift::telemetry::FenceWaitScope wait_scope;
      while (swap_count_val > Word(dev_span, kPresentRetiredCount) + kMaxFramesInFlight) {
        if (!renut::gpu::render_thread::WaitForFenceProgress(std::chrono::milliseconds(2))) {
          break;
        }
        registry.Memory().RunCallbacks();
      }
    }

    // 5. Queue presentation to render thread.
    // Frontbuffer Resolve was already queued by sub_8264C8E8 before Swap.
    // The render thread executes FIFO, so Resolve always completes before Present.
#if defined(__SWITCH__)
    rex_watchdog_set_draw_stage(3u);
#endif
    const uint64_t swap_ordinal = swap_count_val;
    renut::gpu::render_thread::QueueFrame([device_va, front_buffer_va, swap_ordinal] {
      using namespace renut::gpu;
      PresentImage image{};
      {
        std::lock_guard lock(State().mutex);
        if (front_buffer_va) {
          image = FrontBufferImageLocked(front_buffer_va);
          if (image.texture) {
            State().last_front_buffer = front_buffer_va;
          }
        }
        AdvanceTextureFrameLocked();
        AdvanceStreamCacheFrameLocked();
        LatchResolutionScaleLocked();
      }

#if defined(__SWITCH__)
      rex_watchdog_set_render_stage(2u);
#endif
      Video::NotePresentingSwap(swap_ordinal);
      Video::Present(image.texture ? &image : nullptr);

#if defined(__SWITCH__)
      rex_watchdog_set_render_stage(5u);
#endif
      {
        std::lock_guard lock(State().mutex);
        ::renut::gpu::SyncPipelineListLocked();
      }
#if defined(__SWITCH__)
      rex_watchdog_set_render_stage(6u);
#endif
      ::renut::gpu::MaybeReportMemory();

      // Advance presentation retirement counter (offset 16552) in guest device
      if (auto* dev = GuestPtr<uint8_t>(device_va)) {
        auto& retired = *reinterpret_cast<be_u32*>(dev + kPresentRetiredCount);
        retired = uint32_t(swap_ordinal);
      }
#if defined(__SWITCH__)
      rex_watchdog_set_render_stage(1u);
#endif
    });

    // 6. Run ready guest callbacks after queueing frame
    registry.Memory().RunCallbacks();

    // 7. Bounded startup / presentation progress logging
    const uint64_t call_idx = g_swap_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (call_idx % 300 == 0) {
      timeshift::telemetry::CheckAndReportDrops(call_idx);
    }
    if (call_idx == 1 || call_idx % 60 == 0) {
      const uint32_t retired = Word(dev_span, kPresentRetiredCount);
      REXLOG_INFO("[present] Progress: frame #{}, swap_count={}, retired={}, in_flight={}",
                  call_idx, swap_count_val, retired, swap_count_val - retired);
      std::fflush(stdout);
    }

    const uint64_t t_swap_end = timeshift::telemetry::NowTicks();
    if (t_swap_end > t_swap_start) {
      timeshift::telemetry::g_ticks_swap.fetch_add(t_swap_end - t_swap_start, std::memory_order_relaxed);
    }
    static std::atomic<uint64_t> s_prev_swap_end{0};
    const uint64_t prev_swap_end = s_prev_swap_end.exchange(t_swap_end, std::memory_order_relaxed);
    if (prev_swap_end != 0 && t_swap_end > prev_swap_end) {
      timeshift::telemetry::g_ticks_game_frame.fetch_add(t_swap_end - prev_swap_end, std::memory_order_relaxed);
    }
    if (entry_num % 300 == 0) {
      o1_guest_heap::LogWindowSummary();
    }

    ctx.r3.u64 = 0;  // S_OK
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[present] NativeSwap EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

void NativeSynchronizeToPresentationInterval(PPCContext& ctx, uint8_t* base) {
  const uint32_t device_va = ctx.r3.u32;
  const uint32_t immediate_flag = ctx.r4.u32;

  try {
    if (!Owned(device_va)) {
      __imp__sub_8264AD00(ctx, base);
      return;
    }

    auto& registry = DeviceRegistryRuntime();
    auto lease = registry.Acquire(device_va, false);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }

    auto dev_span = registry.Memory().Map(device_va, allocation_bytes);
    if (dev_span.size() >= allocation_bytes) {
      // Set pending swap flag 0x10 in swap_flags (byte 10941)
      dev_span[swap_flags] |= 0x10;
    }

    const uint64_t call_idx = g_sync_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (call_idx == 1) {
      REXLOG_INFO("[present] SynchronizeToPresentationInterval: active title pacing engaged (immediate={})",
                  immediate_flag);
      std::fflush(stdout);
    }

    ctx.r3.u64 = 0;  // S_OK
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[present] SynchronizeToPresentationInterval EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

}  // namespace timeshift::renderer

REX_EXTERN(sub_8264B1A8) {
  timeshift::renderer::NativeSwap(ctx, base);
}

REX_EXTERN(sub_8264AD00) {
  timeshift::renderer::NativeSynchronizeToPresentationInterval(ctx, base);
}

REX_EXTERN(sub_82145740) {
  const uint32_t guest_hw = ctx.r3.u32;
  const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_82145740(ctx, base);
#if defined(__SWITCH__)
  int32_t ideal_core = 2;
  uint32_t core_mask = 0x4u;
  int32_t prio = 0x2C;
  int role = 5;
  if (caller_lr == 0x8224A53Cu) {
    // Main game simulation, physics, and AI thread (TID 04): dedicated Core 1 primary, spillover Core 0.
    role = 0;
    ideal_core = 1;
    core_mask = 0x3u;
    prio = 0x24;
  } else if (caller_lr == 0x82264AC8u && guest_hw == 3u) {
    // Dedicated Saber3D 3D scene & draw submission thread (TID 09): Core 0 primary, spillover Core 1.
    role = 1;
    ideal_core = 0;
    core_mask = 0x3u;
    prio = 0x24;
  } else if (caller_lr == 0x82264AC8u) {
    // Parallel Saber3D workers: prefer Core 1 headroom, allow 0..2, below main/draw/render/audio.
    role = 2;
    ideal_core = 1;
    core_mask = 0x7u;
    prio = 0x27;
  } else {
    // Streaming and background decompression threads: allow Core 1+2 so heap/CS locks never starve on Core 2.
    role = 5;
    ideal_core = 1;
    core_mask = 0x6u;
    prio = 0x28;
  }
  timeshift::telemetry::t_thread_role = role;
  Result rc_prio = 0;
  int32_t actual_core = -1;
  uint32_t applied_mask = 0;
  const Result rc_mask = PinCurrentThread(ideal_core, core_mask, prio, &rc_prio, &actual_core, &applied_mask);
  timeshift::telemetry::g_game_core_mask.fetch_or(1u << svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
  static std::atomic<uint32_t> s_worker_pin_logs{0};
  if (s_worker_pin_logs.fetch_add(1, std::memory_order_relaxed) < 20u) {
    REXLOG_INFO("[cpu-pin] worker lr=0x{:08X} hw={} role={} pinned ideal={} applied_mask=0x{:X} (allowed=0x{:X}) cur_core={} rc=0x{:X} prio=0x{:X} rc=0x{:X}",
                caller_lr, guest_hw, role, ideal_core, applied_mask, QueryAllowedCoreMask(), actual_core,
                uint32_t(rc_mask), uint32_t(prio), uint32_t(rc_prio));
  }
#else
  (void)guest_hw;
  (void)caller_lr;
#endif
}

REX_EXTERN(sub_821455E8) {
  __imp__sub_821455E8(ctx, base);
#if defined(__SWITCH__)
  if (!g_thread_core_pinned) {
    timeshift::telemetry::t_thread_role = 5;
    Result rc_prio = 0;
    int32_t actual_core = -1;
    uint32_t applied_mask = 0;
    const Result rc_mask = PinCurrentThread(1, 0x6u, 0x28, &rc_prio, &actual_core, &applied_mask);
    static std::atomic<uint32_t> s_wait_pin_logs{0};
    if (s_wait_pin_logs.fetch_add(1, std::memory_order_relaxed) < 16u) {
      REXLOG_INFO("[cpu-pin] unpinned waiter pinned ideal=1 applied_mask=0x{:X} (allowed=0x{:X}) cur_core={} rc=0x{:X} prio=0x28 rc=0x{:X}",
                  applied_mask, QueryAllowedCoreMask(), actual_core, uint32_t(rc_mask), uint32_t(rc_prio));
    }
  }
  timeshift::telemetry::AccountThreadCpuTicks(timeshift::telemetry::t_thread_role);
  static thread_local uint32_t s_wait_probe = 0;
  const uint32_t probe = s_wait_probe++;
  if (probe == 0 || (probe & 15u) == 0) {
    timeshift::telemetry::g_game_core_mask.fetch_or(1u << svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
  }
#endif
}

namespace {
inline void LockFreeInterlockedAdd32(PPCContext& ctx, uint8_t* base, int32_t delta) {
#if defined(__SWITCH__)
  static thread_local uint32_t s_sim_probe = 0;
  if (timeshift::telemetry::t_thread_role == 0 && (++s_sim_probe & 1023u) == 0u) {
    timeshift::telemetry::AccountThreadCpuTicks(0);
  }
#endif
  uint32_t* ptr = GuestWordPtr(base, ctx.r3.u32);
  uint32_t cur_be = __atomic_load_n(ptr, __ATOMIC_RELAXED);
  uint64_t next_u64 = 0;
  uint32_t next_be = 0;
  do {
    const uint32_t cur_val = __builtin_bswap32(cur_be);
    next_u64 = static_cast<uint64_t>(static_cast<int64_t>(cur_val) + int64_t(delta));
    next_be = __builtin_bswap32(static_cast<uint32_t>(next_u64));
  } while (!__atomic_compare_exchange_n(ptr, &cur_be, next_be, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
  ctx.reserved.u32 = cur_be;
  ctx.r10.u64 = 0x8000u;
  ctx.r11.u64 = next_u64;
  ctx.cr0.lt = 0;
  ctx.cr0.gt = 0;
  ctx.cr0.eq = 1;
  ctx.cr0.so = ctx.xer.so;
}
}  // namespace

REX_EXTERN(sub_82144E28) {
  LockFreeInterlockedAdd32(ctx, base, 1);
}

REX_EXTERN(sub_82144E48) {
  LockFreeInterlockedAdd32(ctx, base, -1);
}

REX_EXTERN(sub_82145820) {
  uint32_t* ptr = GuestWordPtr(base, ctx.r3.u32);
  const uint32_t comparand = ctx.r5.u32;
  const uint32_t exchange_be = __builtin_bswap32(ctx.r4.u32);
  uint32_t expected_be = __builtin_bswap32(comparand);
  __atomic_compare_exchange_n(ptr, &expected_be, exchange_be, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  const uint32_t old_val = __builtin_bswap32(expected_be);
  ctx.reserved.u32 = expected_be;
  ctx.r10.u64 = 0x8000u;
  ctx.r11.u64 = uint64_t(old_val);
  ctx.r3.u64 = uint64_t(old_val);
  ctx.cr6.compare<int32_t>(static_cast<int32_t>(old_val), ctx.r5.s32, ctx.xer);
  ctx.cr0.lt = 0;
  ctx.cr0.gt = 0;
  ctx.cr0.eq = 1;
  ctx.cr0.so = ctx.xer.so;
}

REX_EXTERN(sub_8214F008) {
  uint32_t* ptr = GuestWordPtr(base, ctx.r3.u32);
  const uint32_t one_be = __builtin_bswap32(1u);
  uint32_t spins = 0;
  while (true) {
    if (__atomic_load_n(ptr, __ATOMIC_RELAXED) == 0u) {
      uint32_t zero_be = 0u;
      if (__atomic_compare_exchange_n(ptr, &zero_be, one_be, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        ctx.reserved.u32 = 0u;
        ctx.r10.u64 = 0x8000u;
        ctx.r11.u64 = 0u;
        ctx.r3.u64 = 0u;
        ctx.cr0.lt = 0;
        ctx.cr0.gt = 0;
        ctx.cr0.eq = 1;
        ctx.cr0.so = ctx.xer.so;
        ctx.cr6.compare<int32_t>(0, 0, ctx.xer);
        return;
      }
    }
    if (++spins < 16u) {
#if defined(__aarch64__)
      __asm__ __volatile__("yield" ::: "memory");
#endif
    } else if (spins < 64u) {
      std::this_thread::yield();
    } else {
#if defined(__SWITCH__)
      svcSleepThread(50000ULL);
#else
      std::this_thread::yield();
#endif
    }
  }
}

REX_EXTERN(sub_828172A8) {
#if defined(__SWITCH__)
  timeshift::telemetry::t_thread_role = 4;
  if (!g_thread_core_pinned) {
    Result rc_prio = 0;
    int32_t actual_core = -1;
    uint32_t applied_mask = 0;
    const Result rc_mask = PinCurrentThread(2, 0x4u, 0x21, &rc_prio, &actual_core, &applied_mask);
    REXLOG_INFO("[audio-thread] 828172A8 pinned ideal=2 mask=0x{:X} (allowed=0x{:X}) cur_core={} prio=0x21 (mask_rc=0x{:X} prio_rc=0x{:X})",
                applied_mask, QueryAllowedCoreMask(), actual_core, rc_mask, rc_prio);
  }
#endif
  __imp__sub_828172A8(ctx, base);
}

REX_EXTERN(sub_8287F678) {
#if defined(__SWITCH__)
  timeshift::telemetry::t_thread_role = 4;
  if (!g_thread_core_pinned) {
    char name[32] = {};
    const uint32_t obj_va = ctx.r3.u32;
    if (obj_va >= 0x10000u && obj_va < 0xDFFF0000u) {
      const char* src = reinterpret_cast<const char*>(base + obj_va + 4u);
      for (size_t i = 0; i < sizeof(name) - 1; ++i) {
        if (src[i] == '\0') break;
        name[i] = src[i];
      }
    }
    const bool is_fmod_mixer = (std::strncmp(name, "FMOD mixer", 10) == 0);
    const int32_t ideal_core = is_fmod_mixer ? 2 : 1;
    const uint32_t core_mask = is_fmod_mixer ? 0x4u : 0x6u;
    const int32_t prio = is_fmod_mixer ? 0x22 : 0x26;
    Result rc_prio = 0;
    int32_t actual_core = -1;
    uint32_t applied_mask = 0;
    const Result rc_mask = PinCurrentThread(ideal_core, core_mask, prio, &rc_prio, &actual_core, &applied_mask);
    REXLOG_INFO("[audio-thread] 8287F678 name='{}' pinned ideal={} mask=0x{:X} (allowed=0x{:X}) cur_core={} prio=0x{:X} (mask_rc=0x{:X} prio_rc=0x{:X})",
                name, ideal_core, applied_mask, QueryAllowedCoreMask(), actual_core, uint32_t(prio), rc_mask, rc_prio);
  }
#endif
  __imp__sub_8287F678(ctx, base);
}

REX_EXTERN(sub_82B08E80) {
#if defined(__SWITCH__)
  timeshift::telemetry::t_thread_role = 4;
  if (!g_thread_core_pinned) {
    Result rc_prio = 0;
    int32_t actual_core = -1;
    uint32_t applied_mask = 0;
    const Result rc_mask = PinCurrentThread(2, 0x6u, 0x26, &rc_prio, &actual_core, &applied_mask);
    REXLOG_INFO("[audio-thread] 82B08E80 pinned ideal=2 mask=0x{:X} (allowed=0x{:X}) cur_core={} prio=0x26 (mask_rc=0x{:X} prio_rc=0x{:X})",
                applied_mask, QueryAllowedCoreMask(), actual_core, rc_mask, rc_prio);
  }
#endif
  __imp__sub_82B08E80(ctx, base);
}

namespace timeshift::telemetry {
void AccountThreadCpuTicks(int role) {
#if defined(__SWITCH__)
  if (role < 0 || role >= 6) return;
  u64 now_ticks = 0;
  Result rc = svcGetInfo(&now_ticks, InfoType_ThreadTickCount, CUR_THREAD_HANDLE, TickCountInfo_Total);
  if (R_FAILED(rc)) {
    rc = svcGetInfo(&now_ticks, InfoType_ThreadTickCountDeprecated, CUR_THREAD_HANDLE, TickCountInfo_Total);
  }
  if (R_FAILED(rc)) return;
  if (g_prev_thread_cpu_ticks != 0u && now_ticks > g_prev_thread_cpu_ticks) {
    g_role_cpu_ticks[role].fetch_add(now_ticks - g_prev_thread_cpu_ticks, std::memory_order_relaxed);
  }
  g_prev_thread_cpu_ticks = now_ticks;
#else
  (void)role;
#endif
}
}  // namespace timeshift::telemetry

namespace {

inline int16_t LoadBeS16(const uint8_t* p) noexcept {
  uint16_t raw = 0;
  std::memcpy(&raw, p, sizeof(raw));
  return static_cast<int16_t>(__builtin_bswap16(raw));
}

inline void StoreBeS16(uint8_t* p, int16_t v) noexcept {
  const uint16_t be = __builtin_bswap16(static_cast<uint16_t>(v));
  std::memcpy(p, &be, sizeof(be));
}

inline float LoadBeF32(const uint8_t* p) noexcept {
  uint32_t raw = 0;
  std::memcpy(&raw, p, sizeof(raw));
  return std::bit_cast<float>(__builtin_bswap32(raw));
}

inline void StoreBeF32(uint8_t* p, float v) noexcept {
  const uint32_t be = __builtin_bswap32(std::bit_cast<uint32_t>(v));
  std::memcpy(p, &be, sizeof(be));
}

inline uint32_t LoadBeU32(const uint8_t* p) noexcept {
  uint32_t raw = 0;
  std::memcpy(&raw, p, sizeof(raw));
  return __builtin_bswap32(raw);
}

inline uint64_t LoadBeU64(const uint8_t* p) noexcept {
  uint64_t raw = 0;
  std::memcpy(&raw, p, sizeof(raw));
  return __builtin_bswap64(raw);
}

inline void StoreBeU64(uint8_t* p, uint64_t v) noexcept {
  const uint64_t be = __builtin_bswap64(v);
  std::memcpy(p, &be, sizeof(be));
}

inline void MaybeAccountAudioTicks(uint32_t mask) noexcept {
#if defined(__SWITCH__)
  static thread_local uint32_t s_aud_probe = 0;
  if (timeshift::telemetry::t_thread_role == 4 && (++s_aud_probe & mask) == 0u) {
    timeshift::telemetry::AccountThreadCpuTicks(4);
  }
#else
  (void)mask;
#endif
}

inline const uint8_t* GuestReadPtr(const uint8_t* base, uint32_t ea) noexcept {
  return base + ea + CrtPhysOff(ea);
}

template <bool Linear>
bool TryNativeFmodResampler(PPCContext& ctx, uint8_t* base) noexcept {
  const uint32_t out_va = ctx.r3.u32;
  const int32_t length = ctx.r4.s32;
  const uint32_t in_va = ctx.r5.u32;
  const int32_t format = ctx.r6.s32;
  const uint32_t pos_va = ctx.r7.u32;
  const uint32_t spd_va = ctx.r8.u32;
  const int32_t channels = ctx.r9.s32;

  if (length <= 0) return true;
  if (!out_va || !in_va || !pos_va || !spd_va) return false;
  if (channels <= 0 || channels > 8) return false;
  if (format != 1 && format != 2 && format != 5) return false;

  MaybeAccountAudioTicks(63u);

  uint8_t* out = base + out_va + CrtPhysOff(out_va);
  uint8_t* pos_ptr = base + pos_va + CrtPhysOff(pos_va);
  const uint8_t* spd_ptr = GuestReadPtr(base, spd_va);

  uint64_t pos = LoadBeU64(pos_ptr);
  const uint64_t spd = LoadBeU64(spd_ptr);
  if (spd == 0x8000000000000000ULL) {
    static std::atomic<uint32_t> s_nan_spd_hits{0};
    if (s_nan_spd_hits.fetch_add(1, std::memory_order_relaxed) < 4u) {
      REXLOG_WARN("[fmod] resampler guarded INT64_MIN (NaN) speed fmt={} ch={} len={}",
                  format, channels, length);
    }
    std::memset(out, 0, size_t(length) * size_t(channels) * 4u);
    return true;
  }

  constexpr float kInv128 = 1.0f / 128.0f;
  constexpr float kInv32768 = 1.0f / 32768.0f;
  constexpr float kInv2Pow32 = 1.0f / 4294967296.0f;

  if constexpr (!Linear) {
    if (format == 2 && channels == 1) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 1));
        StoreBeF32(out + size_t(i) * 4u, static_cast<float>(LoadBeS16(GuestReadPtr(base, ea))) * kInv32768);
      }
    } else if (format == 2 && channels == 2) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 2));
        StoreBeF32(out + size_t(i) * 8u, static_cast<float>(LoadBeS16(GuestReadPtr(base, ea))) * kInv32768);
        StoreBeF32(out + size_t(i) * 8u + 4u,
                   static_cast<float>(LoadBeS16(GuestReadPtr(base, static_cast<uint32_t>(ea + 2u)))) * kInv32768);
      }
    } else if (format == 5 && channels == 1) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 2));
        std::memcpy(out + size_t(i) * 4u, GuestReadPtr(base, ea), 4u);
      }
    } else if (format == 5 && channels == 2) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 3));
        std::memcpy(out + size_t(i) * 8u, GuestReadPtr(base, ea), 8u);
      }
    } else {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t base_s = static_cast<uint32_t>(idx * static_cast<uint32_t>(channels));
        uint8_t* dst = out + size_t(i) * size_t(channels) * 4u;
        if (format == 2) {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea = static_cast<uint32_t>(in_va + ((base_s + static_cast<uint32_t>(ch)) << 1));
            StoreBeF32(dst + size_t(ch) * 4u,
                       static_cast<float>(LoadBeS16(GuestReadPtr(base, ea))) * kInv32768);
          }
        } else if (format == 5) {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea = static_cast<uint32_t>(in_va + ((base_s + static_cast<uint32_t>(ch)) << 2));
            std::memcpy(dst + size_t(ch) * 4u, GuestReadPtr(base, ea), 4u);
          }
        } else {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea = static_cast<uint32_t>(in_va + base_s + static_cast<uint32_t>(ch));
            const int8_t s = static_cast<int8_t>(*GuestReadPtr(base, ea));
            StoreBeF32(dst + size_t(ch) * 4u, static_cast<float>(s) * kInv128);
          }
        }
      }
    }
  } else {
    if (format == 2 && channels == 1) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea0 = static_cast<uint32_t>(in_va + (idx << 1));
        const uint32_t ea1 = static_cast<uint32_t>(ea0 + 2u);
        const float frac = static_cast<float>(static_cast<uint32_t>(pos)) * kInv2Pow32;
        const float inv = 1.0f - frac;
        const float s0 = static_cast<float>(LoadBeS16(GuestReadPtr(base, ea0))) * kInv32768;
        const float s1 = static_cast<float>(LoadBeS16(GuestReadPtr(base, ea1))) * kInv32768;
        StoreBeF32(out + size_t(i) * 4u, s0 * inv + s1 * frac);
      }
    } else if (format == 2 && channels == 2) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 2));
        const float frac = static_cast<float>(static_cast<uint32_t>(pos)) * kInv2Pow32;
        const float inv = 1.0f - frac;
        const float l0 = static_cast<float>(LoadBeS16(GuestReadPtr(base, ea))) * kInv32768;
        const float r0 = static_cast<float>(LoadBeS16(GuestReadPtr(base, static_cast<uint32_t>(ea + 2u)))) * kInv32768;
        const float l1 = static_cast<float>(LoadBeS16(GuestReadPtr(base, static_cast<uint32_t>(ea + 4u)))) * kInv32768;
        const float r1 = static_cast<float>(LoadBeS16(GuestReadPtr(base, static_cast<uint32_t>(ea + 6u)))) * kInv32768;
        StoreBeF32(out + size_t(i) * 8u, l0 * inv + l1 * frac);
        StoreBeF32(out + size_t(i) * 8u + 4u, r0 * inv + r1 * frac);
      }
    } else if (format == 5 && channels == 1) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea0 = static_cast<uint32_t>(in_va + (idx << 2));
        const uint32_t ea1 = static_cast<uint32_t>(ea0 + 4u);
        const float frac = static_cast<float>(static_cast<uint32_t>(pos)) * kInv2Pow32;
        const float inv = 1.0f - frac;
        const float s0 = LoadBeF32(GuestReadPtr(base, ea0));
        const float s1 = LoadBeF32(GuestReadPtr(base, ea1));
        StoreBeF32(out + size_t(i) * 4u, s0 * inv + s1 * frac);
      }
    } else if (format == 5 && channels == 2) {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t ea = static_cast<uint32_t>(in_va + (idx << 3));
        const float frac = static_cast<float>(static_cast<uint32_t>(pos)) * kInv2Pow32;
        const float inv = 1.0f - frac;
        const float l0 = LoadBeF32(GuestReadPtr(base, ea));
        const float r0 = LoadBeF32(GuestReadPtr(base, static_cast<uint32_t>(ea + 4u)));
        const float l1 = LoadBeF32(GuestReadPtr(base, static_cast<uint32_t>(ea + 8u)));
        const float r1 = LoadBeF32(GuestReadPtr(base, static_cast<uint32_t>(ea + 12u)));
        StoreBeF32(out + size_t(i) * 8u, l0 * inv + l1 * frac);
        StoreBeF32(out + size_t(i) * 8u + 4u, r0 * inv + r1 * frac);
      }
    } else {
      for (int32_t i = 0; i < length; ++i, pos += spd) {
        const uint32_t idx = static_cast<uint32_t>(pos >> 32);
        const uint32_t base_s = static_cast<uint32_t>(idx * static_cast<uint32_t>(channels));
        const uint32_t next_s = static_cast<uint32_t>(base_s + static_cast<uint32_t>(channels));
        const float frac = static_cast<float>(static_cast<uint32_t>(pos)) * kInv2Pow32;
        const float inv = 1.0f - frac;
        uint8_t* dst = out + size_t(i) * size_t(channels) * 4u;
        if (format == 2) {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea0 = static_cast<uint32_t>(in_va + ((base_s + static_cast<uint32_t>(ch)) << 1));
            const uint32_t ea1 = static_cast<uint32_t>(in_va + ((next_s + static_cast<uint32_t>(ch)) << 1));
            const float s0 = static_cast<float>(LoadBeS16(GuestReadPtr(base, ea0))) * kInv32768;
            const float s1 = static_cast<float>(LoadBeS16(GuestReadPtr(base, ea1))) * kInv32768;
            StoreBeF32(dst + size_t(ch) * 4u, s0 * inv + s1 * frac);
          }
        } else if (format == 5) {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea0 = static_cast<uint32_t>(in_va + ((base_s + static_cast<uint32_t>(ch)) << 2));
            const uint32_t ea1 = static_cast<uint32_t>(in_va + ((next_s + static_cast<uint32_t>(ch)) << 2));
            const float s0 = LoadBeF32(GuestReadPtr(base, ea0));
            const float s1 = LoadBeF32(GuestReadPtr(base, ea1));
            StoreBeF32(dst + size_t(ch) * 4u, s0 * inv + s1 * frac);
          }
        } else {
          for (int32_t ch = 0; ch < channels; ++ch) {
            const uint32_t ea0 = static_cast<uint32_t>(in_va + base_s + static_cast<uint32_t>(ch));
            const uint32_t ea1 = static_cast<uint32_t>(in_va + next_s + static_cast<uint32_t>(ch));
            const float s0 = static_cast<float>(static_cast<int8_t>(*GuestReadPtr(base, ea0))) * kInv128;
            const float s1 = static_cast<float>(static_cast<int8_t>(*GuestReadPtr(base, ea1))) * kInv128;
            StoreBeF32(dst + size_t(ch) * 4u, s0 * inv + s1 * frac);
          }
        }
      }
    }
  }

  StoreBeU64(pos_ptr, pos);
  return true;
}

}  // namespace

REX_EXTERN(sub_828DEE18) {
  if (!TryNativeFmodResampler<false>(ctx, base)) {
    __imp__sub_828DEE18(ctx, base);
  }
}

REX_EXTERN(sub_828D7818) {
  if (!TryNativeFmodResampler<true>(ctx, base)) {
    __imp__sub_828D7818(ctx, base);
  }
}

REX_EXTERN(sub_828DC538) {
  if (!TryNativeFmodResampler<true>(ctx, base)) {
    __imp__sub_828DC538(ctx, base);
  }
}

REX_EXTERN(sub_828DA770) {
  if (!TryNativeFmodResampler<true>(ctx, base)) {
    __imp__sub_828DA770(ctx, base);
  }
}

REX_EXTERN(sub_828C80D0) {
  if (!std::isfinite(ctx.f1.f64)) {
    static std::atomic<uint32_t> s_nan_freq_hits{0};
    if (s_nan_freq_hits.fetch_add(1, std::memory_order_relaxed) < 4u) {
      REXLOG_WARN("[fmod] clamped non-finite DSPResampler frequency (bits=0x{:08X}) to 0",
                  std::bit_cast<uint32_t>(static_cast<float>(ctx.f1.f64)));
    }
    ctx.f1.f64 = 0.0;
  }
  __imp__sub_828C80D0(ctx, base);
}

REX_EXTERN(sub_828DFCC0) {
  const uint32_t out_va = ctx.r3.u32;
  const uint32_t in_va = ctx.r4.u32;
  const int32_t outfmt = ctx.r5.s32;
  const int32_t infmt = ctx.r6.s32;
  const int32_t length = ctx.r7.s32;
  const int32_t dst_step = ctx.r8.s32;
  const int32_t src_step = ctx.r9.s32;
  const float vol = static_cast<float>(ctx.f1.f64);

  if (length <= 0) {
    ctx.r3.u64 = 0u;
    return;
  }
  if (out_va && in_va && dst_step == 1 && src_step == 1) {
    uint8_t* out = base + out_va + CrtPhysOff(out_va);
    const uint8_t* in = base + in_va + CrtPhysOff(in_va);
    if (outfmt == 5 && infmt == 5) {
      if (vol == 1.0f) {
        if (out_va != in_va) std::memmove(out, in, size_t(length) * 4u);
      } else {
        for (int32_t i = 0; i < length; ++i) {
          StoreBeF32(out + ptrdiff_t(i) * 4, LoadBeF32(in + ptrdiff_t(i) * 4) * vol);
        }
      }
      ctx.r3.u64 = 0u;
      return;
    }
    if (outfmt == 2 && infmt == 5) {
      const float scale = vol * 32768.0f;
      for (int32_t i = 0; i < length; ++i) {
        const float x = LoadBeF32(in + ptrdiff_t(i) * 4) * scale;
        const int32_t q = !(x > -32768.0f) ? -32768 : (x >= 32767.0f) ? 32767 : static_cast<int32_t>(x);
        StoreBeS16(out + ptrdiff_t(i) * 2, static_cast<int16_t>(q));
      }
      ctx.r3.u64 = 0u;
      return;
    }
    if (outfmt == 5 && infmt == 2) {
      const float scale = vol * (1.0f / 32768.0f);
      if (out_va >= in_va && out_va < in_va + uint32_t(length) * 2u) {
        for (int32_t i = length - 1; i >= 0; --i) {
          StoreBeF32(out + ptrdiff_t(i) * 4, static_cast<float>(LoadBeS16(in + ptrdiff_t(i) * 2)) * scale);
        }
      } else {
        for (int32_t i = 0; i < length; ++i) {
          StoreBeF32(out + ptrdiff_t(i) * 4, static_cast<float>(LoadBeS16(in + ptrdiff_t(i) * 2)) * scale);
        }
      }
      ctx.r3.u64 = 0u;
      return;
    }
  }
  __imp__sub_828DFCC0(ctx, base);
}

namespace {

inline void FmodMixMonoTo2(uint8_t* out, const uint8_t* in, uint32_t length, float v0, float v1) noexcept {
  if (v0 == 0.0f && v1 == 0.0f) return;
  for (uint32_t i = 0; i < length; ++i) {
    const float s = LoadBeF32(in + size_t(i) * 4u);
    uint8_t* dst = out + size_t(i) * 8u;
    if (v0 != 0.0f) StoreBeF32(dst, LoadBeF32(dst) + s * v0);
    if (v1 != 0.0f) StoreBeF32(dst + 4u, LoadBeF32(dst + 4u) + s * v1);
  }
}

inline void FmodMix2To2Diag(uint8_t* out, const uint8_t* in, uint32_t length, float v0, float v1) noexcept {
  if (v0 == 0.0f && v1 == 0.0f) return;
  for (uint32_t i = 0; i < length; ++i) {
    const uint8_t* src = in + size_t(i) * 8u;
    uint8_t* dst = out + size_t(i) * 8u;
    if (v0 != 0.0f) StoreBeF32(dst, LoadBeF32(dst) + LoadBeF32(src) * v0);
    if (v1 != 0.0f) StoreBeF32(dst + 4u, LoadBeF32(dst + 4u) + LoadBeF32(src + 4u) * v1);
  }
}

inline void FmodMixMonoTo6(uint8_t* out, const uint8_t* in, uint32_t length, const float g[6]) noexcept {
  uint32_t active[6];
  uint32_t n = 0;
  for (uint32_t c = 0; c < 6u; ++c) {
    if (g[c] != 0.0f) active[n++] = c;
  }
  if (n == 0u) return;
  for (uint32_t i = 0; i < length; ++i) {
    const float s = LoadBeF32(in + size_t(i) * 4u);
    uint8_t* dst = out + size_t(i) * 24u;
    for (uint32_t k = 0; k < n; ++k) {
      const uint32_t c = active[k];
      uint8_t* p = dst + c * 4u;
      StoreBeF32(p, LoadBeF32(p) + s * g[c]);
    }
  }
}

inline void FmodMix6To6Diag(uint8_t* out, const uint8_t* in, uint32_t length, const float g[6]) noexcept {
  uint32_t active[6];
  uint32_t n = 0;
  for (uint32_t c = 0; c < 6u; ++c) {
    if (g[c] != 0.0f) active[n++] = c;
  }
  if (n == 0u) return;
  for (uint32_t i = 0; i < length; ++i) {
    const uint8_t* src = in + size_t(i) * 24u;
    uint8_t* dst = out + size_t(i) * 24u;
    for (uint32_t k = 0; k < n; ++k) {
      const uint32_t c = active[k];
      uint8_t* p = dst + c * 4u;
      StoreBeF32(p, LoadBeF32(p) + LoadBeF32(src + c * 4u) * g[c]);
    }
  }
}

}  // namespace

REX_EXTERN(sub_828D6248) {
  const uint32_t in_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  if (length == 0u) return;
  if (!in_va || !out_va) {
    __imp__sub_828D6248(ctx, base);
    return;
  }
  MaybeAccountAudioTicks(31u);
  FmodMixMonoTo2(base + out_va + CrtPhysOff(out_va), base + in_va + CrtPhysOff(in_va), length,
                 static_cast<float>(ctx.f1.f64), static_cast<float>(ctx.f2.f64));
}

REX_EXTERN(sub_828D6328) {
  const uint32_t in_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  if (length == 0u) return;
  if (!in_va || !out_va) {
    __imp__sub_828D6328(ctx, base);
    return;
  }
  MaybeAccountAudioTicks(31u);
  FmodMix2To2Diag(base + out_va + CrtPhysOff(out_va), base + in_va + CrtPhysOff(in_va), length,
                  static_cast<float>(ctx.f1.f64), static_cast<float>(ctx.f2.f64));
}

REX_EXTERN(sub_828D6418) {
  const uint32_t in_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  const uint32_t g03_va = ctx.r6.u32;
  const uint32_t g45_va = ctx.r7.u32;
  if (length == 0u) return;
  if (!in_va || !out_va || !g03_va || !g45_va) {
    __imp__sub_828D6418(ctx, base);
    return;
  }
  MaybeAccountAudioTicks(31u);
  const uint8_t* g03 = base + g03_va + CrtPhysOff(g03_va);
  const uint8_t* g45 = base + g45_va + CrtPhysOff(g45_va);
  const float g[6] = {LoadBeF32(g03), LoadBeF32(g03 + 4), LoadBeF32(g03 + 8),
                      LoadBeF32(g03 + 12), LoadBeF32(g45), LoadBeF32(g45 + 4)};
  FmodMixMonoTo6(base + out_va + CrtPhysOff(out_va), base + in_va + CrtPhysOff(in_va), length, g);
}

REX_EXTERN(sub_828D65E0) {
  const uint32_t in_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  const uint32_t g03_va = ctx.r6.u32;
  const uint32_t g45_va = ctx.r7.u32;
  if (length == 0u) return;
  if (!in_va || !out_va || !g03_va || !g45_va) {
    __imp__sub_828D65E0(ctx, base);
    return;
  }
  MaybeAccountAudioTicks(31u);
  const uint8_t* g03 = base + g03_va + CrtPhysOff(g03_va);
  const uint8_t* g45 = base + g45_va + CrtPhysOff(g45_va);
  const float g[6] = {LoadBeF32(g03), LoadBeF32(g03 + 4), LoadBeF32(g03 + 8),
                      LoadBeF32(g03 + 12), LoadBeF32(g45), LoadBeF32(g45 + 4)};
  FmodMix6To6Diag(base + out_va + CrtPhysOff(out_va), base + in_va + CrtPhysOff(in_va), length, g);
}

REX_EXTERN(sub_828C05A8) {
  const uint32_t conn_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  const uint32_t in_va = ctx.r5.u32;
  const int32_t out_ch = ctx.r6.s32;
  const int32_t in_ch = ctx.r7.s32;
  const uint32_t length = ctx.r8.u32;
  if (in_va == 0u || length == 0u) {
    ctx.r3.u64 = 0u;
    return;
  }
  if (!conn_va || !out_va || out_ch <= 0 || out_ch > 8 || in_ch <= 0 || in_ch > 8) {
    __imp__sub_828C05A8(ctx, base);
    return;
  }
  const uint8_t* conn = base + conn_va + CrtPhysOff(conn_va);
  if (LoadBeS16(conn + 112u) != 0) {
    __imp__sub_828C05A8(ctx, base);
    return;
  }
  if (LoadBeF32(conn + 116u) == 0.0f) {
    ctx.r3.u64 = 0u;
    return;
  }
  MaybeAccountAudioTicks(31u);

  float m[8][8]{};
  uint32_t active_rows[8]{};
  uint32_t active_n = 0u;
  for (int32_t o = 0; o < out_ch; ++o) {
    const uint32_t row_va = LoadBeU32(conn + 56u + uint32_t(o) * 4u);
    if (row_va == 0u) continue;
    const uint8_t* row = base + row_va + CrtPhysOff(row_va);
    bool nz = false;
    for (int32_t i = 0; i < in_ch; ++i) {
      const float v = LoadBeF32(row + uint32_t(i) * 4u);
      m[o][i] = v;
      if (v != 0.0f) nz = true;
    }
    if (nz) active_rows[active_n++] = static_cast<uint32_t>(o);
  }
  if (active_n == 0u) {
    ctx.r3.u64 = 0u;
    return;
  }

  uint8_t* out = base + out_va + CrtPhysOff(out_va);
  const uint8_t* in = base + in_va + CrtPhysOff(in_va);

  if (out_ch == 6 && in_ch == 1) {
    const float g[6] = {m[0][0], m[1][0], m[2][0], m[3][0], m[4][0], m[5][0]};
    FmodMixMonoTo6(out, in, length, g);
  } else if (out_ch == 6 && in_ch == 2) {
    for (uint32_t t = 0; t < length; ++t) {
      const uint8_t* src = in + size_t(t) * 8u;
      const float s0 = LoadBeF32(src);
      const float s1 = LoadBeF32(src + 4u);
      uint8_t* dst = out + size_t(t) * 24u;
      for (uint32_t k = 0; k < active_n; ++k) {
        const uint32_t o = active_rows[k];
        uint8_t* p = dst + o * 4u;
        StoreBeF32(p, LoadBeF32(p) + s0 * m[o][0] + s1 * m[o][1]);
      }
    }
  } else if (out_ch == 6 && in_ch == 6) {
    bool is_diag = true;
    for (int32_t o = 0; o < 6 && is_diag; ++o) {
      for (int32_t i = 0; i < 6; ++i) {
        if (o != i && m[o][i] != 0.0f) {
          is_diag = false;
          break;
        }
      }
    }
    if (is_diag) {
      const float g[6] = {m[0][0], m[1][1], m[2][2], m[3][3], m[4][4], m[5][5]};
      FmodMix6To6Diag(out, in, length, g);
    } else {
      for (uint32_t t = 0; t < length; ++t) {
        const uint8_t* src = in + size_t(t) * 24u;
        const float s[6] = {LoadBeF32(src), LoadBeF32(src + 4u), LoadBeF32(src + 8u),
                            LoadBeF32(src + 12u), LoadBeF32(src + 16u), LoadBeF32(src + 20u)};
        uint8_t* dst = out + size_t(t) * 24u;
        for (uint32_t k = 0; k < active_n; ++k) {
          const uint32_t o = active_rows[k];
          const float sum = s[0] * m[o][0] + s[1] * m[o][1] + s[2] * m[o][2] +
                            s[3] * m[o][3] + s[4] * m[o][4] + s[5] * m[o][5];
          uint8_t* p = dst + o * 4u;
          StoreBeF32(p, LoadBeF32(p) + sum);
        }
      }
    }
  } else if (out_ch == 2 && in_ch == 1) {
    FmodMixMonoTo2(out, in, length, m[0][0], m[1][0]);
  } else if (out_ch == 2 && in_ch == 2 && m[0][1] == 0.0f && m[1][0] == 0.0f) {
    FmodMix2To2Diag(out, in, length, m[0][0], m[1][1]);
  } else {
    const size_t in_stride = size_t(in_ch) * 4u;
    const size_t out_stride = size_t(out_ch) * 4u;
    for (uint32_t t = 0; t < length; ++t) {
      const uint8_t* src = in + size_t(t) * in_stride;
      float s[8];
      for (int32_t i = 0; i < in_ch; ++i) {
        s[i] = LoadBeF32(src + uint32_t(i) * 4u);
      }
      uint8_t* dst = out + size_t(t) * out_stride;
      for (uint32_t k = 0; k < active_n; ++k) {
        const uint32_t o = active_rows[k];
        float sum = 0.0f;
        for (int32_t i = 0; i < in_ch; ++i) {
          sum += s[i] * m[o][i];
        }
        uint8_t* p = dst + o * 4u;
        StoreBeF32(p, LoadBeF32(p) + sum);
      }
    }
  }
  ctx.r3.u64 = 0u;
}

REX_EXTERN(sub_828C6470) {
  MaybeAccountAudioTicks(15u);
  if (REXCVAR_GET(renut_audio_reverb)) {
    __imp__sub_828C6470(ctx, base);
    return;
  }
  const uint32_t in_va = ctx.r4.u32;
  const uint32_t out_va = ctx.r5.u32;
  const int32_t channels = ctx.r6.s32;
  const uint32_t length = ctx.r7.u32;
  const float dry_gain = static_cast<float>(ctx.f2.f64);
  if (channels > 0 && length > 0u && out_va != 0u) {
    const uint64_t total = uint64_t(channels) * uint64_t(length);
    uint8_t* out = base + out_va + CrtPhysOff(out_va);
    if (dry_gain == 0.0f || in_va == 0u) {
      std::memset(out, 0, size_t(total * 4u));
    } else if (dry_gain == 1.0f) {
      if (in_va != out_va) {
        std::memmove(out, base + in_va + CrtPhysOff(in_va), size_t(total * 4u));
      }
    } else {
      const uint8_t* in = base + in_va + CrtPhysOff(in_va);
      for (uint64_t i = 0; i < total; ++i) {
        StoreBeF32(out + size_t(i) * 4u, LoadBeF32(in + size_t(i) * 4u) * dry_gain);
      }
    }
  }
}

REX_EXTERN(sub_8281AE38) {
#if defined(__SWITCH__)
  timeshift::telemetry::t_thread_role = 4;
#endif
  __imp__sub_8281AE38(ctx, base);
  MaybeAccountAudioTicks(31u);
}

REX_EXTERN(sub_8281A8E8) {
  const uint32_t ea = 0x82CBFC3Cu + (ctx.r3.u32 << 2);
  uint32_t* ptr = GuestWordPtr(base, ea);
  uint32_t cur_be = __atomic_load_n(ptr, __ATOMIC_RELAXED);
  const uint32_t add_val = ctx.r4.u32;
  uint32_t next_be = 0;
  do {
    next_be = __builtin_bswap32(__builtin_bswap32(cur_be) + add_val);
  } while (!__atomic_compare_exchange_n(ptr, &cur_be, next_be, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
  ctx.r3.u64 = 0u;
}

REX_EXTERN(sub_8264DCF0) {
  const uint32_t obj_va = ctx.r3.u32;
  uint32_t* ref_ptr = GuestWordPtr(base, obj_va + 4u);
  uint32_t cur_be = __atomic_load_n(ref_ptr, __ATOMIC_RELAXED);
  while (true) {
    const uint32_t cur_val = __builtin_bswap32(cur_be);
    if (cur_val <= 1u) {
      __imp__sub_8264DCF0(ctx, base);
      return;
    }
    const uint32_t next_val = cur_val - 1u;
    const uint32_t next_be = __builtin_bswap32(next_val);
    if (__atomic_compare_exchange_n(ref_ptr, &cur_be, next_be, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
      ctx.r3.u64 = uint64_t(next_val);
      return;
    }
  }
}

REX_EXTERN(sub_82150CE0) {
  o1_guest_heap::HeapGuard lock;
  ctx.r3.u64 = o1_guest_heap::MallocLocked(base, ctx.r3.u32);
}

REX_EXTERN(sub_82151158) {
  const uint32_t ptr_va = ctx.r3.u32;
  if (ptr_va != 0u) {
    o1_guest_heap::HeapGuard lock;
    o1_guest_heap::FreeLocked(base, ptr_va);
  }
}

REX_EXTERN(sub_82151D58) {
  o1_guest_heap::HeapGuard lock;
  ctx.r3.u64 = o1_guest_heap::ReallocLocked(base, ctx.r3.u32, ctx.r4.u32);
}

REX_EXTERN(sub_82151A58) {
  o1_guest_heap::HeapGuard lock;
  ctx.r3.u64 = o1_guest_heap::ReallocLocked(base, ctx.r4.u32, ctx.r5.u32);
}

REX_EXTERN(sub_8214ED50) {
  const uint32_t ptr_va = ctx.r3.u32;
  if (ptr_va != 0u) {
    o1_guest_heap::HeapGuard lock;
    o1_guest_heap::FreeLocked(base, ptr_va);
  }
}

REX_EXTERN(sub_8214ED48) {
  o1_guest_heap::HeapGuard lock;
  ctx.r3.u64 = o1_guest_heap::MallocLocked(base, ctx.r3.u32);
}

REX_EXTERN(sub_8214ED58) {
  const uint32_t sz = ctx.r3.u32 ? ctx.r3.u32 : 1u;
  o1_guest_heap::HeapGuard lock;
  ctx.r3.u64 = o1_guest_heap::MallocLocked(base, sz);
}

REX_EXTERN(sub_8214EEC0) {
  const uint32_t ptr_va = ctx.r3.u32;
  const uint32_t new_sz = ctx.r4.u32;
  o1_guest_heap::HeapGuard lock;
  if (ptr_va == 0u) {
    ctx.r3.u64 = o1_guest_heap::MallocLocked(base, new_sz ? new_sz : 1u);
  } else if (new_sz == 0u) {
    o1_guest_heap::FreeLocked(base, ptr_va);
    ctx.r3.u64 = 0u;
  } else {
    ctx.r3.u64 = o1_guest_heap::ReallocLocked(base, ptr_va, new_sz);
  }
}

namespace {
inline void NativeGuestMemMove(PPCContext& ctx, uint8_t* base) noexcept {
  const uint32_t n = ctx.r5.u32;
  const uint32_t dst_va = ctx.r3.u32;
  const uint32_t src_va = ctx.r4.u32;
  if (n != 0u && dst_va != src_va) {
    std::memmove(base + dst_va + CrtPhysOff(dst_va),
                 base + src_va + CrtPhysOff(src_va), n);
  }
}
}  // namespace

REX_EXTERN(sub_828E1800) {
  NativeGuestMemMove(ctx, base);
}

REX_EXTERN(sub_82908B38) {
  NativeGuestMemMove(ctx, base);
}

REX_EXTERN(sub_828E10C0) {
  NativeGuestMemMove(ctx, base);
}

REX_EXTERN(sub_828E27E0) {
  const uint32_t n = ctx.r5.u32;
  if (n != 0u) {
    const uint32_t dst_va = ctx.r3.u32;
    std::memset(base + dst_va + CrtPhysOff(dst_va), static_cast<int>(ctx.r4.u8), n);
  }
}

REX_EXTERN(sub_828E8360) {
  const uint8_t* s1 = base + ctx.r3.u32 + CrtPhysOff(ctx.r3.u32);
  const uint8_t* s2 = base + ctx.r4.u32 + CrtPhysOff(ctx.r4.u32);
  int32_t diff = 0;
  while (true) {
    uint8_t a = *s1++;
    uint8_t b = *s2++;
    diff = int32_t(a) - int32_t(b);
    if (b == 0) break;
    if (diff == 0) continue;
    if (b >= 'A' && b <= 'Z') b |= 32u;
    if (a >= 'A' && a <= 'Z') a |= 32u;
    diff = int32_t(a) - int32_t(b);
    if (diff != 0) break;
  }
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(diff));
  ctx.cr0.compare<int32_t>(diff, 0, ctx.xer);
}

REX_EXTERN(sub_829087E0) {
  const uint8_t* s1 = base + ctx.r3.u32 + CrtPhysOff(ctx.r3.u32);
  const uint8_t* s2 = base + ctx.r4.u32 + CrtPhysOff(ctx.r4.u32);
  int32_t diff = 0;
  while (true) {
    const uint8_t a = *s1++;
    const uint8_t b = *s2++;
    diff = int32_t(a) - int32_t(b);
    if (a == 0 || diff != 0) break;
  }
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(diff));
}

REX_EXTERN(sub_82908A30) {
  const char* s = reinterpret_cast<const char*>(base + ctx.r3.u32 + CrtPhysOff(ctx.r3.u32));
  ctx.r3.u64 = static_cast<uint32_t>(std::strlen(s));
}

REX_EXTERN(sub_828E1360) {
  const uint32_t n = ctx.r5.u32;
  if (n != 0u) {
    char* dst = reinterpret_cast<char*>(base + ctx.r3.u32 + CrtPhysOff(ctx.r3.u32));
    const char* src = reinterpret_cast<const char*>(base + ctx.r4.u32 + CrtPhysOff(ctx.r4.u32));
    std::strncpy(dst, src, n);
  }
}

// Native ARM64 CRT math overrides replacing recompiled PowerPC libm sequences.
REX_EXTERN(sub_828E2C18) {
  ctx.f1.f64 = std::cos(ctx.f1.f64);
}

REX_EXTERN(sub_828E2B48) {
  ctx.f1.f64 = std::sin(ctx.f1.f64);
}

REX_EXTERN(sub_828E0E90) {
  ctx.f1.f64 = std::tan(ctx.f1.f64);
}

REX_EXTERN(sub_828E5F70) {
  ctx.f1.f64 = std::atan2(ctx.f1.f64, ctx.f2.f64);
}

REX_EXTERN(sub_828E5CD0) {
  ctx.f1.f64 = std::exp(ctx.f1.f64);
}

REX_EXTERN(sub_828E3228) {
  ctx.f1.f64 = std::log(ctx.f1.f64);
}

REX_EXTERN(sub_828E3700) {
  ctx.f1.f64 = std::pow(ctx.f1.f64, ctx.f2.f64);
}

REX_EXTERN(sub_828E2D08) {
  ctx.f1.f64 = std::floor(ctx.f1.f64);
}

REX_EXTERN(sub_828E4CD8) {
  ctx.f1.f64 = std::ceil(ctx.f1.f64);
}

REX_EXTERN(sub_828E3D10) {
  ctx.f1.f64 = static_cast<double>(ctx.r3.u64);
}

REX_EXTERN(sub_828E8420) {
  ctx.r3.u64 = std::isfinite(ctx.f1.f64) ? 1ull : 0ull;
}
