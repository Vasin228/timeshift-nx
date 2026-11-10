#include "device_registry.h"
#include <rex/ppc.h>
#include <cstdio>
#include <exception>

REX_EXTERN(__imp__sub_826478E0);
REX_EXTERN(__imp__sub_82647600);
REX_EXTERN(__imp__sub_8264E6F0);
REX_EXTERN(__imp__sub_8264F3B8);
REX_EXTERN(__imp__sub_8264F5D8);
namespace {
bool Owned(uint32_t device){return timeshift::renderer::DeviceLifecycleHooksEnabled()&&timeshift::renderer::DeviceRegistryRuntime().Contains(device);}
}
REX_EXTERN(sub_826478E0){
  std::fprintf(stderr, "[device] sub_826478E0 (CreateDevice) enter: adapter=%u, type=%u, flags=0x%08X, out_device=0x%08X\n",
               ctx.r3.u32, ctx.r4.u32, ctx.r6.u32, ctx.r8.u32);
  std::fflush(stderr);
  try {
    if(timeshift::renderer::DeviceLifecycleHooksEnabled()){
      auto &registry=timeshift::renderer::DeviceRegistryRuntime();
      // Safe boundary: this API returns a new device, never an old ring/inline
      // pointer that deferred retirement could invalidate before the caller writes.
      registry.RetryPending(ctx,base);
      const uint32_t create_res = registry.Create(ctx,base);
      registry.RetryPending(ctx,base);
      ctx.r3.u64 = create_res;
      std::fprintf(stderr, "[device] sub_826478E0 exit: result=0x%08X, r3=0x%08X\n", create_res, (uint32_t)ctx.r3.u32);
      std::fflush(stderr);
    }
    else {
      __imp__sub_826478E0(ctx,base);
      std::fprintf(stderr, "[device] sub_826478E0 (unhooked) exit: r3=0x%08X\n", (uint32_t)ctx.r3.u32);
      std::fflush(stderr);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[device] sub_826478E0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0x80004005u;
  }
}
REX_EXTERN(sub_82647600){
  try {
    if(Owned(ctx.r3.u32))ctx.r3.u64=timeshift::renderer::DeviceRegistryRuntime().Release(ctx,base,ctx.r3.u32);
    else __imp__sub_82647600(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[device] sub_82647600 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 1;
  }
}
REX_EXTERN(sub_8264E6F0){
  try {
    if(Owned(ctx.r3.u32))ctx.r3.u64=timeshift::renderer::DeviceRegistryRuntime().AllocateInline(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32);
    else __imp__sub_8264E6F0(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[device] sub_8264E6F0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0;
  }
}
REX_EXTERN(sub_8264F3B8){
  try {
    if(Owned(ctx.r3.u32))ctx.r3.u64=timeshift::renderer::DeviceRegistryRuntime().Kick(ctx.r3.u32);
    else __imp__sub_8264F3B8(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[device] sub_8264F3B8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0;
  }
}
REX_EXTERN(sub_8264F5D8){
  try {
    if(Owned(ctx.r3.u32))ctx.r3.u64=timeshift::renderer::DeviceRegistryRuntime().PacketSpace(ctx.r3.u32,ctx.r4.u32);
    else __imp__sub_8264F5D8(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[device] sub_8264F5D8 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    ctx.r3.u64 = 0;
  }
}
