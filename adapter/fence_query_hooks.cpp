#include "device_registry.h"
#include "perf_breakdown.h"
#include <rex/ppc.h>
#include <cstdio>
#include <exception>

REX_EXTERN(__imp__sub_8264F4F0);

// BOOL D3DDevice_IsFencePending(DWORD fence), not (device,fence).
// The completion read shares ReNut's writer mutex through DeviceSources.
REX_EXTERN(sub_8264F4F0){
  try {
    if(timeshift::renderer::DeviceLifecycleHooksEnabled()){
      const uint64_t t0=timeshift::telemetry::NowTicks();
      const auto pending=timeshift::renderer::DeviceRegistryRuntime().QueryGlobalFence(ctx.r3.u32);
      const uint64_t t1=timeshift::telemetry::NowTicks();
      timeshift::telemetry::g_ticks_fence_query.fetch_add(t1>t0?(t1-t0):0,std::memory_order_relaxed);
      timeshift::telemetry::g_count_fence_query.fetch_add(1,std::memory_order_relaxed);
      if(pending.has_value()){ctx.r3.u64=*pending;return;}
    }
    __imp__sub_8264F4F0(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[fence_query] sub_8264F4F0 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
    __imp__sub_8264F4F0(ctx,base);
  }
}
