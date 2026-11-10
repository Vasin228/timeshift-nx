#include "sdk_tiling_host.h"
#include "device_registry.h"
#include <rex/ppc.h>
#include <cstdio>
#include <exception>

REX_EXTERN(__imp__sub_8264B8C0);
REX_EXTERN(__imp__sub_8264BD58);
REX_EXTERN(__imp__sub_8264B750);
namespace {
bool Owned(uint32_t device){return timeshift::renderer::DeviceLifecycleHooksEnabled()&&timeshift::renderer::DeviceRegistryRuntime().Contains(device);}
}
REX_EXTERN(sub_8264B8C0){
  try {
    if(Owned(ctx.r3.u32))timeshift::renderer::NativeBeginTiling(ctx,base,timeshift::renderer::DeviceRegistryRuntime());
    else __imp__sub_8264B8C0(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[tiling] BeginTiling EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}
REX_EXTERN(sub_8264BD58){
  try {
    if(Owned(ctx.r3.u32))timeshift::renderer::NativeEndTiling(ctx,base,timeshift::renderer::DeviceRegistryRuntime());
    else __imp__sub_8264BD58(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[tiling] EndTiling EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}
REX_EXTERN(sub_8264B750){
  try {
    if(Owned(ctx.r3.u32))timeshift::renderer::NativeSetPredication(ctx,base,timeshift::renderer::DeviceRegistryRuntime());
    else __imp__sub_8264B750(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[tiling] SetPredication EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
  }
}
