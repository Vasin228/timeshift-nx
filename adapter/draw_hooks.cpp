#include "draw_hooks.h"
#include "draw_runtime_bridge.h"
#include "draw_abi.h"
#include "device_registry.h"
#include "render_drop_telemetry.h"
#include <rex/ppc.h>
#include <cstdio>
#include <exception>

REX_EXTERN(__imp__sub_826505F0);
REX_EXTERN(__imp__sub_8264FFD8);
REX_EXTERN(__imp__sub_82650550);
REX_EXTERN(__imp__sub_8264FB38);
REX_EXTERN(__imp__sub_82650020);
REX_EXTERN(__imp__sub_8264A810);
REX_EXTERN(__imp__sub_8264C8E8);

namespace {
using namespace timeshift::renderer::abi;

bool Owned(uint32_t device) {
  return timeshift::renderer::DrawHooksEnabled() &&
         timeshift::renderer::DeviceLifecycleHooksEnabled() &&
         timeshift::renderer::DeviceRegistryRuntime().Contains(device);
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
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_826505F0(ctx, base);
      return;
    }
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, current_up != nullptr);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::DrawIndexed(device, ctx.r4.u32, ctx.r5.s32, ctx.r6.u32, ctx.r7.u32);
    __imp__sub_826505F0(ctx, base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_826505F0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264FFD8) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_8264FFD8(ctx, base);
      return;
    }
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, current_up != nullptr);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    const auto primitive = ctx.r4.u32, count = ctx.r5.u32, data = ctx.r6.u32, stride = ctx.r7.u32;
    timeshift::renderer::FlushPendingDraw(device);
    UpScope scope;
    __imp__sub_8264FFD8(ctx, base);
    if (scope.result.succeeded) {
      timeshift::renderer::DrawVerticesUP(device, primitive, count, data, stride);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264FFD8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_82650550) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_82650550(ctx, base);
      return;
    }
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, current_up != nullptr);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    const auto primitive = ctx.r4.u32, minimum = ctx.r5.u32, vertices = ctx.r6.u32, count = ctx.r7.u32;
    const auto indices = ctx.r8.u32, format = ctx.r9.u32, data = ctx.r10.u32;
    const auto stride = Word(base, ctx.r1.u32 + inline_stack_argument);
    timeshift::renderer::FlushPendingDraw(device);
    UpScope scope;
    __imp__sub_82650550(ctx, base);
    if (scope.result.succeeded) {
      timeshift::renderer::DrawIndexedUP(device, primitive, minimum, vertices, count, indices, format, data, stride);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_82650550 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264FB38) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_8264FB38(ctx, base);
      return;
    }
    const bool is_nested = (current_up != nullptr);
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, is_nested);
    if (!lease) {
      ctx.r3.u64 = 0;
      if (current_up) current_up->succeeded = false;
      return;
    }
    const auto primitive = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
    if (!is_nested) timeshift::renderer::FlushPendingDraw(device);
    __imp__sub_8264FB38(ctx, base);
    if (current_up) current_up->succeeded = (ctx.r3.u32 != 0);
    if (!is_nested && ctx.r3.u32) {
      timeshift::renderer::BeginVerticesCompleted(device, primitive, count, stride, ctx.r3.u32);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264FB38 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0;
  }
}

REX_EXTERN(sub_82650020) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_82650020(ctx, base);
      return;
    }
    const bool is_nested = (current_up != nullptr);
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, is_nested);
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
    __imp__sub_82650020(ctx, base);
    if (current_up) current_up->succeeded = (ctx.r3.u32 == 0);
    if (!is_nested && ctx.r3.u32 == 0) {
      timeshift::renderer::BeginIndexedCompleted(
          device, primitive, vertex_base, vertices, count, format, stride,
          Word(base, out_vertices), Word(base, out_indices));
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_82650020 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264A810) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_8264A810(ctx, base);
      return;
    }
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, current_up != nullptr);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::ClearFrame(device, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, float(ctx.f1.f64), ctx.r8.u32);
    __imp__sub_8264A810(ctx, base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264A810 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}

REX_EXTERN(sub_8264C8E8) {
  try {
    const auto device = ctx.r3.u32;
    if (!Owned(device)) {
      __imp__sub_8264C8E8(ctx, base);
      return;
    }
    auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, current_up != nullptr);
    if (!lease) {
      ctx.r3.u64 = 0x80004005u;
      return;
    }
    timeshift::renderer::ResolveFrame(
        device, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
        ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, float(ctx.f1.f64),
        Word(base, ctx.r1.u32 + resolve_stencil), Word(base, ctx.r1.u32 + resolve_parameters));
    __imp__sub_8264C8E8(ctx, base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[draw] sub_8264C8E8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}
