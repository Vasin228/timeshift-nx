#include "callback_hooks.h"
#include "device_registry.h"
#include "renut_engine/gpu/guest/callbacks.h"
#include <rex/ppc.h>
#include <cstdio>
#include <stdexcept>

REX_EXTERN(__imp__sub_8264F6B8);

namespace timeshift::renderer {
namespace {
constexpr uint32_t kLegacyWorker = 0x82652698;

uint32_t Word(std::span<const uint8_t> p, uint32_t at = 0) {
  if (uint64_t(at) + 4 > p.size()) throw std::out_of_range("callback hook guest word");
  return uint32_t(p[at]) << 24 | uint32_t(p[at + 1]) << 16 | uint32_t(p[at + 2]) << 8 | p[at + 3];
}

bool Owned(uint32_t device) {
  return DeviceLifecycleHooksEnabled() && DeviceRegistryRuntime().Contains(device);
}
}  // namespace

void NativeInsertCallback(PPCContext &ctx, uint8_t *base) {
  if (!Owned(ctx.r3.u32)) {
    __imp__sub_8264F6B8(ctx, base);
    return;
  }
  auto &registry = DeviceRegistryRuntime();
  auto lease = registry.Acquire(ctx.r3.u32, true);
  if (!lease) {
    __imp__sub_8264F6B8(ctx, base);
    return;
  }

  const uint32_t callback = ctx.r5.u32;
  // Suppress legacy worker 82652698 completely:
  // Never emit PM4 packets, never call sub_8264EE08, never schedule legacy worker.
  if (callback == kLegacyWorker) {
    const auto p = registry.Memory().Map(ctx.r3.u32, 22400);
    if (!p.empty()) {
      ctx.r3.u64 = Word(p, 48);
    }
    return;
  }

  // Valid guest callback:
  // 1. Flush deferred sources (e.g. vertex fills, linear textures).
  lease->FlushSources(ctx.r3.u32);

  // 2. Queue into ReNut callback queue with sequence markers.
  renut::gpu::InsertCallback(ctx.r5.u32, ctx.r6.u32);

  // 3. Return preserved ring cursor bounds matching guest ABI.
  const auto p = registry.Memory().Map(ctx.r3.u32, 22400);
  if (!p.empty()) {
    ctx.r3.u64 = Word(p, 48);
  }
}

}  // namespace timeshift::renderer

REX_EXTERN(sub_8264F6B8) {
  try {
    timeshift::renderer::NativeInsertCallback(ctx, base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[callback] NativeInsertCallback EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}
