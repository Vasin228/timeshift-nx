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
#include <rex/logging.h>
#include <rex/runtime.h>
#include <o1heap.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(__SWITCH__)
#include <switch.h>
#endif

REX_EXTERN(__imp__sub_8264B1A8);
REX_EXTERN(__imp__sub_8264AD00);
REX_EXTERN(__imp__sub_82145740);
REX_EXTERN(__imp__sub_821455E8);
REX_EXTERN(__imp__sub_828172A8);
REX_EXTERN(__imp__sub_8287F678);
REX_EXTERN(__imp__sub_82B08E80);

namespace {

thread_local bool g_thread_core_pinned = false;

inline uint32_t* GuestWordPtr(uint8_t* base, uint32_t ea) {
  const uint32_t phys_off = (ea >= 0xE0000000u) ? 0x1000u : 0u;
  return reinterpret_cast<uint32_t*>(base + ea + phys_off);
}

#if defined(__SWITCH__)
Result PinCurrentThread(int32_t ideal_core, uint32_t core_mask, int32_t prio, Result* out_prio_rc = nullptr) {
  Result rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, ideal_core, core_mask);
  if (R_FAILED(rc_mask)) {
    const int32_t fallback_ideal = (ideal_core == 1) ? 1 : 0;
    rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, fallback_ideal, 0x3u);
  }
  const Result rc_prio = svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
  if (out_prio_rc) *out_prio_rc = rc_prio;
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
  HeapGuard() { mutexLock(&g_mutex); }
  ~HeapGuard() { mutexUnlock(&g_mutex); }
};
#else
std::mutex g_mutex;
using HeapGuard = std::lock_guard<std::mutex>;
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
  ++g_alloc_cnt;
  if (req_size > kMaxValidReq) return 0u;
  const uint32_t req = (req_size == 0u) ? 16u : req_size;
  auto* vheap = GetVirtualHeap64K();
  if (!vheap) return 0u;

  if (req >= kHugeThreshold) {
    const uint32_t total = (req + 16u + 65535u) & ~65535u;
    uint32_t va = 0u;
    if (!vheap->Alloc(total, 65536u,
                      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &va)) {
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
    if (!vheap->Alloc(kArenaSize, 65536u,
                      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &va)) {
      return 0u;
    }
    O1HeapInstance* inst = o1heapInit(base + va, kArenaSize);
    if (!inst) {
      vheap->Release(va);
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
    if (vheap) vheap->Release(base_va);
    if (g_huge_live > 0u) --g_huge_live;
    return;
  }
  if (idx < kMaxArenas && g_arenas[idx].inst) {
    o1heapFree(g_arenas[idx].inst, hdr);
    if (g_arenas[idx].live_allocs > 0u) --g_arenas[idx].live_allocs;
    if (idx >= 2u && g_arenas[idx].live_allocs == 0u) {
      if (vheap) vheap->Release(g_arenas[idx].va_start);
      g_arenas[idx] = Arena{};
      if (g_last_arena == idx) g_last_arena = 0u;
    }
  }
}

uint32_t ReallocLocked(uint8_t* base, uint32_t old_va, uint32_t new_size) {
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

inline void PinBackgroundThreadToCore3(const char* tag) {
#if defined(__SWITCH__)
  if (!g_thread_core_pinned) {
    Result rc_prio = 0;
    const Result rc_mask = PinCurrentThread(3, 0x8u, 0x28, &rc_prio);
    static std::atomic<uint32_t> s_bg_pin_logs{0};
    if (s_bg_pin_logs.fetch_add(1, std::memory_order_relaxed) < 20u) {
      REXLOG_INFO("[cpu-pin] bg {} pinned ideal=3 mask=0x8 rc=0x{:X} prio=0x28 rc=0x{:X}",
                  tag, uint32_t(rc_mask), uint32_t(rc_prio));
    }
  }
#else
  (void)tag;
#endif
}

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
    if (!g_thread_core_pinned) {
      Result rc_prio = 0;
      const Result rc_mask = PinCurrentThread(0, 0x3u, 0x24, &rc_prio);
      REXLOG_INFO("[cpu-pin] swap thread pinned ideal=0 mask=0x3 rc=0x{:X} prio=0x24 rc=0x{:X}",
                  uint32_t(rc_mask), uint32_t(rc_prio));
    }
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

      Video::NotePresentingSwap(swap_ordinal);
      Video::Present(image.texture ? &image : nullptr);

      {
        std::lock_guard lock(State().mutex);
        ::renut::gpu::SyncPipelineListLocked();
      }
      ::renut::gpu::MaybeReportMemory();

      // Advance presentation retirement counter (offset 16552) in guest device
      if (auto* dev = GuestPtr<uint8_t>(device_va)) {
        auto& retired = *reinterpret_cast<be_u32*>(dev + kPresentRetiredCount);
        retired = uint32_t(swap_ordinal);
      }
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
  int32_t ideal_core = 3;
  uint32_t core_mask = 0x8u;
  int32_t prio = 0x28;
  if (caller_lr == 0x8224A53Cu) {
    // Main game simulation, physics, and AI thread (TID 04): dual-core spillover with TID 09.
    ideal_core = 1;
    core_mask = 0x3u;
    prio = 0x24;
  } else if (caller_lr == 0x82264AC8u && guest_hw == 3u) {
    // Dedicated Saber3D 3D scene & draw submission thread (TID 09): Core 0 primary (away from AudioWorker on Core 1), spillover to Core 1.
    ideal_core = 0;
    core_mask = 0x3u;
    prio = 0x24;
  } else if (caller_lr == 0x82264AC8u) {
    // With lock-free o1heap (libnx Mutex with priority inheritance), allow parallel Saber3D
    // workers onto Cores 0, 1, 3 (mask 0xB, prio 0x28 < TID 04/09 prio 0x24) so they use idle
    // Cycles when TID 04 waits on semaphores without ever preempting TID 04/09.
    ideal_core = 3;
    core_mask = 0xBu;
    prio = 0x28;
  } else {
    // Streaming and background asset decompression threads (0x8220040C, etc.): equal prio 0x28 on Core 3.
    ideal_core = 3;
    core_mask = 0x8u;
    prio = 0x28;
  }
  Result rc_prio = 0;
  const Result rc_mask = PinCurrentThread(ideal_core, core_mask, prio, &rc_prio);
  timeshift::telemetry::g_game_core_mask.fetch_or(1u << svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
  static std::atomic<uint32_t> s_worker_pin_logs{0};
  if (s_worker_pin_logs.fetch_add(1, std::memory_order_relaxed) < 20u) {
    REXLOG_INFO("[cpu-pin] worker lr=0x{:08X} hw={} pinned ideal={} mask=0x{:X} rc=0x{:X} prio=0x{:X} rc=0x{:X}",
                caller_lr, guest_hw, ideal_core, core_mask, uint32_t(rc_mask), uint32_t(prio), uint32_t(rc_prio));
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
    Result rc_prio = 0;
    const Result rc_mask = PinCurrentThread(3, 0x8u, 0x28, &rc_prio);
    static std::atomic<uint32_t> s_wait_pin_logs{0};
    if (s_wait_pin_logs.fetch_add(1, std::memory_order_relaxed) < 16u) {
      REXLOG_INFO("[cpu-pin] unpinned waiter pinned ideal=3 mask=0x8 rc=0x{:X} prio=0x28 rc=0x{:X}",
                  uint32_t(rc_mask), uint32_t(rc_prio));
    }
  }
  static thread_local uint32_t s_wait_probe = 0;
  const uint32_t probe = s_wait_probe++;
  if (probe == 0 || (probe & 15u) == 0) {
    timeshift::telemetry::g_game_core_mask.fetch_or(1u << svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
  }
#endif
}

namespace {
inline void LockFreeInterlockedAdd32(PPCContext& ctx, uint8_t* base, int32_t delta) {
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
  PinBackgroundThreadToCore3("828172A8");
  __imp__sub_828172A8(ctx, base);
}

REX_EXTERN(sub_8287F678) {
  PinBackgroundThreadToCore3("8287F678");
  __imp__sub_8287F678(ctx, base);
}

REX_EXTERN(sub_82B08E80) {
  PinBackgroundThreadToCore3("82B08E80");
  __imp__sub_82B08E80(ctx, base);
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
