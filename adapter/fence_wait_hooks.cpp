#include "device_registry.h"
#include <rex/ppc.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

REX_EXTERN(__imp__sub_82647528);
REX_EXTERN(__imp__sub_82653648);
REX_EXTERN(__imp__sub_8264ED40);
namespace {
struct NativePoll;
thread_local NativePoll *active_poll=nullptr;
struct NativePoll{
  uint32_t device;bool watchdog=false;NativePoll *previous;
  explicit NativePoll(uint32_t d):device(d),previous(active_poll){active_poll=this;}
  ~NativePoll(){active_poll=previous;}
};
}

REX_EXTERN(sub_8264ED40){
  try {
    if(timeshift::renderer::DeviceLifecycleHooksEnabled()){
      auto &registry=timeshift::renderer::DeviceRegistryRuntime();
      if(registry.Contains(ctx.r3.u32)){
        auto lease=registry.Acquire(ctx.r3.u32,true);
        if(!lease)throw std::logic_error("fence wait on an unavailable owned device");
        const auto tag=registry.Memory().Read(ctx.r3.u32+10908,4);
        if(tag.size()!=4)throw std::logic_error("owned fence tag unavailable");
        const uint32_t current=uint32_t(tag[0])<<24|uint32_t(tag[1])<<16|uint32_t(tag[2])<<8|tag[3];
        // Native segments deliberately do not use legacy device+13216. Preserve
        // the original current-fence-inside-segment early return explicitly.
        if(ctx.r4.u32==current&&lease->sources.InSegment())return;
        __imp__sub_8264ED40(ctx,base);
        return;
      }
    }
    __imp__sub_8264ED40(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[fence_wait] sub_8264ED40 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
#if !defined(__SWITCH__)
    throw;
#endif
  }
}

// Retain TimeShift's CBlocker progress clock, metrics and Close callback.
// The selected completion loads in its retained CPU bodies are instrumented
// by port_fence_readers.py, instead of copying a different title's layout.
REX_EXTERN(sub_82647528){
  try {
    if(timeshift::renderer::DeviceLifecycleHooksEnabled()){
      auto &registry=timeshift::renderer::DeviceRegistryRuntime();
      const auto bytes=registry.Memory().Read(ctx.r3.u32,4);
      if(bytes.size()==4){
        const uint32_t device=uint32_t(bytes[0])<<24|uint32_t(bytes[1])<<16|uint32_t(bytes[2])<<8|bytes[3];
        if(registry.Contains(device)){
          auto lease=registry.Acquire(device,true);
          if(!lease)throw std::logic_error("wait poll on an unavailable owned device");
          registry.Memory().RunCallbacks();
          const auto prior=registry.Memory().Read(ctx.r3.u32+8,4);
          if(prior.size()!=4)throw std::logic_error("owned wait state unavailable");
          const uint32_t previous=uint32_t(prior[0])<<24|uint32_t(prior[1])<<16|uint32_t(prior[2])<<8|prior[3];
          const auto completed=registry.ReadOwnedCompletion(device,lease->sources.BootstrapMemory().fence);
          static thread_local uint32_t s_wait_iter = 0;
          if(completed==previous){
            if(!registry.Memory().WaitProgress(std::chrono::milliseconds(1)))
              throw std::runtime_error("ReNut source wait cannot progress: render queue stopped");
            if(s_wait_iter < 64){
              std::this_thread::yield();
            } else if(s_wait_iter < 84){
              std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ++s_wait_iter;
          } else {
            s_wait_iter = 0;
          }
          NativePoll poll(device);
          __imp__sub_82647528(ctx,base);
          // Five guest seconds is a GPU watchdog, not a safe deadline for host
          // shader compilation. Keep waiting while the host queue is alive;
          // the outer retained loop still requires actual locked completion.
          if(poll.watchdog)ctx.r3.u64=1;
          return;
        }
      }
    }
    __imp__sub_82647528(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[fence_wait] sub_82647528 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
#if !defined(__SWITCH__)
    throw;
#endif
  }
}

// Original CBlocker calls this after5000 guest milliseconds without progress.
// Its hardware-register dump must never run on a renderer-owned device. The
// matching poll overrides its false result so the outer wait cannot escape
// with incomplete sources. This adds no automatic five-second game exit.
REX_EXTERN(sub_82653648){
  try {
    if(timeshift::renderer::DeviceLifecycleHooksEnabled()&&
       timeshift::renderer::DeviceRegistryRuntime().Contains(ctx.r3.u32))
    {
      if(active_poll&&active_poll->device==ctx.r3.u32){active_poll->watchdog=true;return;}
      throw std::runtime_error("legacy GPU diagnostic requested outside a native wait poll");
    }
    __imp__sub_82653648(ctx,base);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[fence_wait] sub_82653648 EXCEPTION: %s\n", e.what());
    std::fflush(stderr);
#if !defined(__SWITCH__)
    throw;
#endif
  }
}
