#include "device_registry.h"
#include "device_guest_calls.h"
#include <rex/ppc.h>
#include <limits>
#include <stdexcept>

REX_EXTERN(__imp__sub_82647600);
namespace timeshift::renderer {
namespace {
uint32_t Word(std::span<const uint8_t> p,uint32_t at=0){
  if(uint64_t(at)+4>p.size())throw std::out_of_range("device registry guest word");
  return uint32_t(p[at])<<24|uint32_t(p[at+1])<<16|uint32_t(p[at+2])<<8|p[at+3];
}
void Put(std::span<uint8_t> p,uint32_t at,uint32_t value){
  if(uint64_t(at)+4>p.size())throw std::out_of_range("device registry guest word");
  for(unsigned i=0;i<4;++i)p[at+i]=uint8_t(value>>(24-8*i));
}
uint32_t OriginalRelease(PPCContext &parent,uint8_t *base,uint32_t device){
  auto call=parent;call.r3.u64=device;__imp__sub_82647600(call,base);parent.fpscr=call.fpscr;return call.r3.u32;
}
bool HasResources(std::span<const uint8_t> p,bool include_cpu_helper=true){
  if(include_cpu_helper&&Word(p,21552))return true;
  for(auto at:{11812u,12428u,12448u,12684u,12688u,14808u,14812u,14816u})if(Word(p,at))return true;
  for(unsigned i=0;i<4;++i)if(Word(p,12432+i*4))return true;
  for(unsigned i=0;i<16;++i)if(Word(p,12452+i*4))return true;
  for(unsigned i=0;i<26;++i)if(Word(p,12536+i*4))return true;
  return false;
}
}
std::shared_ptr<DeviceRegistry::Entry> DeviceRegistry::Find(uint32_t device){
  std::lock_guard lock(mutex_);auto it=entries_.find(device);return it==entries_.end()?nullptr:it->second;
}
bool DeviceRegistry::Contains(uint32_t device){return bool(Find(device));}
DeviceRegistry::Lease DeviceRegistry::Acquire(uint32_t device,bool continuation){
  auto p=Find(device);if(!p)return {};
  Lease lease(p);
  if(p->phase==Phase::Active||(p->phase==Phase::Creating&&p->creator==std::this_thread::get_id())||
     (continuation&&p->leases&&(p->phase==Phase::Quarantined||
       (p->phase==Phase::Closing&&p->retire_continuations)))){
    ++p->leases;lease.counted_=true;return lease;
  }
  return {};
}
bool DeviceRegistry::Bind(const std::shared_ptr<Entry>&e,uint32_t device){
  if(Contains(device))throw std::logic_error("duplicate live device address");
  e->address=device;
  if(!e->sources.Attach(device))return false;
  if(e->title)e->global=Word(runtime_.Read(d3d6274::kGlobalDeviceImport,4));
  std::lock_guard lock(mutex_);
  if(!entries_.emplace(device,e).second)throw std::logic_error("duplicate live device address");
  return true;
}
void DeviceRegistry::Erase(const std::shared_ptr<Entry>&e){
  std::lock_guard lock(mutex_);auto it=entries_.find(e->address);
  if(it!=entries_.end()&&it->second==e)entries_.erase(it);
  e->phase=Phase::Retired;
}
bool DeviceRegistry::Retire(PPCContext &context,uint8_t *base,const std::shared_ptr<Entry>&e,std::chrono::milliseconds budget){
  auto device=runtime_.Map(e->address,22400);
  if(device.size()!=22400)throw std::logic_error("retiring device disappeared");
  e->phase=Phase::Closing;
  auto defer=[&]{e->phase=Phase::Quarantined;return false;};
  if(e->leases||e->cleanup_faulted)return defer();
  // Caller already owns serial; only explicit nested continuations can enter
  // while source waits, cleanup or callbacks execute. New root work stays shut.
  auto continuation=[&](auto work){
    struct Reset{Entry &e;~Reset(){e.retire_continuations=false;}}reset{*e};
    e->retire_continuations=true;Lease lease(e);++e->leases;lease.counted_=true;
    return work();
  };
  try{
    if(HasResources(device)){
      const bool cleaned=continuation([&]{
        if(!e->cleanup){
          if(!e->cleanup_fence){
            e->cleanup_fence=e->sources.Submit();e->cleanup_callback=runtime_.LastCallback();
          }
          if(!e->cleanup_fence||!e->sources.Wait(*e->cleanup_fence,budget))return false;
          if(e->cleanup_callback!=runtime_.LastCallback()){e->cleanup_fence.reset();return false;}
          e->cleanup=runtime_.BeginDeviceCleanup(e->address);
        }
        return e->cleanup&&e->cleanup->Poll(context,base,runtime_,budget);
      });
      if(!cleaned)return defer();
      device=runtime_.Map(e->address,22400);
      // The retained no-hardware destructor still owns its CPU helper tree.
      if(HasResources(device,false))throw std::logic_error("device cleanup left live guest bindings");
    }
    if(!continuation([&]{return e->sources.Close(budget);}))return defer();
  }catch(...){e->cleanup_faulted=true;e->phase=Phase::Quarantined;throw;}
  runtime_.DeviceRetired(e->address,e->title);
  if(e->title){auto global=runtime_.Map(e->global,4);if(Word(global)==e->address)Put(global,0,0);}
  Erase(e);return true;
}
uint32_t DeviceRegistry::Create(PPCContext &context,uint8_t *base){
  auto output=runtime_.Map(context.r8.u32,4);
  if(output.size()!=4){context.r3.u64=d3d6274::kInvalidArgument;return context.r3.u32;}
  Put(output,0,0);
  auto e=std::make_shared<Entry>(runtime_,config_);e->title=context.r4.u32!=2;
  if(!e->sources.Ready()){context.r3.u64=d3d6274::kOutOfMemory;return context.r3.u32;}
  std::lock_guard serial(e->serial);
  struct Binding{DeviceRegistry *registry;std::shared_ptr<Entry> entry;PPCContext *context;uint8_t *base;};Binding binding{this,e,&context,base};
  const d3d6274::GuestMapping mapping{
    &binding,
    +[](void *user,uint32_t at,size_t bytes)->std::span<std::byte>{
      auto &b=*static_cast<Binding*>(user);if(bytes>std::numeric_limits<uint32_t>::max())return {};
      // The XEX import slot is read-only. Every other bootstrap span is writable.
      if(at==d3d6274::kGlobalDeviceImport&&bytes==4){
        auto s=b.registry->runtime_.Read(at,4);
        return {reinterpret_cast<std::byte*>(const_cast<uint8_t*>(s.data())),s.size()};
      }
      auto s=b.registry->runtime_.Map(at,uint32_t(bytes));return {reinterpret_cast<std::byte*>(s.data()),s.size()};
    },
    +[](void *user,uint32_t device){auto &b=*static_cast<Binding*>(user);return b.registry->Bind(b.entry,device);},
    +[](void *user,uint32_t){auto &b=*static_cast<Binding*>(user);return b.registry->Retire(*b.context,b.base,b.entry,std::chrono::milliseconds(1000));}
  };
  const auto result=d3d6274::BootstrapGuestDevice(context,base,e->sources.BootstrapMemory(),mapping);
  if(result.hresult==0){
    if(e->phase!=Phase::Creating||e->address!=result.device)throw std::logic_error("device bootstrap did not bind ownership");
    e->phase=Phase::Active;
    runtime_.DeviceActivated(e->address,e->title);
  }
  return result.hresult;
}
uint32_t DeviceRegistry::Release(PPCContext &context,uint8_t *base,uint32_t device,std::chrono::milliseconds budget){
  auto e=Find(device);if(!e)return 1; // A formerly owned VA may already be retired.
  std::lock_guard serial(e->serial);
  if(e->phase==Phase::Closing||e->phase==Phase::Retired)return 1;
  auto p=runtime_.Map(device,22400);const auto refs=Word(p,60);
  if(!refs)throw std::logic_error("device reference count underflow");
  if((e->phase==Phase::Active||e->phase==Phase::Creating)&&refs>1)return OriginalRelease(context,base,device);
  if(e->phase==Phase::Creating)return 1; // The bootstrap still owns its initial reference.
  if(refs!=1)throw std::logic_error("retiring device gained references");
  return Retire(context,base,e,budget)?OriginalRelease(context,base,device):1;
}
void DeviceRegistry::RetryPending(PPCContext &context,uint8_t *base){
  static thread_local bool running=false;if(running)return;
  struct Guard{bool &value;Guard(bool &v):value(v){value=true;}~Guard(){value=false;}}guard(running);
  std::vector<std::shared_ptr<Entry>> all;
  {std::lock_guard lock(mutex_);for(auto &[address,e]:entries_)all.push_back(e);}
  for(auto &e:all){
    std::unique_lock serial(e->serial,std::try_to_lock);if(!serial.owns_lock())continue;
    if(e->phase==Phase::Quarantined&&Retire(context,base,e,std::chrono::milliseconds(0)))OriginalRelease(context,base,e->address);
  }
}
uint32_t DeviceRegistry::AllocateInline(uint32_t device,uint32_t words,uint32_t alignment){
  auto lease=Acquire(device,true);return lease?lease->sources.AllocateInline(words,alignment):0;
}
uint32_t DeviceRegistry::Kick(uint32_t device){
  auto lease=Acquire(device,true);if(!lease)return 0;
  lease->sources.Submit();return Word(runtime_.Map(device,22400),48);
}
uint32_t DeviceRegistry::PacketSpace(uint32_t device,uint32_t words){
  auto lease=Acquire(device,true);if(!lease)return 0;
  auto p=runtime_.Map(device,22400);const uint64_t bytes=uint64_t(words)*4;
  // The cursor points to the LAST written word; stwu writes at cursor+4.
  // RingLimit is an exclusive allocation end, so keep that extra word too.
  auto available=[&]{const uint64_t end=Word(p,52),cursor=Word(p,48);return end>=cursor+4?end-cursor-4:0;};
  if(bytes>available()){lease->sources.Submit();p=runtime_.Map(device,22400);}
  if(bytes>available()){p[10941]|=0x20;return 0;}
  return Word(p,48);
}
bool DeviceRegistry::Wait(uint32_t device,uint32_t fence,std::chrono::milliseconds budget){
  auto lease=Acquire(device);return lease&&lease->sources.Wait(fence,budget);
}
std::optional<bool> DeviceRegistry::QueryGlobalFence(uint32_t fence){
  if(!fence)return false;
  const auto slot=runtime_.Read(d3d6274::kGlobalDeviceImport,4);
  if(slot.size()!=4)return {};
  const auto global=runtime_.Read(Word(slot),4);
  if(global.size()!=4)return {};
  const auto device=Word(global);
  if(!Contains(device))return {};
  auto lease=Acquire(device,true);
  // Never fall back into legacy ring code for an owned closing device, and
  // never claim completion when its ownership is unavailable.
  if(!lease)return true;
  runtime_.RunCallbacks();
  if(fence==Word(runtime_.Read(device,22400),10908))lease->sources.Submit();
  return lease->sources.Pending(fence);
}
std::optional<uint32_t> DeviceRegistry::ReadOwnedCompletion(uint32_t device,uint32_t completion){
  if(!Contains(device))return {};
  auto lease=Acquire(device,true);
  if(!lease)throw std::logic_error("completion read on an unavailable owned device");
  if(!completion||completion!=lease->sources.BootstrapMemory().fence)
    throw std::logic_error("owned device completion address changed");
  return runtime_.Completed(completion);
}
}
