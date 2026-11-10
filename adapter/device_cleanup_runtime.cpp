#include "device_cleanup.h"
#include "source_runtime.h"
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/backend/video.h"

namespace timeshift::renderer {
namespace {
struct GpuIdle {
 enum Status:uint32_t{Queued,Ready,Unavailable,Failed};
 std::atomic<uint32_t>status{Queued};
};
class SdkDeviceCleanup final:public DeviceCleanup {
 public:
 explicit SdkDeviceCleanup(uint32_t device):device_(device){}
 bool Poll(PPCContext &context,uint8_t *base,SourceRuntime &runtime,std::chrono::milliseconds budget)override{
  if(faulted_)throw std::logic_error("partial device cleanup cannot be replayed");
  if(done_)return true;
  const auto deadline=std::chrono::steady_clock::now()+budget;
  if(!gpu_||(gpu_->status.load(std::memory_order_acquire)==GpuIdle::Unavailable&&renut::gpu::Video::HostReady())){
   gpu_=std::make_shared<GpuIdle>();auto ticket=gpu_;
   // This is a real GPU barrier, queued after source/callback quiescence.
   // No Entry/context/guest-pointer references escape to the render thread.
   try{renut::gpu::render_thread::Queue([ticket]{
    try{ticket->status.store(renut::gpu::Video::QuiesceGuestWorkLocked()?GpuIdle::Ready:GpuIdle::Unavailable,std::memory_order_release);}
    catch(...){ticket->status.store(GpuIdle::Failed,std::memory_order_release);}
   });}catch(...){faulted_=true;throw;}
  }
  while(gpu_->status.load(std::memory_order_acquire)==GpuIdle::Queued){
   runtime.RunCallbacks();
   const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
   if(left.count()<=0||!runtime.WaitProgress(std::min(left,std::chrono::milliseconds(2))))return false;
  }
  const auto status=gpu_->status.load(std::memory_order_acquire);
  if(status==GpuIdle::Failed){faulted_=true;throw std::runtime_error("GPU retirement barrier failed");}
  if(status!=GpuIdle::Ready)return false;
  try{done_=ReleaseGuestDeviceBindings(context,base,runtime,device_);}
  catch(...){faulted_=true;throw;}
  return done_;
 }
 private:
 uint32_t device_;bool faulted_=false,done_=false;std::shared_ptr<GpuIdle>gpu_;
};
}
std::unique_ptr<DeviceCleanup> MakeSdkDeviceCleanup(uint32_t device){return std::make_unique<SdkDeviceCleanup>(device);}
std::unique_ptr<DeviceCleanup> SdkSourceRuntime::BeginDeviceCleanup(uint32_t device){return MakeSdkDeviceCleanup(device);}
void SdkSourceRuntime::DeviceActivated(uint32_t device,bool title){if(title)renut::gpu::Video::SetGuestDevice(device);}
void SdkSourceRuntime::DeviceRetired(uint32_t device,bool title){
 if(title)renut::gpu::Video::ClearGuestDeviceIf(device);
}
}
