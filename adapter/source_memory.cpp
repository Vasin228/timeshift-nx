#include "source_memory.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace timeshift::renderer {
namespace {
constexpr uint32_t page=65536;
uint32_t Read(std::span<uint8_t> p,uint32_t at){return uint32_t(p[at])<<24|uint32_t(p[at+1])<<16|uint32_t(p[at+2])<<8|p[at+3];}
void Write(std::span<uint8_t> p,uint32_t at,uint32_t v){for(unsigned i=0;i<4;++i)p[at+i]=uint8_t(v>>(24-i*8));}
bool PendingTag(uint32_t current,uint32_t completed,uint32_t fence){return fence&&uint32_t(current-fence)<uint32_t(current-completed);}
}
DeviceSources::DeviceSources(SourceRuntime&r,SourceConfig c):runtime_(r),config_(c){
  if(c.packet_bytes%page||c.packet_bytes<=c.guarantee_margin||c.guarantee_margin<160||c.guarantee_margin%32||
     !c.chunk_bytes||c.chunk_bytes%page||c.source_budget<c.chunk_bytes||c.source_budget%page)
    throw std::invalid_argument("invalid TimeShift source-memory configuration");
  packet_=r.Allocate(c.packet_bytes);
  if(packet_)fence_=r.Allocate(page);
  if(!Ready())ReleaseAll();
}
DeviceSources::~DeviceSources(){if(!device_&&!faulted_)ReleaseAll();}
d3d6274::BootstrapMemory DeviceSources::BootstrapMemory()const{
  return {packet_,config_.packet_bytes,config_.guarantee_margin,fence_,page};
}
std::span<uint8_t> DeviceSources::Device(){
  auto p=runtime_.Map(device_,22400);
  if(!device_||p.size()!=22400||Read(p,13500)||Read(p,10908)!=next_tag_)
    throw std::logic_error("TimeShift source owner/device contract changed");
  return p;
}
bool DeviceSources::Attach(uint32_t device){
  if(!Ready()||device_||closing_||faulted_)return false;
  auto p=runtime_.Map(device,22400);
  if(p.size()!=22400||Read(p,13500)||Read(p,10896)!=fence_||Read(p,10900)!=fence_+128||
     Read(p,52)!=uint64_t(packet_)+config_.packet_bytes||
     Read(p,56)!=uint64_t(packet_)+config_.packet_bytes-config_.guarantee_margin||
     Read(p,48)<packet_||Read(p,48)>=uint64_t(packet_)+config_.packet_bytes||
     !(Read(p,10908)&1)||runtime_.Completed(fence_)!=uint32_t(Read(p,10908)-2))return false;
  device_=device;next_tag_=Read(p,10908);p[10941]&=0xFD;return true;
}
void DeviceSources::Collect(){
  const auto completed=runtime_.Completed(fence_);
  for(auto &c:chunks_)if(c.status==Status::Sealed&&!PendingTag(next_tag_,completed,c.tag)){
    c.status=Status::Free;c.used=c.tag=0;
  }
}
uint32_t DeviceSources::FailAllocation(){if(device_)Device()[10941]|=0x20;return 0;}
uint32_t DeviceSources::AllocateInline(uint32_t words,uint32_t alignment){
  if(!device_||closing_||faulted_)return 0;
  auto device=Device();
  if(device[10941]&0x20)return 0;
  if(alignment<4||alignment>page||(alignment&(alignment-1))||words>0x3FFFFFFF)return FailAllocation();
  const uint32_t bytes=std::max(4u,words*4);
  Collect();
  auto take=[&](Chunk &c)->uint32_t{
    if(c.status==Status::Sealed)return 0;
    const uint64_t offset=(uint64_t(c.used)+alignment-1)&~uint64_t(alignment-1);
    if(offset+bytes>c.bytes)return 0;
    c.used=uint32_t(offset+bytes);c.status=Status::Open;return c.address+uint32_t(offset);
  };
  for(auto &c:chunks_)if(auto address=take(c))return address;
  const uint64_t required=(uint64_t(bytes)+alignment-1+page-1)&~uint64_t(page-1);
  const uint64_t size=std::max<uint64_t>(config_.chunk_bytes,required);
  if(size>config_.source_budget)return FailAllocation();
  if(size>config_.source_budget-source_bytes_){
    // Only a sealed epoch can be waited/reused. Earlier allocations of a
    // BeginIndexed still being filled belong to Open and must stay disjoint.
    auto sealed=std::find_if(chunks_.begin(),chunks_.end(),[](auto &c){return c.status==Status::Sealed;});
    if(sealed!=chunks_.end()){const auto tag=sealed->tag;Wait(tag,config_.allocation_wait);}
    Collect();for(auto &c:chunks_)if(auto result=take(c))return result;
    for(auto &c:chunks_)if(c.status==Status::Free&&runtime_.Release(c.address)){
      source_bytes_-=c.bytes;c.address=0;
    }
    std::erase_if(chunks_,[](auto &c){return !c.address;});
    if(size>config_.source_budget-source_bytes_)return FailAllocation();
  }
  const auto address=runtime_.Allocate(uint32_t(size));
  if(!address)return FailAllocation();
  try{chunks_.push_back({address,uint32_t(size)});}catch(...){runtime_.Release(address);throw;}
  source_bytes_+=uint32_t(size);return take(chunks_.back());
}
void DeviceSources::RewindPackets(){
  auto p=Device();Write(p,48,packet_);Write(p,52,packet_+config_.packet_bytes);
  Write(p,56,packet_+config_.packet_bytes-config_.guarantee_margin);
}
std::optional<uint32_t> DeviceSources::Submit(){
  if(closing_)return {};
  return Seal();
}
std::optional<uint32_t> DeviceSources::Seal(){
  if(!device_||faulted_)return {};
  Device();runtime_.FlushSources(device_);
  // An original tiling segment does not issue its final source fence at an
  // intermediate KickOff. Never reclaim its still-open allocations here.
  if(segment_){RewindPackets();return {};}
  const auto tag=next_tag_;
  try{runtime_.QueueCompletion(fence_,tag,runtime_.LastCallback());}
  catch(...){faulted_=true;throw;} // Completion may have escaped; retain memory.
  for(auto &c:chunks_)if(c.status==Status::Open){c.status=Status::Sealed;c.tag=tag;}
  next_tag_+=2;auto p=runtime_.Map(device_,22400);Write(p,10908,next_tag_);
  RewindPackets();return tag;
}
bool DeviceSources::Pending(uint32_t fence){
  if(!device_||faulted_)return fence!=0;
  Device();return PendingTag(next_tag_,runtime_.Completed(fence_),fence);
}
bool DeviceSources::Wait(uint32_t fence,std::chrono::milliseconds budget){
  if(!fence)return true;
  if(!device_||faulted_)return false;
  if(fence==next_tag_){if(segment_)return false;if(!Submit())return false;}
  const auto end=std::chrono::steady_clock::now()+budget;
  while(Pending(fence)){
    runtime_.RunCallbacks();if(!Pending(fence))break;
    auto left=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now());
    if(left.count()<=0||!runtime_.WaitProgress(std::min(left,std::chrono::milliseconds(10))))return false;
  }
  Collect();return true;
}
bool DeviceSources::BeginSegment(){
  if(!device_||segment_||closing_||faulted_)return false;
  Submit();segment_=true;return true;
}
bool DeviceSources::EndSegment(){
  if(!device_||!segment_||closing_||faulted_)throw std::logic_error("unmatched TimeShift segment end");
  segment_=false;auto p=Device();const bool oom=p[10941]&0x20;p[10941]&=0xDF;
  Submit();return oom;
}
bool DeviceSources::ReleaseAll(){
  bool ok=true;
  for(auto &c:chunks_)if(c.address){if(runtime_.Release(c.address))c.address=0;else ok=false;}
  if(fence_){if(runtime_.Release(fence_))fence_=0;else ok=false;}
  if(packet_){if(runtime_.Release(packet_))packet_=0;else ok=false;}
  if(ok){chunks_.clear();source_bytes_=0;}return ok;
}
bool DeviceSources::Close(std::chrono::milliseconds budget){
  if(faulted_||segment_)return false;
  if(!device_)return ReleaseAll();
  closing_=true;
  if(!close_fence_)close_fence_=Seal();
  if(!close_fence_||!Wait(*close_fence_,budget))return false;
  auto p=Device();Write(p,48,0);Write(p,52,0);Write(p,56,0);Write(p,10896,0);Write(p,10900,0);
  device_=0;return ReleaseAll();
}
}
