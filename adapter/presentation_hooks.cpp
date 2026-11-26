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
#include <rex/system/xthread.h>
#include <o1heap.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <rex/ppc/func.h>
#if defined(__SWITCH__)
#include <pthread.h>
#include <switch.h>
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#if defined(__SWITCH__)
REXCVAR_DEFINE_BOOL(renut_audio_reverb, false, "Renut",
                    "Enable FMOD software SFX reverb DSP (off by default on Switch to save 10-15ms CPU in combat)");
#else
REXCVAR_DEFINE_BOOL(renut_audio_reverb, true, "Renut",
                    "Enable FMOD software SFX reverb DSP");
#endif
REXCVAR_DEFINE_BOOL(renut_guest_profiler, false, "Renut",
                    "Enable 268Hz guest hotspot sampler on Core 2 (Stage 8 profiling)");

REX_EXTERN(__imp__sub_8264B1A8);
REX_EXTERN(__imp__sub_8264AD00);
REX_EXTERN(__imp__sub_8264DCF0);
REX_EXTERN(__imp__sub_82144DA0);
REX_EXTERN(__imp__sub_82144E08);
REX_EXTERN(__imp__sub_82145740);
REX_EXTERN(__imp__sub_821455E8);
REX_EXTERN(__imp__sub_82228410);
REX_EXTERN(__imp__sub_82422970);
REX_EXTERN(__imp__sub_82424238);
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
REX_EXTERN(__imp__sub_829303D0);
REX_EXTERN(__imp__sub_82B08E80);
REX_EXTERN(__imp__sub_8218CA80);
REX_EXTERN(__imp__sub_821F4168);
REX_EXTERN(__imp__sub_821FD330);
REX_EXTERN(__imp__sub_82290748);
REX_EXTERN(__imp__sub_82401388);
REX_EXTERN(__imp__sub_82401D20);
REX_EXTERN(__imp__sub_82430090);
REX_EXTERN(__imp__sub_82436068);
REX_EXTERN(sub_821E1210);
REX_EXTERN(sub_821F0168);
REX_EXTERN(sub_8228ECF8);
REX_EXTERN(sub_8228F2E0);
REX_EXTERN(sub_8228FF80);
REX_EXTERN(sub_822FE0A0);
REX_EXTERN(sub_82401888);

#if defined(__SWITCH__)
extern "C" {
struct RexWatchdogThreadSnap {
  uint32_t tid;
  uint32_t start;
  uint32_t lr;
  uint32_t ctr;
  uint32_t r1;
  uint32_t r3;
  uint8_t is_guest;
  uint8_t is_running;
  uint16_t pad;
};
int rex_kernel_snapshot_threads_trylock(RexWatchdogThreadSnap* out, int max_count);
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

#if defined(__SWITCH__)
namespace guest_profiler {

inline constexpr uint32_t kGuestCodeBegin = 0x82140000u;
inline constexpr uint32_t kGuestCodeEnd = 0x82B20000u;

inline bool IsGuestCodeAddr(uint32_t va) noexcept {
  return va >= kGuestCodeBegin && va < kGuestCodeEnd;
}

inline uint32_t SafeGuestThreadId() noexcept {
  auto* thr = rex::system::XThread::GetCurrentThread();
  return thr ? thr->thread_id() : 0u;
}

std::atomic<uint32_t> g_tid_sim{0};
std::atomic<uint32_t> g_tid_draw{0};
std::atomic<uint32_t> g_tid_fmod_mixer{0};

struct ProfSlot {
  uint32_t key;
  uint32_t count;
  uint32_t last_ctr;
};

template <size_t Capacity>
struct AddrHist {
  static_assert((Capacity & (Capacity - 1u)) == 0u);
  ProfSlot slots[Capacity]{};
  uint32_t overflow = 0u;

  void Clear() noexcept {
    std::memset(slots, 0, sizeof(slots));
    overflow = 0u;
  }

  void Add(uint32_t key, uint32_t code_ctr) noexcept {
    if (key == 0u) return;
    size_t idx = (size_t(key) * 2654435761u) & (Capacity - 1u);
    for (size_t step = 0; step < Capacity; ++step) {
      ProfSlot& s = slots[idx];
      if (s.key == key) {
        ++s.count;
        if (code_ctr != 0u) s.last_ctr = code_ctr;
        return;
      }
      if (s.key == 0u) {
        s.key = key;
        s.count = 1u;
        s.last_ctr = code_ctr;
        return;
      }
      idx = (idx + 1u) & (Capacity - 1u);
    }
    ++overflow;
  }

  template <size_t TopN>
  size_t ExtractTop(ProfSlot (&out)[TopN]) const noexcept {
    size_t n = 0u;
    for (size_t i = 0; i < Capacity; ++i) {
      const ProfSlot& s = slots[i];
      if (s.key == 0u || s.count == 0u) continue;
      if (n == TopN && s.count <= out[TopN - 1u].count) continue;
      size_t pos = (n < TopN) ? n++ : (TopN - 1u);
      while (pos > 0u && out[pos - 1u].count < s.count) {
        out[pos] = out[pos - 1u];
        --pos;
      }
      out[pos] = s;
    }
    return n;
  }
};

struct WindowData {
  AddrHist<1024> sim_lr;
  AddrHist<512> sim_ctr;
  AddrHist<512> draw_lr;
  AddrHist<256> aud_lr;
  uint64_t ticks = 0u;
  uint64_t gcr_busy = 0u;
  uint64_t sim_n = 0u;
  uint64_t draw_n = 0u;
  uint64_t aud_n = 0u;
  uint64_t wrk_busy = 0u;

  void Clear() noexcept {
    sim_lr.Clear();
    sim_ctr.Clear();
    draw_lr.Clear();
    aud_lr.Clear();
    ticks = 0u;
    gcr_busy = 0u;
    sim_n = 0u;
    draw_n = 0u;
    aud_n = 0u;
    wrk_busy = 0u;
  }
};

Mutex g_prof_mtx = 0;
WindowData g_live{};

void* SamplerThreadMain(void*) {
  PinCurrentThread(2, 0x4u, 0x2D);
  REXLOG_INFO("[prof] 268Hz guest hotspot sampler started on core={} prio=0x2D",
              svcGetCurrentProcessorNumber());
  while (true) {
    svcSleepThread(3733000ULL);
    RexWatchdogThreadSnap snap[32]{};
    const int n = rex_kernel_snapshot_threads_trylock(snap, 32);
    if (n < 0) {
      mutexLock(&g_prof_mtx);
      ++g_live.ticks;
      ++g_live.gcr_busy;
      mutexUnlock(&g_prof_mtx);
      continue;
    }

    const uint32_t tid_sim = g_tid_sim.load(std::memory_order_relaxed);
    const uint32_t tid_draw = g_tid_draw.load(std::memory_order_relaxed);
    const uint32_t tid_aud = g_tid_fmod_mixer.load(std::memory_order_relaxed);

    uint32_t sim_lr = 0u;
    uint32_t sim_ctr = 0u;
    uint32_t draw_lr = 0u;
    uint32_t aud_lr = 0u;
    uint32_t wrk_busy = 0u;

    for (int i = 0; i < n; ++i) {
      const RexWatchdogThreadSnap& s = snap[i];
      if (!s.is_guest || !s.is_running) continue;
      if ((tid_sim != 0u && s.tid == tid_sim) || (tid_sim == 0u && s.start == 0x8280F810u)) {
        sim_lr = s.lr;
        if (IsGuestCodeAddr(s.ctr)) sim_ctr = s.ctr;
      } else if (tid_draw != 0u && s.tid == tid_draw) {
        draw_lr = s.lr;
      } else if (tid_aud != 0u && s.tid == tid_aud) {
        aud_lr = s.lr;
      } else if (s.start == 0x82264A98u && s.lr != 0x82810478u) {
        ++wrk_busy;
      }
    }

    mutexLock(&g_prof_mtx);
    ++g_live.ticks;
    if (sim_lr != 0u) {
      g_live.sim_lr.Add(sim_lr, sim_ctr);
      ++g_live.sim_n;
      if (sim_ctr != 0u) g_live.sim_ctr.Add(sim_ctr, 0u);
    }
    if (draw_lr != 0u) {
      g_live.draw_lr.Add(draw_lr, 0u);
      ++g_live.draw_n;
    }
    if (aud_lr != 0u) {
      g_live.aud_lr.Add(aud_lr, 0u);
      ++g_live.aud_n;
    }
    g_live.wrk_busy += wrk_busy;
    mutexUnlock(&g_prof_mtx);
  }
  return nullptr;
}

void EnsureStarted() {
  static const bool enabled = REXCVAR_GET(renut_guest_profiler);
  if (!enabled) return;
  static std::once_flag once;
  std::call_once(once, [] {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u * 1024u);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thr{};
    const int rc = pthread_create(&thr, &attr, SamplerThreadMain, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
      REXLOG_WARN("[prof] pthread_create failed (rc={})", rc);
    }
  });
}

template <size_t N>
void LogTopSlots(const char* tag, const ProfSlot (&top)[N], size_t begin, size_t count,
                 uint64_t denom, bool with_ctr) {
  if (begin >= count || denom == 0u) return;
  char buf[512];
  size_t pos = 0u;
  const size_t end = std::min(begin + 8u, count);
  for (size_t i = begin; i < end && pos + 40u < sizeof(buf); ++i) {
    const double pct = (100.0 * double(top[i].count)) / double(denom);
    int wrote = 0;
    if (with_ctr && top[i].last_ctr != 0u) {
      wrote = std::snprintf(buf + pos, sizeof(buf) - pos, "%s0x%08X:%u(%.1f%%)[c=0x%08X]",
                            (i == begin) ? "" : " ", top[i].key, top[i].count, pct, top[i].last_ctr);
    } else {
      wrote = std::snprintf(buf + pos, sizeof(buf) - pos, "%s0x%08X:%u(%.1f%%)",
                            (i == begin) ? "" : " ", top[i].key, top[i].count, pct);
    }
    if (wrote > 0) {
      const size_t avail = sizeof(buf) - 1u - pos;
      pos += (size_t(wrote) < avail) ? size_t(wrote) : avail;
    }
  }
  if (pos > 0u) {
    REXLOG_INFO("{} {}", tag, buf);
  }
}

}  // namespace guest_profiler
#endif

}  // namespace

namespace timeshift::telemetry {
void FlushGuestProfilerWindow() {
#if defined(__SWITCH__)
  if (!REXCVAR_GET(renut_guest_profiler)) return;
  static guest_profiler::WindowData s_snap{};
  mutexLock(&guest_profiler::g_prof_mtx);
  s_snap = guest_profiler::g_live;
  guest_profiler::g_live.Clear();
  mutexUnlock(&guest_profiler::g_prof_mtx);
  if (s_snap.ticks == 0u) return;

  REXLOG_INFO(
      "[prof-meta] 300f: ticks={} gcr_busy={} sim_n={} draw_n={} aud_n={} wrk_busy={} ovf={}/{}/{}/{}",
      s_snap.ticks, s_snap.gcr_busy, s_snap.sim_n, s_snap.draw_n, s_snap.aud_n, s_snap.wrk_busy,
      s_snap.sim_lr.overflow, s_snap.sim_ctr.overflow, s_snap.draw_lr.overflow, s_snap.aud_lr.overflow);

  guest_profiler::ProfSlot sim_top[16]{};
  const size_t sim_cnt = s_snap.sim_lr.ExtractTop(sim_top);
  guest_profiler::LogTopSlots("[prof-sim-1]", sim_top, 0u, sim_cnt, s_snap.sim_n, true);
  guest_profiler::LogTopSlots("[prof-sim-2]", sim_top, 8u, sim_cnt, s_snap.sim_n, true);

  guest_profiler::ProfSlot ctr_top[8]{};
  const size_t ctr_cnt = s_snap.sim_ctr.ExtractTop(ctr_top);
  guest_profiler::LogTopSlots("[prof-sim-ctr]", ctr_top, 0u, ctr_cnt, s_snap.sim_n, false);

  guest_profiler::ProfSlot draw_top[8]{};
  const size_t draw_cnt = s_snap.draw_lr.ExtractTop(draw_top);
  guest_profiler::LogTopSlots("[prof-draw]", draw_top, 0u, draw_cnt, s_snap.draw_n, false);

  guest_profiler::ProfSlot aud_top[6]{};
  const size_t aud_cnt = s_snap.aud_lr.ExtractTop(aud_top);
  guest_profiler::LogTopSlots("[prof-aud]", aud_top, 0u, aud_cnt, s_snap.aud_n, false);
#endif
}
}  // namespace timeshift::telemetry

namespace timeshift::renderer {
namespace {

using namespace d3d6274::layout;

inline constexpr uint32_t kPresentRetiredCount = 16552;
inline constexpr uint32_t kMaxFramesInFlight = 1;

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
    if (timeshift::telemetry::t_thread_role == 5) {
      timeshift::telemetry::t_thread_role = 1;
    }
    if (timeshift::telemetry::t_thread_role == 1 && guest_profiler::g_tid_draw.load(std::memory_order_relaxed) == 0u) {
      guest_profiler::g_tid_draw.store(guest_profiler::SafeGuestThreadId(), std::memory_order_relaxed);
    }
    guest_profiler::EnsureStarted();
    if (!g_thread_core_pinned) {
      Result rc_prio = 0;
      int32_t actual_core = -1;
      uint32_t applied_mask = 0;
      const Result rc_mask = PinCurrentThread(0, 0x3u, 0x24, &rc_prio, &actual_core, &applied_mask);
      REXLOG_INFO("[cpu-pin] swap thread pinned ideal=0 mask=0x{:X} (allowed=0x{:X}) cur_core={} rc=0x{:X} prio=0x24 rc=0x{:X}",
                  applied_mask, QueryAllowedCoreMask(), actual_core, uint32_t(rc_mask), uint32_t(rc_prio));
    }
    timeshift::telemetry::AccountThreadCpuTicks(timeshift::telemetry::t_thread_role);
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

    const uint64_t call_idx = g_swap_calls.fetch_add(1, std::memory_order_relaxed) + 1;
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
    guest_profiler::g_tid_sim.store(guest_profiler::SafeGuestThreadId(), std::memory_order_relaxed);
  } else if (caller_lr == 0x82264AC8u && guest_hw == 3u) {
    // Dedicated Saber3D 3D scene & draw submission thread (TID 09): Core 0 primary, spillover Core 1.
    role = 1;
    ideal_core = 0;
    core_mask = 0x3u;
    prio = 0x24;
    guest_profiler::g_tid_draw.store(guest_profiler::SafeGuestThreadId(), std::memory_order_relaxed);
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
    if (is_fmod_mixer) {
      guest_profiler::g_tid_fmod_mixer.store(guest_profiler::SafeGuestThreadId(), std::memory_order_relaxed);
    }
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

namespace {

inline void StoreBeU32(uint8_t* p, uint32_t v) noexcept {
  const uint32_t be = __builtin_bswap32(v);
  std::memcpy(p, &be, sizeof(be));
}

inline uint16_t LoadBeU16(const uint8_t* p) noexcept {
  uint16_t raw = 0;
  std::memcpy(&raw, p, sizeof(raw));
  return __builtin_bswap16(raw);
}

inline void StoreBeU16(uint8_t* p, uint16_t v) noexcept {
  const uint16_t be = __builtin_bswap16(v);
  std::memcpy(p, &be, sizeof(be));
}

inline int32_t LoadBeS32(const uint8_t* p) noexcept {
  return static_cast<int32_t>(LoadBeU32(p));
}

inline uint8_t* GuestWritePtr(uint8_t* base, uint32_t ea) noexcept {
  return base + ea + CrtPhysOff(ea);
}

inline uint32_t GuestKThreadBe() noexcept {
  static thread_local uint32_t s_kthread_be = 0u;
  if (s_kthread_be == 0u) [[unlikely]] {
    if (auto* thr = rex::system::XThread::GetCurrentThread()) {
      s_kthread_be = __builtin_bswap32(thr->guest_object());
    }
  }
  return s_kthread_be;
}

inline bool TryFastEnterSaberCs(uint8_t* base, uint32_t cs_va) noexcept {
  if (cs_va == 0u) return false;
  uint8_t* p = base + cs_va + CrtPhysOff(cs_va);
  if (p[32] == 0u || p[0] == 0u) return false;
  if (p[33] != 0xFFu) return true;

  const uint32_t cur_be = GuestKThreadBe();
  if (cur_be == 0u) return false;

  auto* lock_ptr = reinterpret_cast<int32_t*>(p + 16u);
  auto* rec_ptr = reinterpret_cast<uint32_t*>(p + 20u);
  auto* own_ptr = reinterpret_cast<uint32_t*>(p + 24u);
  auto* depth_ptr = reinterpret_cast<uint32_t*>(p + 36u);

  if (__atomic_load_n(own_ptr, __ATOMIC_RELAXED) == cur_be) {
    __atomic_fetch_add(lock_ptr, 1, __ATOMIC_RELAXED);
    const int32_t rec = static_cast<int32_t>(__builtin_bswap32(*rec_ptr)) + 1;
    *rec_ptr = __builtin_bswap32(static_cast<uint32_t>(rec));
    *depth_ptr = __builtin_bswap32(__builtin_bswap32(*depth_ptr) + 1u);
    return true;
  }

  constexpr uint32_t kBeOne = __builtin_bswap32(1u);
  for (int spin = 0; spin < 16; ++spin) {
    int32_t expected = -1;
    if (__atomic_compare_exchange_n(lock_ptr, &expected, 0, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      __atomic_store_n(own_ptr, cur_be, __ATOMIC_RELAXED);
      *rec_ptr = kBeOne;
      *depth_ptr = __builtin_bswap32(__builtin_bswap32(*depth_ptr) + 1u);
      return true;
    }
#if defined(__aarch64__)
    if (spin < 15) __asm__ __volatile__("yield" ::: "memory");
#endif
  }
  return false;
}

inline bool TryFastLeaveSaberCs(uint8_t* base, uint32_t cs_va) noexcept {
  if (cs_va == 0u) return false;
  uint8_t* p = base + cs_va + CrtPhysOff(cs_va);
  if (p[33] != 0xFFu) return true;

  const uint32_t cur_be = GuestKThreadBe();
  if (cur_be == 0u) return false;

  auto* lock_ptr = reinterpret_cast<int32_t*>(p + 16u);
  auto* rec_ptr = reinterpret_cast<uint32_t*>(p + 20u);
  auto* own_ptr = reinterpret_cast<uint32_t*>(p + 24u);
  auto* depth_ptr = reinterpret_cast<uint32_t*>(p + 36u);

  if (__atomic_load_n(own_ptr, __ATOMIC_RELAXED) != cur_be) return false;
  const int32_t rec = static_cast<int32_t>(__builtin_bswap32(*rec_ptr));
  if (rec <= 0) return false;

  if (rec > 1) {
    *rec_ptr = __builtin_bswap32(static_cast<uint32_t>(rec - 1));
    *depth_ptr = __builtin_bswap32(__builtin_bswap32(*depth_ptr) - 1u);
    __atomic_fetch_sub(lock_ptr, 1, __ATOMIC_RELAXED);
    return true;
  }

  if (__atomic_load_n(lock_ptr, __ATOMIC_RELAXED) != 0) return false;
  const uint32_t old_depth = __builtin_bswap32(*depth_ptr);
  *depth_ptr = __builtin_bswap32(old_depth - 1u);
  *rec_ptr = 0u;
  __atomic_store_n(own_ptr, 0u, __ATOMIC_RELAXED);

  int32_t expected = 0;
  if (__atomic_compare_exchange_n(lock_ptr, &expected, -1, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    return true;
  }

  // Waiter arrived concurrently; restore pre-leave state for __imp__sub_82144E08.
  __atomic_store_n(own_ptr, cur_be, __ATOMIC_RELAXED);
  *rec_ptr = __builtin_bswap32(1u);
  *depth_ptr = __builtin_bswap32(old_depth);
  return false;
}

inline void FastEnterSaberCsOrImp(PPCContext& ctx, uint8_t* base, uint32_t cs_va) {
  if (!TryFastEnterSaberCs(base, cs_va)) {
    ctx.r3.u64 = cs_va;
    ctx.r4.u64 = 0u;
    ctx.r5.u64 = 0u;
    __imp__sub_82144DA0(ctx, base);
  }
}

inline void FastLeaveSaberCsOrImp(PPCContext& ctx, uint8_t* base, uint32_t cs_va) {
  if (!TryFastLeaveSaberCs(base, cs_va)) {
    ctx.r3.u64 = cs_va;
    ctx.r4.u64 = 0u;
    ctx.r5.u64 = 0u;
    __imp__sub_82144E08(ctx, base);
  }
}

#if defined(__aarch64__)
inline float32x4_t LoadBeF32x4(const uint8_t* p) noexcept {
  return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(p)));
}

inline void StoreBeF32x4(uint8_t* p, float32x4_t v) noexcept {
  vst1q_u8(p, vrev32q_u8(vreinterpretq_u8_f32(v)));
}

inline float Dot3Be(float32x4_t a, float dx, float dy, float dz) noexcept {
  return vgetq_lane_f32(a, 0) * dx + vgetq_lane_f32(a, 1) * dy + vgetq_lane_f32(a, 2) * dz;
}
#endif

}  // namespace

// Native Saber critical section fast-path + GetPos (Stage 8B)
REX_EXTERN(sub_82144DA0) {
  if (!TryFastEnterSaberCs(base, ctx.r3.u32)) {
    __imp__sub_82144DA0(ctx, base);
  }
}

REX_EXTERN(sub_82144E08) {
  if (!TryFastLeaveSaberCs(base, ctx.r3.u32)) {
    __imp__sub_82144E08(ctx, base);
  }
}

REX_EXTERN(sub_82228410) {
  const uint32_t obj_va = ctx.r3.u32;
  const uint32_t out_va = ctx.r4.u32;
  if (obj_va == 0u || out_va == 0u) {
    __imp__sub_82228410(ctx, base);
    return;
  }
  const uint32_t cs_va = obj_va + 488u;
  if (!TryFastEnterSaberCs(base, cs_va)) {
    ctx.r3.u64 = cs_va;
    __imp__sub_82144DA0(ctx, base);
    ctx.r3.u64 = obj_va;
  }
  const uint32_t pos_va = obj_va + 104u;
  std::memmove(base + out_va + CrtPhysOff(out_va), GuestReadPtr(base, pos_va), 12u);
  if (!TryFastLeaveSaberCs(base, cs_va)) {
    ctx.r3.u64 = cs_va;
    __imp__sub_82144E08(ctx, base);
    ctx.r3.u64 = obj_va;
  }
}

// Native NEON XMMatrixMultiply (out[r5] = A[r3] * B[r4])
REX_EXTERN(sub_82424238) {
  const uint32_t a_va = ctx.r3.u32;
  const uint32_t b_va = ctx.r4.u32;
  const uint32_t out_va = ctx.r5.u32;
  if (!a_va || !b_va || !out_va) {
    __imp__sub_82424238(ctx, base);
    return;
  }
  const uint8_t* a_ptr = GuestReadPtr(base, a_va);
  const uint8_t* b_ptr = GuestReadPtr(base, b_va);
  uint8_t* out_ptr = base + out_va + CrtPhysOff(out_va);
#if defined(__aarch64__)
  const float32x4_t b0 = LoadBeF32x4(b_ptr + 0u);
  const float32x4_t b1 = LoadBeF32x4(b_ptr + 16u);
  const float32x4_t b2 = LoadBeF32x4(b_ptr + 32u);
  const float32x4_t b3 = LoadBeF32x4(b_ptr + 48u);
  const float32x4_t a0 = LoadBeF32x4(a_ptr + 0u);
  const float32x4_t a1 = LoadBeF32x4(a_ptr + 16u);
  const float32x4_t a2 = LoadBeF32x4(a_ptr + 32u);
  const float32x4_t a3 = LoadBeF32x4(a_ptr + 48u);
  auto mul_row = [&](float32x4_t a) noexcept -> float32x4_t {
    float32x4_t o = vmulq_laneq_f32(b0, a, 0);
    o = vfmaq_laneq_f32(o, b1, a, 1);
    o = vfmaq_laneq_f32(o, b2, a, 2);
    return vfmaq_laneq_f32(o, b3, a, 3);
  };
  const float32x4_t r0 = mul_row(a0);
  const float32x4_t r1 = mul_row(a1);
  const float32x4_t r2 = mul_row(a2);
  const float32x4_t r3 = mul_row(a3);
  StoreBeF32x4(out_ptr + 0u, r0);
  StoreBeF32x4(out_ptr + 16u, r1);
  StoreBeF32x4(out_ptr + 32u, r2);
  StoreBeF32x4(out_ptr + 48u, r3);
#else
  float a[4][4], b[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      a[i][j] = LoadBeF32(a_ptr + size_t(i * 4 + j) * 4u);
      b[i][j] = LoadBeF32(b_ptr + size_t(i * 4 + j) * 4u);
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      StoreBeF32(out_ptr + size_t(i * 4 + j) * 4u,
                 a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j]);
    }
  }
#endif
}

// Native NEON hkpTriangleShape::getSupportingVertex (shape[r3], dir[r4], out[r5])
REX_EXTERN(sub_829303D0) {
  const uint32_t shape_va = ctx.r3.u32;
  const uint32_t dir_va = ctx.r4.u32;
  const uint32_t out_va = ctx.r5.u32;
  if (!shape_va || !dir_va || !out_va) {
    __imp__sub_829303D0(ctx, base);
    return;
  }
#if defined(__aarch64__)
  const float32x4_t dir = LoadBeF32x4(GuestReadPtr(base, dir_va & ~0xFu));
  const float32x4_t v0 = LoadBeF32x4(GuestReadPtr(base, (shape_va + 32u) & ~0xFu));
  const float32x4_t v1 = LoadBeF32x4(GuestReadPtr(base, (shape_va + 48u) & ~0xFu));
  const float32x4_t v2 = LoadBeF32x4(GuestReadPtr(base, (shape_va + 64u) & ~0xFu));
  const float32x4_t ext = LoadBeF32x4(GuestReadPtr(base, (shape_va + 80u) & ~0xFu));

  const float dx = vgetq_lane_f32(dir, 0);
  const float dy = vgetq_lane_f32(dir, 1);
  const float dz = vgetq_lane_f32(dir, 2);
  const float d0 = Dot3Be(v0, dx, dy, dz);
  const float d1 = Dot3Be(v1, dx, dy, dz);
  const float d2 = Dot3Be(v2, dx, dy, dz);

  uint32_t best = 1u;
  float best_d = d1;
  float32x4_t res = v1;
  if (d2 > d1) {
    best = 2u;
    best_d = d2;
    res = v2;
  }
  if (d0 > best_d) {
    best = 0u;
    res = v0;
  }

  const float d_ext = Dot3Be(ext, dx, dy, dz);
  const float thresh = LoadBeF32(GuestReadPtr(base, 0x82000AA4u));
  if (d_ext > thresh) {
    res = vaddq_f32(res, ext);
    best += 3u;
  }

  const uint32_t out_aligned = out_va & ~0xFu;
  StoreBeF32x4(base + out_aligned + CrtPhysOff(out_aligned), res);
  StoreBeU32(base + (out_va + 12u) + CrtPhysOff(out_va + 12u), best | 0x3F000000u);
#else
  const uint8_t* dp = GuestReadPtr(base, dir_va & ~0xFu);
  const float dx = LoadBeF32(dp), dy = LoadBeF32(dp + 4u), dz = LoadBeF32(dp + 8u);
  float v[3][4];
  for (int i = 0; i < 3; ++i) {
    const uint32_t va = (shape_va + 32u + uint32_t(i) * 16u) & ~0xFu;
    const uint8_t* vp = GuestReadPtr(base, va);
    for (int j = 0; j < 4; ++j) v[i][j] = LoadBeF32(vp + uint32_t(j) * 4u);
  }
  const float d0 = v[0][0] * dx + v[0][1] * dy + v[0][2] * dz;
  const float d1 = v[1][0] * dx + v[1][1] * dy + v[1][2] * dz;
  const float d2 = v[2][0] * dx + v[2][1] * dy + v[2][2] * dz;
  uint32_t best = 1u;
  float best_d = d1;
  if (d2 > d1) { best = 2u; best_d = d2; }
  if (d0 > best_d) { best = 0u; }
  float res[4] = {v[best][0], v[best][1], v[best][2], v[best][3]};
  const uint8_t* ep = GuestReadPtr(base, (shape_va + 80u) & ~0xFu);
  const float ex = LoadBeF32(ep), ey = LoadBeF32(ep + 4u), ez = LoadBeF32(ep + 8u), ew = LoadBeF32(ep + 12u);
  const float thresh = LoadBeF32(GuestReadPtr(base, 0x82000AA4u));
  if (ex * dx + ey * dy + ez * dz > thresh) {
    res[0] += ex; res[1] += ey; res[2] += ez; res[3] += ew;
    best += 3u;
  }
  uint8_t* op = base + (out_va & ~0xFu) + CrtPhysOff(out_va & ~0xFu);
  for (int j = 0; j < 4; ++j) StoreBeF32(op + uint32_t(j) * 4u, res[j]);
  StoreBeU32(base + (out_va + 12u) + CrtPhysOff(out_va + 12u), best | 0x3F000000u);
#endif
}

// Native NEON XMVector3TransformCoordStream (dst[r3], out_stride[r4], src[r5], in_stride[r6], count[r7], M[r8,r9,r10,r1+88..127])
REX_EXTERN(sub_82422970) {
  const uint32_t count = ctx.r7.u32;
  if (count == 0u) return;
  const uint32_t dst_va = ctx.r3.u32;
  const uint32_t out_stride = ctx.r4.u32;
  const uint32_t src_va = ctx.r5.u32;
  const uint32_t in_stride = ctx.r6.u32;
  if (!dst_va || !src_va) {
    __imp__sub_82422970(ctx, base);
    return;
  }

  uint8_t m_be[64];
  StoreBeU64(m_be + 0u, ctx.r8.u64);
  StoreBeU64(m_be + 8u, ctx.r9.u64);
  StoreBeU64(m_be + 16u, ctx.r10.u64);
  std::memcpy(m_be + 24u, GuestReadPtr(base, ctx.r1.u32 + 88u), 40u);

#if defined(__aarch64__)
  const float32x4_t row0 = LoadBeF32x4(m_be + 0u);
  const float32x4_t row1 = LoadBeF32x4(m_be + 16u);
  const float32x4_t row2 = LoadBeF32x4(m_be + 32u);
  const float32x4_t row3 = LoadBeF32x4(m_be + 48u);
  for (uint32_t i = 0u, s_ea = src_va, d_ea = dst_va; i < count;
       ++i, s_ea += in_stride, d_ea += out_stride) {
    const uint8_t* sp = GuestReadPtr(base, s_ea);
    const float x = LoadBeF32(sp + 0u);
    const float y = LoadBeF32(sp + 4u);
    const float z = LoadBeF32(sp + 8u);
    float32x4_t r = vfmaq_n_f32(vfmaq_n_f32(vfmaq_n_f32(row3, row2, z), row1, y), row0, x);
    r = vmulq_n_f32(r, 1.0f / vgetq_lane_f32(r, 3));
    uint8_t* dp = base + d_ea + CrtPhysOff(d_ea);
    StoreBeF32(dp + 0u, vgetq_lane_f32(r, 0));
    StoreBeF32(dp + 4u, vgetq_lane_f32(r, 1));
    StoreBeF32(dp + 8u, vgetq_lane_f32(r, 2));
  }
#else
  float m[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) m[i][j] = LoadBeF32(m_be + size_t(i * 4 + j) * 4u);
  }
  for (uint32_t i = 0u, s_ea = src_va, d_ea = dst_va; i < count;
       ++i, s_ea += in_stride, d_ea += out_stride) {
    const uint8_t* sp = GuestReadPtr(base, s_ea);
    const float x = LoadBeF32(sp + 0u), y = LoadBeF32(sp + 4u), z = LoadBeF32(sp + 8u);
    const float ox = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
    const float oy = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
    const float oz = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
    const float inv_w = 1.0f / (x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3]);
    uint8_t* dp = base + d_ea + CrtPhysOff(d_ea);
    StoreBeF32(dp + 0u, ox * inv_w);
    StoreBeF32(dp + 4u, oy * inv_w);
    StoreBeF32(dp + 8u, oz * inv_w);
  }
#endif
}

// Stage 8C native overrides (input deadzone + 46.7% active SIM hotspots)
REX_EXTERN(sub_8218CA80) {
  // SwitchInputDriver already applies a 15.3% radial deadzone; cap secondary axial
  // deadzone at 4% so short-throw Joy-Con sticks keep smooth horizontal modulation.
  const float dz = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.04f);
  const float v = std::clamp(static_cast<float>(ctx.f2.f64), -1.0f, 1.0f);
  float res = 0.0f;
  if (v > dz) {
    res = (v - dz) / (1.0f - dz);
  } else if (v < -dz) {
    res = (v + dz) / (1.0f - dz);
  }
  ctx.f1.f64 = static_cast<double>(res);
}

REX_EXTERN(sub_821F4168) {
  const uint32_t grid_va = ctx.r3.u32;
  const uint32_t cell_idx = ctx.r4.u32;
  const uint32_t out_list_va = ctx.r5.u32;
  if (!grid_va || !out_list_va) {
    __imp__sub_821F4168(ctx, base);
    return;
  }
  const uint32_t buckets_va = LoadBeU32(GuestReadPtr(base, grid_va + 64u));
  uint32_t node_va = LoadBeU32(GuestReadPtr(base, buckets_va + cell_idx * 12u));
  if (node_va == 0u) {
    ctx.r3.s64 = 1;
    return;
  }
  const uint32_t bitset_va = LoadBeU32(GuestReadPtr(base, grid_va + 76u));
  int32_t out_cnt = LoadBeS32(GuestReadPtr(base, out_list_va));
  const int32_t out_cap = LoadBeS32(GuestReadPtr(base, out_list_va + 24u));
  const uint32_t out_arr_va = LoadBeU32(GuestReadPtr(base, out_list_va + 4u));
  while (node_va != 0u) {
    const int32_t id = LoadBeS32(GuestReadPtr(base, node_va + 8u));
    const uint32_t word_va = bitset_va + static_cast<uint32_t>(id >> 5) * 4u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(id) & 31u);
    const uint32_t word = LoadBeU32(GuestReadPtr(base, word_va));
    if ((word & bit) == 0u) {
      if (out_cnt >= out_cap) {
        StoreBeU32(GuestWritePtr(base, out_list_va), static_cast<uint32_t>(out_cnt));
        ctx.r3.s64 = 0;
        return;
      }
      StoreBeU16(GuestWritePtr(base, out_arr_va + static_cast<uint32_t>(out_cnt) * 2u),
                 static_cast<uint16_t>(id));
      StoreBeU32(GuestWritePtr(base, word_va), word | bit);
      ++out_cnt;
    }
    node_va = LoadBeU32(GuestReadPtr(base, node_va + 4u));
  }
  StoreBeU32(GuestWritePtr(base, out_list_va), static_cast<uint32_t>(out_cnt));
  ctx.r3.s64 = 1;
}

REX_EXTERN(sub_821FD330) {
  const uint32_t stream_va = ctx.r3.u32;
  const uint32_t src_va = ctx.r4.u32;
  const uint32_t len = ctx.r5.u32;
  if (!stream_va || !src_va || LoadBeS32(GuestReadPtr(base, 0x82C2FD60u)) != 0) {
    __imp__sub_821FD330(ctx, base);
    return;
  }
  if (len == 0u) {
    return;
  }
  const uint32_t pos = LoadBeU32(GuestReadPtr(base, stream_va + 4u));
  const uint32_t cap = LoadBeU32(GuestReadPtr(base, stream_va + 40u));
  const uint32_t flags28 = LoadBeU32(GuestReadPtr(base, stream_va + 28u));
  if (static_cast<uint32_t>(pos + len) > cap && ((flags28 & 2u) != 0u)) {
    __imp__sub_821FD330(ctx, base);
    return;
  }
  const uint32_t dst_va = LoadBeU32(GuestReadPtr(base, stream_va + 36u));
  std::memmove(GuestWritePtr(base, dst_va), GuestReadPtr(base, src_va), len);
  StoreBeU32(GuestWritePtr(base, stream_va + 36u), dst_va + len);
}

REX_EXTERN(sub_82401388) {
  const float a = LoadBeF32(GuestReadPtr(base, ctx.r3.u32));
  const float b = LoadBeF32(GuestReadPtr(base, ctx.r4.u32));
  ctx.r3.s64 = (a > b) ? 1 : -1;
}

REX_EXTERN(sub_82401D20) {
  const uint32_t mgr_va = ctx.r3.u32;
  if (!mgr_va) {
    __imp__sub_82401D20(ctx, base);
    return;
  }
  const int32_t n_total = LoadBeS32(GuestReadPtr(base, mgr_va + 644u));
  const uint32_t init_slots_va = LoadBeU32(GuestReadPtr(base, mgr_va + 640u));
  const uint32_t init_arr_va = LoadBeU32(GuestReadPtr(base, mgr_va + 656u));
  const float zero_f = LoadBeF32(GuestReadPtr(base, 0x82000AA4u));

  struct Cand {
    float key;
    uint32_t raw_key;
    uint32_t idx;
  };
  Cand stack_cands[256];
  std::vector<Cand> heap_cands;
  uint32_t cand_cnt = 0u;
  int32_t active_cnt = 0;

  for (int32_t i = 0; i < n_total; ++i) {
    const uint8_t* slot = GuestReadPtr(base, init_slots_va + static_cast<uint32_t>(i) * 116u);
    const uint16_t ch_state = LoadBeU16(slot + 72u);
    if (ch_state < 32768u) {
      ++active_cnt;
    } else if (LoadBeF32(slot + 68u) > zero_f) {
      uint32_t raw_key = 0u;
      std::memcpy(&raw_key, slot + 100u, sizeof(raw_key));
      raw_key = __builtin_bswap32(raw_key);
      const Cand c{std::bit_cast<float>(raw_key), raw_key, static_cast<uint32_t>(i)};
      if (cand_cnt < 256u && heap_cands.empty()) {
        stack_cands[cand_cnt++] = c;
      } else {
        if (heap_cands.empty()) {
          heap_cands.assign(stack_cands, stack_cands + cand_cnt);
        }
        heap_cands.push_back(c);
        ++cand_cnt;
      }
    }
  }

  if (n_total == active_cnt) {
    return;
  }

  if (cand_cnt > 0u) {
    Cand* cands = heap_cands.empty() ? stack_cands : heap_cands.data();
    std::stable_sort(cands, cands + cand_cnt, [](const Cand& a, const Cand& b) noexcept {
      const bool a_nan = std::isnan(a.key);
      const bool b_nan = std::isnan(b.key);
      if (a_nan != b_nan) {
        return a_nan;
      }
      return a.key < b.key;
    });
    for (uint32_t k = 0u; k < cand_cnt; ++k) {
      uint8_t* entry = GuestWritePtr(base, init_arr_va + k * 8u);
      StoreBeU32(entry, cands[k].raw_key);
      StoreBeU32(entry + 4u, cands[k].idx);
    }
  }

  const uint32_t orig_r1 = ctx.r1.u32;
  const uint64_t orig_lr = ctx.lr;
  const uint32_t frame_r1 = orig_r1 - 96u;
  StoreBeU32(GuestWritePtr(base, frame_r1), orig_r1);
  ctx.r1.u32 = frame_r1;

  FastEnterSaberCsOrImp(ctx, base, 0x82B2B378u);

  for (uint32_t k = 0u; k < cand_cnt; ++k) {
    const uint32_t arr_va = LoadBeU32(GuestReadPtr(base, mgr_va + 656u));
    const uint32_t slots_va = LoadBeU32(GuestReadPtr(base, mgr_va + 640u));
    const int32_t n_ch = LoadBeS32(GuestReadPtr(base, mgr_va + 704u));
    const uint32_t cand_idx = LoadBeU32(GuestReadPtr(base, arr_va + k * 8u + 4u));
    const uint32_t cand_va = slots_va + cand_idx * 116u;
    const float f11 = LoadBeF32(GuestReadPtr(base, cand_va + 68u));
    float f12 = LoadBeF32(GuestReadPtr(base, cand_va + 100u));
    float f13 = f11;
    int32_t best_r5 = -1;
    int32_t best_r4 = -1;
    int32_t ch = 0;

    for (; ch < n_ch; ++ch) {
      const int32_t owner = LoadBeS32(GuestReadPtr(base, mgr_va + 712u + static_cast<uint32_t>(ch) * 12u));
      if (owner == -1) {
        break;
      }
      const uint8_t* slot_c = GuestReadPtr(base, slots_va + static_cast<uint32_t>(owner) * 116u);
      if ((slot_c[0] & 0x4u) != 0u) {
        break;
      }
      const float c100 = LoadBeF32(slot_c + 100u);
      const float c68 = LoadBeF32(slot_c + 68u);
      if (f12 < c100 && f11 > c68) {
        f12 = c100;
        best_r5 = ch;
      }
      if (f13 > c68) {
        f13 = c68;
        best_r4 = ch;
      }
    }

    if (ch == n_ch) {
      const int32_t pick = (best_r5 > 0) ? best_r5 : best_r4;
      if (pick < 0) {
        continue;
      }
      ch = pick;
    }

    const uint32_t ch_entry_va = mgr_va + 712u + static_cast<uint32_t>(ch) * 12u;
    const int32_t old_owner = LoadBeS32(GuestReadPtr(base, ch_entry_va));
    if (old_owner >= 0) {
      StoreBeU16(GuestWritePtr(base, slots_va + static_cast<uint32_t>(old_owner) * 116u + 72u), 0xFFFFu);
      StoreBeU32(GuestWritePtr(base, ch_entry_va), 0xFFFFFFFFu);
    }

    const uint32_t cur_arr_va = LoadBeU32(GuestReadPtr(base, mgr_va + 656u));
    const uint32_t cur_idx = LoadBeU32(GuestReadPtr(base, cur_arr_va + k * 8u + 4u));
    StoreBeU32(GuestWritePtr(base, ch_entry_va), cur_idx);
    StoreBeU16(GuestWritePtr(base, cand_va + 72u), static_cast<uint16_t>(ch));

    ctx.r3.u64 = mgr_va;
    ctx.r4.u64 = mgr_va + 708u + static_cast<uint32_t>(ch) * 12u;
    ctx.lr = 0x824020DCu;
    sub_82401888(ctx, base);
  }

  FastLeaveSaberCsOrImp(ctx, base, 0x82B2B378u);
  ctx.r1.u32 = orig_r1;
  ctx.lr = orig_lr;
}

REX_EXTERN(sub_82430090) {
  const uint32_t anim_va = ctx.r3.u32;
  const int32_t frame_idx = ctx.r4.s32;
  const uint32_t out_va = ctx.r5.u32;
  if (!anim_va || !out_va || frame_idx < 0) {
    __imp__sub_82430090(ctx, base);
    return;
  }
  const uint32_t hdr_va = LoadBeU32(GuestReadPtr(base, anim_va + 16u));
  const uint32_t n_ch = *GuestReadPtr(base, anim_va + 7u);
  const uint32_t step = hdr_va ? *GuestReadPtr(base, hdr_va + 2u) : 0u;
  if (!hdr_va || step == 0u || n_ch > 32u) {
    __imp__sub_82430090(ctx, base);
    return;
  }

  const uint32_t hdr_floats_va = hdr_va + 4u;
  const uint32_t block_idx = static_cast<uint32_t>(frame_idx / static_cast<int32_t>(step)) + 2u * n_ch;
  const uint32_t block_desc = LoadBeU32(GuestReadPtr(base, hdr_floats_va + block_idx * 4u));
  uint32_t stream_va = hdr_va + ((block_desc >> 8) & 0xFFFFFFu);
  const uint32_t mask_1byte = block_desc & 0xFu;
  const uint32_t mask_const_block = (block_desc >> 4) & 0xFu;
  const uint32_t mask_static = LoadBeU16(GuestReadPtr(base, hdr_va));

  const float f9 = LoadBeF32(GuestReadPtr(base, 0x82022428u));
  const float f10 = LoadBeF32(GuestReadPtr(base, 0x82022424u));
  const float f11 = LoadBeF32(GuestReadPtr(base, 0x82022420u));
  const float f12 = LoadBeF32(GuestReadPtr(base, 0x820039ACu));
  const uint8_t* lut = GuestReadPtr(base, 0x82B2B558u);
  auto decode16 = [&](uint16_t q) noexcept -> float {
    return std::fmaf(LoadBeF32(lut + static_cast<uint32_t>(q >> 8) * 4u), f12,
                     LoadBeF32(lut + static_cast<uint32_t>(q & 0xFFu) * 4u));
  };

  float out_vals[32]{};
  float scales[32]{};
  int32_t stride_bytes = 0;

  for (uint32_t ch = 0u; ch < n_ch; ++ch) {
    const uint32_t bit = 1u << ch;
    const float base_v = LoadBeF32(GuestReadPtr(base, hdr_floats_va + ch * 8u));
    if ((bit & mask_static) != 0u) {
      out_vals[ch] = base_v;
      continue;
    }
    const float range_v = LoadBeF32(GuestReadPtr(base, hdr_floats_va + ch * 8u + 4u));
    const uint16_t q0 = LoadBeU16(GuestReadPtr(base, stream_va));
    stream_va += 2u;
    out_vals[ch] = std::fmaf(decode16(q0) * range_v, f11, base_v);
    if ((bit & mask_const_block) == 0u) {
      const uint16_t q1 = LoadBeU16(GuestReadPtr(base, stream_va));
      stream_va += 2u;
      const float s = range_v * decode16(q1);
      if ((bit & mask_1byte) != 0u) {
        stride_bytes += 1;
        scales[ch] = s * f10;
      } else {
        stride_bytes += 2;
        scales[ch] = s * f9;
      }
    }
  }

  if (stride_bytes != 0) {
    const int32_t sub_frame = frame_idx % static_cast<int32_t>(step);
    stream_va += static_cast<uint32_t>(sub_frame * stride_bytes);
    const uint32_t skip_mask = mask_const_block | mask_static;
    for (uint32_t ch = 0u; ch < n_ch; ++ch) {
      const uint32_t bit = 1u << ch;
      if ((bit & skip_mask) != 0u) {
        continue;
      }
      float d = 0.0f;
      if ((bit & mask_1byte) != 0u) {
        const uint8_t b = *GuestReadPtr(base, stream_va++);
        d = LoadBeF32(lut + static_cast<uint32_t>(b) * 4u);
      } else {
        const uint16_t q = LoadBeU16(GuestReadPtr(base, stream_va));
        stream_va += 2u;
        d = decode16(q);
      }
      out_vals[ch] = std::fmaf(d, scales[ch], out_vals[ch]);
    }
  }

  uint8_t* dst = GuestWritePtr(base, out_va);
  for (uint32_t ch = 0u; ch < n_ch; ++ch) {
    StoreBeF32(dst + ch * 4u, out_vals[ch]);
  }
}

REX_EXTERN(sub_82436068) {
  const uint32_t iter_va = ctx.r3.u32;
  const uint32_t out_buf_va = ctx.r4.u32;
  const uint32_t out_count_va = ctx.r5.u32;
  const int32_t max_count = ctx.r6.s32;
  if (!iter_va || !out_buf_va || !out_count_va) {
    __imp__sub_82436068(ctx, base);
    return;
  }

  const uint8_t* it = GuestReadPtr(base, iter_va);
  const int32_t min_x = LoadBeS32(it + 48u);
  const int32_t min_y = LoadBeS32(it + 52u);
  const int32_t min_z = LoadBeS32(it + 56u);
  const int32_t max_x = LoadBeS32(it + 60u);
  const int32_t max_y = LoadBeS32(it + 64u);
  const int32_t max_z = LoadBeS32(it + 68u);
  const int32_t cur_min_x = LoadBeS32(it + 76u);
  const int32_t cur_min_y = LoadBeS32(it + 80u);
  const int32_t cur_min_z = LoadBeS32(it + 84u);
  const int32_t cur_max_x = LoadBeS32(it + 88u);
  const int32_t cur_max_y = LoadBeS32(it + 92u);
  const int32_t cur_max_z = LoadBeS32(it + 96u);

  if (cur_min_x < min_x && cur_min_y < min_y && cur_min_z < min_z &&
      cur_max_x > max_x && cur_max_y > max_y && cur_max_z > max_z) {
    ctx.r3.s64 = 0;
    return;
  }

  int32_t cnt = 0;
  int32_t dim_y = 0;
  int32_t dim_z = 0;
  bool dims_loaded = false;
  uint8_t* out_buf = GuestWritePtr(base, out_buf_va);

  auto emit = [&](int32_t x, int32_t y, int32_t z) noexcept -> bool {
    if (cnt >= max_count) {
      return false;
    }
    if (!dims_loaded) {
      const uint32_t grid_va = LoadBeU32(GuestReadPtr(base, iter_va + 8u));
      const uint8_t* gp = GuestReadPtr(base, grid_va);
      dim_y = LoadBeS32(gp + 4u);
      dim_z = LoadBeS32(gp + 8u);
      dims_loaded = true;
    }
    const int32_t idx = (dim_y * x + y) * dim_z + z;
    StoreBeU32(out_buf + static_cast<uint32_t>(cnt) * 4u, static_cast<uint32_t>(idx));
    ++cnt;
    return true;
  };

  const int32_t x0 = std::max(cur_min_x, min_x);
  const int32_t x1 = std::min(cur_max_x, max_x);
  const int32_t z0 = std::max(cur_min_z, min_z);
  const int32_t z1 = std::min(cur_max_z, max_z);
  const int32_t y0 = std::max(cur_min_y + 1, min_y);
  const int32_t y1 = std::min(cur_max_y - 1, max_y);
  const int32_t ix0 = std::max(cur_min_x + 1, min_x);
  const int32_t ix1 = std::min(cur_max_x - 1, max_x);

  if (cur_min_y >= min_y && cur_min_y <= max_y) {
    for (int64_t x = x0; x <= x1; ++x) {
      for (int64_t z = z0; z <= z1; ++z) {
        if (!emit(static_cast<int32_t>(x), cur_min_y, static_cast<int32_t>(z))) goto finish_step;
      }
    }
  }
  if (cur_max_y >= min_y && cur_max_y <= max_y && cur_min_y != cur_max_y) {
    for (int64_t x = x0; x <= x1; ++x) {
      for (int64_t z = z0; z <= z1; ++z) {
        if (!emit(static_cast<int32_t>(x), cur_max_y, static_cast<int32_t>(z))) goto finish_step;
      }
    }
  }
  if (cur_min_x >= min_x && cur_min_x <= max_x) {
    for (int64_t y = y0; y <= y1; ++y) {
      for (int64_t z = z0; z <= z1; ++z) {
        if (!emit(cur_min_x, static_cast<int32_t>(y), static_cast<int32_t>(z))) goto finish_step;
      }
    }
  }
  if (cur_max_x >= min_x && cur_max_x <= max_x && cur_min_x != cur_max_x) {
    for (int64_t y = y0; y <= y1; ++y) {
      for (int64_t z = z0; z <= z1; ++z) {
        if (!emit(cur_max_x, static_cast<int32_t>(y), static_cast<int32_t>(z))) goto finish_step;
      }
    }
  }
  if (cur_min_z >= min_z && cur_min_z <= max_z) {
    for (int64_t x = ix0; x <= ix1; ++x) {
      for (int64_t y = y0; y <= y1; ++y) {
        if (!emit(static_cast<int32_t>(x), static_cast<int32_t>(y), cur_min_z)) goto finish_step;
      }
    }
  }
  if (cur_max_z >= min_z && cur_max_z <= max_z && cur_min_z != cur_max_z) {
    for (int64_t x = ix0; x <= ix1; ++x) {
      for (int64_t y = y0; y <= y1; ++y) {
        if (!emit(static_cast<int32_t>(x), static_cast<int32_t>(y), cur_max_z)) goto finish_step;
      }
    }
  }

finish_step:
  StoreBeU32(GuestWritePtr(base, out_count_va), static_cast<uint32_t>(cnt));
  uint8_t* it_w = GuestWritePtr(base, iter_va);
  StoreBeF32(it_w + 12u, LoadBeF32(it_w + 12u) + LoadBeF32(it_w + 72u));
  StoreBeU32(it_w + 76u, static_cast<uint32_t>(cur_min_x - 1));
  StoreBeU32(it_w + 80u, static_cast<uint32_t>(cur_min_y - 1));
  StoreBeU32(it_w + 84u, static_cast<uint32_t>(cur_min_z - 1));
  StoreBeU32(it_w + 88u, static_cast<uint32_t>(cur_max_x + 1));
  StoreBeU32(it_w + 92u, static_cast<uint32_t>(cur_max_y + 1));
  StoreBeU32(it_w + 96u, static_cast<uint32_t>(cur_max_z + 1));
  ctx.r3.s64 = 1;
}

REX_EXTERN(sub_82290748) {
  const uint32_t zone_mgr_va = ctx.r3.u32;
  const uint32_t world_va = ctx.r4.u32;
  if (!zone_mgr_va || !world_va) {
    __imp__sub_82290748(ctx, base);
    return;
  }

  uint32_t zm160 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 160u)) & ~0x20u;
  StoreBeU32(GuestWritePtr(base, zone_mgr_va + 160u), zm160);
  if (LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)) == 0) {
    return;
  }

  zm160 &= ~0x10u;
  StoreBeU32(GuestWritePtr(base, zone_mgr_va + 160u), zm160);
  const bool force_refresh = (zm160 & 0x8u) != 0u;
  zm160 = force_refresh ? (zm160 | 0x40u) : (zm160 & ~0x40u);
  StoreBeU32(GuestWritePtr(base, zone_mgr_va + 160u), zm160);

  const uint32_t orig_r1 = ctx.r1.u32;
  const uint64_t orig_lr = ctx.lr;
  const uint32_t frame_r1 = orig_r1 - 128u;
  StoreBeU32(GuestWritePtr(base, frame_r1), orig_r1);
  ctx.r1.u32 = frame_r1;
  const uint32_t scratch_type_va = frame_r1 + 80u;
  const uint32_t scratch_pos_va = frame_r1 + 88u;

  const float f30 = LoadBeF32(GuestReadPtr(base, 0x82031B7Cu));
  const float f29 = LoadBeF32(GuestReadPtr(base, 0x82000AB4u));
  const float f31 = LoadBeF32(GuestReadPtr(base, 0x82001148u));
  const uint64_t want_mask = LoadBeU64(GuestReadPtr(base, 0x82C4B1B4u));

  // Loop 1: scan active slots [0..2699] across 85 32-bit bitmask words at [ent_mgr + 80].
  uint32_t ent_mgr_va = LoadBeU32(GuestReadPtr(base, 0x82C2F630u));
  for (uint32_t w = 0u; w < 85u; ++w) {
    uint32_t bits = LoadBeU32(GuestReadPtr(base, ent_mgr_va + 80u + w * 4u));
    if (w == 84u) {
      bits &= 0xFFFu;
    }
    while (bits != 0u) {
      const uint32_t b = static_cast<uint32_t>(__builtin_ctz(bits));
      bits &= (bits - 1u);
      const uint32_t slot_idx = w * 32u + b;
      const uint32_t ent_va = LoadBeU32(GuestReadPtr(base, ent_mgr_va + 10960u)) + slot_idx * 568u;
      uint32_t sub_obj = LoadBeU32(GuestReadPtr(base, ent_va + 16u));
      if (sub_obj == 0u) {
        continue;
      }
      const uint32_t f4 = LoadBeU32(GuestReadPtr(base, ent_va + 4u));
      if ((f4 & 0x800u) != 0u) {
        continue;
      }
      const bool is_alt = (f4 & 0x40u) != 0u;
      if (is_alt && (LoadBeU32(GuestReadPtr(base, ent_va + 8u)) & 0x20u) == 0u) {
        continue;
      }

      if (!is_alt) {
        const uint32_t cs_va = ent_va + 488u;
        FastEnterSaberCsOrImp(ctx, base, cs_va);
        std::memmove(GuestWritePtr(base, scratch_pos_va), GuestReadPtr(base, ent_va + 104u), 12u);
        FastLeaveSaberCsOrImp(ctx, base, cs_va);
      } else {
        ctx.r3.u64 = ent_va;
        ctx.r4.u64 = scratch_pos_va;
        ctx.lr = 0x8229089Cu;
        sub_8228ECF8(ctx, base);
      }

      sub_obj = LoadBeU32(GuestReadPtr(base, ent_va + 16u));
      if (sub_obj != 0u) {
        const uint32_t obj120 = LoadBeU32(GuestReadPtr(base, sub_obj + 120u));
        const uint32_t vtbl = LoadBeU32(GuestReadPtr(base, obj120));
        const uint32_t vfn = LoadBeU32(GuestReadPtr(base, vtbl + 24u));
        uint64_t type_mask = 0u;
        if (vfn == 0x8223A010u) {
          type_mask = LoadBeU64(GuestReadPtr(base, 0x82C412E4u));
        } else {
          PPCFunc* fn = rex::runtime::ResolveIndirectFunction(vfn);
          ctx.r3.u64 = scratch_type_va;
          ctx.r4.u64 = obj120;
          ctx.ctr.u64 = vfn;
          ctx.last_indirect_target = vfn;
          ctx.lr = 0x822908C8u;
          fn(ctx, base);
          type_mask = LoadBeU64(GuestReadPtr(base, scratch_type_va));
        }
        if ((type_mask & want_mask) == want_mask) {
          const uint32_t sub_obj2 = LoadBeU32(GuestReadPtr(base, ent_va + 16u));
          const uint32_t obj624 = LoadBeU32(GuestReadPtr(base, sub_obj2 + 624u));
          if (obj624 != 0u) {
            ctx.r3.u64 = obj624;
            ctx.r4.u64 = scratch_pos_va;
            ctx.lr = 0x822908F0u;
            sub_822FE0A0(ctx, base);
          }
        }
      }

      const float px = LoadBeF32(GuestReadPtr(base, scratch_pos_va + 0u));
      const float py = LoadBeF32(GuestReadPtr(base, scratch_pos_va + 4u));
      const float pz = LoadBeF32(GuestReadPtr(base, scratch_pos_va + 8u));
      if (!force_refresh) {
        const float dx = px - LoadBeF32(GuestReadPtr(base, ent_va + 20u));
        const float dz = pz - LoadBeF32(GuestReadPtr(base, ent_va + 28u));
        if (std::sqrt(dx * dx + dz * dz) < f31 &&
            std::fabs(py - LoadBeF32(GuestReadPtr(base, ent_va + 24u))) < f29) {
          continue;
        }
      }

      StoreBeF32(GuestWritePtr(base, ent_va + 20u), px);
      StoreBeF32(GuestWritePtr(base, ent_va + 24u), py);
      StoreBeF32(GuestWritePtr(base, ent_va + 28u), pz);
      StoreBeF32(GuestWritePtr(base, scratch_pos_va + 4u), py + f30);

      for (int32_t j = 0; j < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)); ++j) {
        const uint32_t arr164 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 164u));
        const uint32_t elem_va = LoadBeU32(GuestReadPtr(base, arr164 + static_cast<uint32_t>(j) * 4u));
        const uint32_t f224 = LoadBeU32(GuestReadPtr(base, elem_va + 224u)) & ~0x100u;
        StoreBeU32(GuestWritePtr(base, elem_va + 224u), f224);
        if ((f224 & (0x40u | 0x80u)) != 0u) {
          const uint32_t zobj = elem_va + 160u;
          const uint32_t zvtbl = LoadBeU32(GuestReadPtr(base, zobj));
          const uint32_t zvfn = LoadBeU32(GuestReadPtr(base, zvtbl + 12u));
          PPCFunc* fn = rex::runtime::ResolveIndirectFunction(zvfn);
          ctx.r3.u64 = zobj;
          ctx.r4.u64 = scratch_pos_va;
          ctx.r5.u64 = 0u;
          ctx.ctr.u64 = zvfn;
          ctx.last_indirect_target = zvfn;
          ctx.lr = 0x822909C0u;
          fn(ctx, base);
          if (ctx.r3.s32 != 0) {
            const uint32_t cur224 = LoadBeU32(GuestReadPtr(base, elem_va + 224u));
            StoreBeU32(GuestWritePtr(base, elem_va + 224u), cur224 | 0x100u);
          }
        }
      }

      StoreBeU32(GuestWritePtr(base, ent_va + 4u),
                 LoadBeU32(GuestReadPtr(base, ent_va + 4u)) | 0x2u);

      for (int32_t j = 0; j < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)); ++j) {
        const uint32_t arr164 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 164u));
        const uint32_t elem_va = LoadBeU32(GuestReadPtr(base, arr164 + static_cast<uint32_t>(j) * 4u));
        const uint32_t f224 = LoadBeU32(GuestReadPtr(base, elem_va + 224u));
        if ((f224 & 0x40u) != 0u && (f224 & 0x100u) != 0u) {
          StoreBeU32(GuestWritePtr(base, ent_va + 4u),
                     LoadBeU32(GuestReadPtr(base, ent_va + 4u)) & ~0x2u);
        }
      }
      for (int32_t j = 0; j < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)); ++j) {
        const uint32_t arr164 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 164u));
        const uint32_t elem_va = LoadBeU32(GuestReadPtr(base, arr164 + static_cast<uint32_t>(j) * 4u));
        const uint32_t f224 = LoadBeU32(GuestReadPtr(base, elem_va + 224u));
        if ((f224 & 0x80u) != 0u && (f224 & 0x100u) != 0u) {
          StoreBeU32(GuestWritePtr(base, ent_va + 4u),
                     LoadBeU32(GuestReadPtr(base, ent_va + 4u)) | 0x2u);
        }
      }

      ent_mgr_va = LoadBeU32(GuestReadPtr(base, 0x82C2F630u));
      if (b < 31u) {
        uint32_t refreshed = LoadBeU32(GuestReadPtr(base, ent_mgr_va + 80u + w * 4u));
        refreshed &= (~0u << (b + 1u));
        if (w == 84u) {
          refreshed &= 0xFFFu;
        }
        bits = refreshed;
      } else {
        bits = 0u;
      }
    }
  }

  if (force_refresh) {
    uint32_t f160 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 160u));
    f160 = (f160 & ~0x8u) | 0x10u;
    StoreBeU32(GuestWritePtr(base, zone_mgr_va + 160u), f160);

    for (int32_t k = 0; k < LoadBeS32(GuestReadPtr(base, world_va + 10952u)); ++k) {
      const uint32_t arr = LoadBeU32(GuestReadPtr(base, world_va + 11008u));
      const int32_t sk = static_cast<int32_t>(static_cast<int16_t>(k));
      const uint32_t ptr = LoadBeU32(GuestReadPtr(base, arr + static_cast<uint32_t>(sk) * 4u));
      if (ptr != 0u) {
        const uint32_t f4 = LoadBeU32(GuestReadPtr(base, ptr + 4u));
        if ((f4 & 0x20000u) == 0u) {
          const uint32_t nf4 = (LoadBeS32(GuestReadPtr(base, ptr + 20u)) > 0) ? (f4 | 0x20u) : (f4 & ~0x20u);
          StoreBeU32(GuestWritePtr(base, ptr + 4u), nf4);
        }
      }
    }

    for (int32_t k = 0; k < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 232u)); ++k) {
      const uint32_t arr = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 228u));
      const uint32_t ptr = LoadBeU32(GuestReadPtr(base, arr + static_cast<uint32_t>(k) * 4u));
      StoreBeU32(GuestWritePtr(base, ptr + 4u), LoadBeU32(GuestReadPtr(base, ptr + 4u)) | 0x20u);
    }

    ent_mgr_va = LoadBeU32(GuestReadPtr(base, 0x82C2F630u));
    const uint32_t ents_base = LoadBeU32(GuestReadPtr(base, ent_mgr_va + 10960u));
    for (uint32_t w = 0u; w < 85u; ++w) {
      uint32_t bits = LoadBeU32(GuestReadPtr(base, ent_mgr_va + 80u + w * 4u));
      if (w == 84u) {
        bits &= 0xFFFu;
      }
      while (bits != 0u) {
        const uint32_t b = static_cast<uint32_t>(__builtin_ctz(bits));
        bits &= (bits - 1u);
        const uint32_t ent_va = ents_base + (w * 32u + b) * 568u;
        const uint32_t f4 = LoadBeU32(GuestReadPtr(base, ent_va + 4u));
        if ((f4 & 0x800u) == 0u && (f4 & 0x40u) != 0u &&
            (LoadBeU32(GuestReadPtr(base, ent_va + 8u)) & 0x20u) == 0u) {
          StoreBeU32(GuestWritePtr(base, ent_va + 4u), f4 | 0x2u);
        }
      }
    }

    for (int32_t j = 0; j < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)); ++j) {
      const uint32_t arr164 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 164u));
      const uint32_t elem_va = LoadBeU32(GuestReadPtr(base, arr164 + static_cast<uint32_t>(j) * 4u));
      if ((LoadBeU32(GuestReadPtr(base, elem_va + 224u)) & 0x40u) != 0u) {
        ctx.r3.u64 = elem_va;
        ctx.r4.s64 = 1;
        ctx.lr = 0x82290E4Cu;
        sub_8228F2E0(ctx, base);
      }
    }
    for (int32_t j = 0; j < LoadBeS32(GuestReadPtr(base, zone_mgr_va + 168u)); ++j) {
      const uint32_t arr164 = LoadBeU32(GuestReadPtr(base, zone_mgr_va + 164u));
      const uint32_t elem_va = LoadBeU32(GuestReadPtr(base, arr164 + static_cast<uint32_t>(j) * 4u));
      if ((LoadBeU32(GuestReadPtr(base, elem_va + 224u)) & 0x80u) != 0u) {
        ctx.r3.u64 = elem_va;
        ctx.r4.s64 = 0;
        ctx.lr = 0x82290E94u;
        sub_8228F2E0(ctx, base);
      }
    }

    ctx.r3.u64 = LoadBeU32(GuestReadPtr(base, world_va + 10948u));
    ctx.lr = 0x82290EB0u;
    sub_8228FF80(ctx, base);

    ctx.r3.s64 = 1030;
    ctx.r4.s64 = 0;
    ctx.r5.u64 = zone_mgr_va;
    ctx.lr = 0x82290EC0u;
    sub_821F0168(ctx, base);

    ctx.r3.u64 = LoadBeU32(GuestReadPtr(base, 0x82C2C22Cu));
    ctx.r4.u64 = LoadBeU32(GuestReadPtr(base, 0x82C2F630u));
    ctx.lr = 0x82290ED0u;
    sub_821E1210(ctx, base);
  }

  ctx.r1.u32 = orig_r1;
  ctx.lr = orig_lr;
}
