#include "shader_resource_hooks.h"
#include "shader_runtime_bridge.h"
#include "device_registry.h"
#include "render_drop_telemetry.h"
#include <rex/ppc.h>

REX_EXTERN(__imp__sub_8263F7C8);
REX_EXTERN(__imp__sub_82640FF0);
REX_EXTERN(__imp__sub_8263FB98);
REX_EXTERN(__imp__sub_8264D7C0);

namespace {
bool Owned(uint32_t device) {
  return timeshift::renderer::ResourceHooksEnabled() &&
         timeshift::renderer::DeviceLifecycleHooksEnabled() &&
         timeshift::renderer::DeviceRegistryRuntime().Contains(device);
}

uint32_t Word(uint8_t *base, uint32_t address) {
  if (!base) {
    timeshift::telemetry::g_drop_texture_null_base.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  size_t at = address;
#if REX_PLATFORM_SWITCH || REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))
  if (address >= 0xE0000000u) at += 0x1000;
#endif
  return uint32_t(base[at]) << 24 | uint32_t(base[at + 1]) << 16 |
         uint32_t(base[at + 2]) << 8 | base[at + 3];
}

uint32_t GlobalDevice(uint8_t *base) {
  if (!base) return 0;
  constexpr uint32_t kGlobalDeviceImport = 0x82000914;
  const uint32_t slot = Word(base, kGlobalDeviceImport);
  if (!slot) return 0;
  return Word(base, slot);
}
}  // namespace

// Use the original title functions in both modes. These hooks add host-side
// registration only; they never manufacture a foreign XDK shader object.
REX_EXTERN(sub_8263F7C8) {
  try {
    const uint32_t shader = ctx.r3.u32, physical = ctx.r4.u32;
    const bool active = timeshift::renderer::ResourceHooksEnabled() &&
                        timeshift::renderer::DeviceLifecycleHooksEnabled();
    __imp__sub_8263F7C8(ctx, base);
    if (active) timeshift::renderer::AfterVertexRegistration(shader, physical);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[shader_resource] sub_8263F7C8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}

REX_EXTERN(sub_82640FF0) {
  try {
    const bool active = timeshift::renderer::ResourceHooksEnabled() &&
                        timeshift::renderer::DeviceLifecycleHooksEnabled();
    __imp__sub_82640FF0(ctx, base);
    if (active && ctx.r3.u32) timeshift::renderer::AfterPixelCreation(ctx.r3.u32);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[shader_resource] sub_82640FF0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}

REX_EXTERN(sub_8263FB98) {
  try {
    const uint32_t elements = ctx.r3.u32;
    const bool active = timeshift::renderer::ResourceHooksEnabled() &&
                        timeshift::renderer::DeviceLifecycleHooksEnabled();
    __imp__sub_8263FB98(ctx, base);
    if (active && ctx.r3.u32) timeshift::renderer::AfterDeclarationCreation(ctx.r3.u32, elements);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[shader_resource] sub_8263FB98 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}

REX_EXTERN(sub_8264D7C0) {
  try {
    const uint32_t resource = ctx.r3.u32;
    const uint32_t device = GlobalDevice(base);
    if (device && Owned(device)) {
      auto lease = timeshift::renderer::DeviceRegistryRuntime().Acquire(device, /*continuation=*/true);
      if (lease) {
        timeshift::renderer::BeforeResourceDestroy(resource);
      }
      __imp__sub_8264D7C0(ctx, base);
    } else {
      __imp__sub_8264D7C0(ctx, base);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[shader_resource] sub_8264D7C0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    __imp__sub_8264D7C0(ctx, base);
  }
}
