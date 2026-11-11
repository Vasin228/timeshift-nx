#include "presentation_hooks.h"
#include "device_registry.h"
#include "draw_runtime_bridge.h"
#include "shader_runtime_bridge.h"
#include "timeshift_device_layout.h"
#include "render_drop_telemetry.h"

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/backend/memory_report.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/render/surfaces.h"
#include "renut_engine/gpu/render/pipeline.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/callbacks.h"

#include <rex/ppc.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>

REX_EXTERN(__imp__sub_8264B1A8);
REX_EXTERN(__imp__sub_8264AD00);

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
    while (swap_count_val > Word(dev_span, kPresentRetiredCount) + kMaxFramesInFlight) {
      if (!renut::gpu::render_thread::WaitForFenceProgress(std::chrono::milliseconds(2))) {
        break;
      }
      registry.Memory().RunCallbacks();
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
