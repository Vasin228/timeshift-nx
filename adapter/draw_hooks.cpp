#include "draw_hooks.h"
#include "draw_runtime_bridge.h"
#include "draw_abi.h"
#include "device_registry.h"
#include "render_drop_telemetry.h"
#include "perf_breakdown.h"
#include <rex/cvar.h>
#include <rex/ppc.h>
#include <cstdio>
#include <exception>

REXCVAR_DEFINE_BOOL(renut_skip_orig_d3d, true, "Renut",
                    "Skip redundant guest D3D ring-buffer packet writes in DrawIndexed/DrawUP");

REX_EXTERN(__imp__sub_826505F0);
REX_EXTERN(__imp__sub_8264FFD8);
REX_EXTERN(__imp__sub_82650550);
REX_EXTERN(__imp__sub_8264FB38);
REX_EXTERN(__imp__sub_82650020);
REX_EXTERN(__imp__sub_8264A810);
REX_EXTERN(__imp__sub_8264C8E8);
REX_EXTERN(__imp__sub_82655E00);

namespace {
using namespace timeshift::renderer::abi;

bool HooksActive() {
  return timeshift::renderer::DrawHooksEnabled() &&
         timeshift::renderer::DeviceLifecycleHooksEnabled();
}

// The two UP wrappers call Begin* internally. Their input is already complete
// and copied by the outer hook, so the nested Begin must not queue it again.
struct UpResult {
  bool succeeded = false;
};
thread_local UpResult *current_up = nullptr;
struct UpScope {
  UpResult result;
  UpResult *previous = current_up;
  UpScope() { current_up = &result; }
  ~UpScope() { current_up = previous; }
};

uint32_t Word(uint8_t *base, uint32_t address) {
  size_t at = address;
#if REX_PLATFORM_SWITCH || REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))
  // Match the frozen generated PCH's REX_PHYS_HOST_OFFSET for out pointers.
  if (address >= 0xE0000000u) at += 0x1000;
#endif
  return uint32_t(base[at]) << 24 | uint32_t(base[at + 1]) << 16 |
         uint32_t(base[at + 2]) << 8 | base[at + 3];
}

void PutWord(uint8_t *base, uint32_t address, uint32_t value) {
  if (!address) {
    timeshift::telemetry::g_drop_draw_null_address.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  size_t at = address;
#if REX_PLATFORM_SWITCH || REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))
  if (address >= 0xE0000000u) at += 0x1000;
#endif
  base[at] = uint8_t(value >> 24);
  base[at + 1] = uint8_t(value >> 16);
  base[at + 2] = uint8_t(value >> 8);
  base[at + 3] = uint8_t(value);
}
}  // namespace

namespace timeshift::renderer {
bool IsInsideUpScope() { return current_up != nullptr; }
}

REX_EXTERN(sub_826505F0) {
  try {
    const uint64_t t0 = timeshift::telemetry::NowTicks();
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_826505F0(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, current_up != nullptr, owned);
    if (!owned) {
      __imp__sub_826505F0(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::DrawIndexed(device, ctx.r4.u32, ctx.r5.s32, ctx.r6.u32, ctx.r7.u32);
    const uint64_t p2 = timeshift::renderer::ConsumeImmediatePending(device);
    const uint64_t t1 = timeshift::telemetry::NowTicks();
    timeshift::telemetry::g_ticks_draw_adapter.fetch_add(t1 > t0 ? (t1 - t0) : 0, std::memory_order_relaxed);
    static const bool skip_orig = REXCVAR_GET(renut_skip_orig_d3d);
    if (!skip_orig && (p2 & 0x1E0000ULL) != 0) {
      const uint64_t saved_lr = ctx.lr;
      ctx.r3.u64 = device;
      ctx.r4.u64 = p2;
      __imp__sub_82655E00(ctx, base);
      ctx.lr = saved_lr;
      const uint64_t t2 = timeshift::telemetry::NowTicks();
      const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
      timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_ticks_didx.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_cnt_didx.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_826505F0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264FFD8) {
  try {
    const bool outer = (current_up == nullptr);
    const uint64_t t0 = outer ? timeshift::telemetry::NowTicks() : 0;
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_8264FFD8(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, current_up != nullptr, owned);
    if (!owned) {
      __imp__sub_8264FFD8(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    const auto primitive = ctx.r4.u32, count = ctx.r5.u32, data = ctx.r6.u32, stride = ctx.r7.u32;
    timeshift::renderer::FlushPendingDraw(device);
    static const bool skip_orig = REXCVAR_GET(renut_skip_orig_d3d);
    if (skip_orig) {
      if (count > 0 && data != 0 && stride > 0) {
        timeshift::renderer::DrawVerticesUP(device, primitive, count, data, stride);
        timeshift::renderer::ConsumeImmediatePending(device);
      }
      return;
    }
    const uint64_t t1 = outer ? timeshift::telemetry::NowTicks() : 0;
    UpScope scope;
    __imp__sub_8264FFD8(ctx, base);
    const uint64_t t2 = outer ? timeshift::telemetry::NowTicks() : 0;
    if (scope.result.succeeded) {
      timeshift::renderer::DrawVerticesUP(device, primitive, count, data, stride);
    }
    if (outer) {
      const uint64_t t3 = timeshift::telemetry::NowTicks();
      const uint64_t adp = (t1 > t0 ? (t1 - t0) : 0) + (t3 > t2 ? (t3 - t2) : 0);
      const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
      timeshift::telemetry::g_ticks_draw_adapter.fetch_add(adp, std::memory_order_relaxed);
      timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_ticks_up.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_cnt_up.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264FFD8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_82650550) {
  try {
    const bool outer = (current_up == nullptr);
    const uint64_t t0 = outer ? timeshift::telemetry::NowTicks() : 0;
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_82650550(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, current_up != nullptr, owned);
    if (!owned) {
      __imp__sub_82650550(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    const auto primitive = ctx.r4.u32, minimum = ctx.r5.u32, vertices = ctx.r6.u32, count = ctx.r7.u32;
    const auto indices = ctx.r8.u32, format = ctx.r9.u32, data = ctx.r10.u32;
    const auto stride = Word(base, ctx.r1.u32 + inline_stack_argument);
    timeshift::renderer::FlushPendingDraw(device);
    static const bool skip_orig = REXCVAR_GET(renut_skip_orig_d3d);
    if (skip_orig) {
      if (count > 0 && vertices > 0 && data != 0 && indices != 0 && stride > 0) {
        timeshift::renderer::DrawIndexedUP(device, primitive, minimum, vertices, count, indices, format, data, stride);
        timeshift::renderer::ConsumeImmediatePending(device);
      }
      return;
    }
    const uint64_t t1 = outer ? timeshift::telemetry::NowTicks() : 0;
    UpScope scope;
    __imp__sub_82650550(ctx, base);
    const uint64_t t2 = outer ? timeshift::telemetry::NowTicks() : 0;
    if (scope.result.succeeded) {
      timeshift::renderer::DrawIndexedUP(device, primitive, minimum, vertices, count, indices, format, data, stride);
    }
    if (outer) {
      const uint64_t t3 = timeshift::telemetry::NowTicks();
      const uint64_t adp = (t1 > t0 ? (t1 - t0) : 0) + (t3 > t2 ? (t3 - t2) : 0);
      const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
      timeshift::telemetry::g_ticks_draw_adapter.fetch_add(adp, std::memory_order_relaxed);
      timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_ticks_up.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_cnt_up.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_82650550 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264FB38) {
  try {
    const bool is_nested = (current_up != nullptr);
    const uint64_t t0 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_8264FB38(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, is_nested, owned);
    if (!owned) {
      __imp__sub_8264FB38(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0;
      if (current_up) current_up->succeeded = false;
      return;
    }
    const auto primitive = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
    if (!is_nested) timeshift::renderer::FlushPendingDraw(device);
    const uint64_t t1 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    __imp__sub_8264FB38(ctx, base);
    const uint64_t t2 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    if (current_up) current_up->succeeded = (ctx.r3.u32 != 0);
    if (!is_nested && ctx.r3.u32) {
      timeshift::renderer::BeginVerticesCompleted(device, primitive, count, stride, ctx.r3.u32);
    }
    if (!is_nested) {
      const uint64_t t3 = timeshift::telemetry::NowTicks();
      const uint64_t adp = (t1 > t0 ? (t1 - t0) : 0) + (t3 > t2 ? (t3 - t2) : 0);
      const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
      timeshift::telemetry::g_ticks_draw_adapter.fetch_add(adp, std::memory_order_relaxed);
      timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_ticks_up.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_cnt_up.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264FB38 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0;
  }
}

REX_EXTERN(sub_82650020) {
  try {
    const bool is_nested = (current_up != nullptr);
    const uint64_t t0 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_82650020(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, is_nested, owned);
    if (!owned) {
      __imp__sub_82650020(ctx, base);
      return;
    }
    const auto out_indices = ctx.r10.u32;
    const auto out_vertices = Word(base, ctx.r1.u32 + inline_stack_argument);
    if (!lease) {
      PutWord(base, out_indices, 0);
      PutWord(base, out_vertices, 0);
      ctx.r3.u64 = 0x80004005u;
      if (current_up) current_up->succeeded = false;
      return;
    }
    const auto primitive = ctx.r4.u32, vertices = ctx.r6.u32, count = ctx.r7.u32;
    const int32_t vertex_base = ctx.r5.s32;
    const auto format = ctx.r8.u32, stride = ctx.r9.u32;
    if (!is_nested) timeshift::renderer::FlushPendingDraw(device);
    const uint64_t t1 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    __imp__sub_82650020(ctx, base);
    const uint64_t t2 = !is_nested ? timeshift::telemetry::NowTicks() : 0;
    if (current_up) current_up->succeeded = (ctx.r3.u32 == 0);
    if (!is_nested && ctx.r3.u32 == 0) {
      timeshift::renderer::BeginIndexedCompleted(
          device, primitive, vertex_base, vertices, count, format, stride,
          Word(base, out_vertices), Word(base, out_indices));
    }
    if (!is_nested) {
      const uint64_t t3 = timeshift::telemetry::NowTicks();
      const uint64_t adp = (t1 > t0 ? (t1 - t0) : 0) + (t3 > t2 ? (t3 - t2) : 0);
      const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
      timeshift::telemetry::g_ticks_draw_adapter.fetch_add(adp, std::memory_order_relaxed);
      timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_ticks_up.fetch_add(dt, std::memory_order_relaxed);
      timeshift::telemetry::g_orig_cnt_up.fetch_add(1, std::memory_order_relaxed);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_82650020 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264A810) {
  try {
    const uint64_t t0 = timeshift::telemetry::NowTicks();
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_8264A810(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, current_up != nullptr, owned);
    if (!owned) {
      __imp__sub_8264A810(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::ClearFrame(device, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, float(ctx.f1.f64), ctx.r8.u32);
    const uint64_t t1 = timeshift::telemetry::NowTicks();
    timeshift::telemetry::g_ticks_draw_adapter.fetch_add(t1 > t0 ? (t1 - t0) : 0, std::memory_order_relaxed);
    __imp__sub_8264A810(ctx, base);
    const uint64_t t2 = timeshift::telemetry::NowTicks();
    const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
    timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
    timeshift::telemetry::g_orig_ticks_clr.fetch_add(dt, std::memory_order_relaxed);
    timeshift::telemetry::g_orig_cnt_clr.fetch_add(1, std::memory_order_relaxed);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264A810 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264C8E8) {
  try {
    const uint64_t t0 = timeshift::telemetry::NowTicks();
    const auto device = ctx.r3.u32;
    if (!HooksActive()) {
      __imp__sub_8264C8E8(ctx, base);
      return;
    }
    bool owned = false;
    auto lease = timeshift::renderer::DeviceRegistryRuntime().AcquireIfOwned(device, current_up != nullptr, owned);
    if (!owned) {
      __imp__sub_8264C8E8(ctx, base);
      return;
    }
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::ResolveFrame(
        device, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
        ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, float(ctx.f1.f64),
        Word(base, ctx.r1.u32 + resolve_stencil), Word(base, ctx.r1.u32 + resolve_parameters));
    const uint64_t t1 = timeshift::telemetry::NowTicks();
    timeshift::telemetry::g_ticks_draw_adapter.fetch_add(t1 > t0 ? (t1 - t0) : 0, std::memory_order_relaxed);
    __imp__sub_8264C8E8(ctx, base);
    const uint64_t t2 = timeshift::telemetry::NowTicks();
    const uint64_t dt = t2 > t1 ? (t2 - t1) : 0;
    timeshift::telemetry::g_ticks_orig_d3d.fetch_add(dt, std::memory_order_relaxed);
    timeshift::telemetry::g_orig_ticks_res.fetch_add(dt, std::memory_order_relaxed);
    timeshift::telemetry::g_orig_cnt_res.fetch_add(1, std::memory_order_relaxed);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264C8E8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}
