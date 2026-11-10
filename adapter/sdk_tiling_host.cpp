#include "sdk_tiling_host.h"
#include "device_registry.h"
#include "tiling.h"
#include "tiling_guest_calls.h"
#include <rex/ppc.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/guest/callbacks.h"
#include "renut_engine/gpu/render/surfaces.h"

REX_EXTERN(__imp__sub_8263D008);
REX_EXTERN(__imp__sub_8263D088);
REX_EXTERN(__imp__sub_8263C858);
REX_EXTERN(__imp__sub_8263BD20);
REX_EXTERN(__imp__sub_8263CC98);
REX_EXTERN(__imp__sub_8264A810);
REX_EXTERN(__imp__sub_8264C8E8);
namespace timeshift::renderer {
namespace {
class SdkTilingHost final:public TilingHost {
 public:
  SdkTilingHost(PPCContext &context,uint8_t *base,DeviceRegistry &registry,DeviceRegistry::Entry &entry,bool calls)
   :parent_(context),base_(base),memory_(registry.Memory()),entry_(entry){
    if(calls){
      // Own outgoing arguments and scratch without touching the caller's frame.
      // The retained ClearF closure needs up to1456 bytes (Resolve1120),
      // including conservative ABI save red zones, below our256-byte frame.
      // See tools/prepare_frame_cpu_fixture.py; a384-byte Resolve alone is
      // insufficient to size its nested original calls.
      constexpr uint32_t stack_bytes=2048;
      if(parent_.r1.u32<stack_bytes||memory_.Map(parent_.r1.u32-stack_bytes,stack_bytes).size()!=stack_bytes)
        throw std::out_of_range("native tiling guest stack frame unavailable");
      frame_=parent_.r1.u32-256;
    }
  }
  std::span<uint8_t> Map(uint32_t at,uint32_t size)override{return memory_.Map(at,size);}
  std::span<const uint8_t> Read(uint32_t at,uint32_t size)override{return memory_.Read(at,size);}
  void Flush(uint32_t device)override{
    renut::gpu::FlushDeferredVertices(device);renut::gpu::RunReadyCallbacks();
  }
  void Tiles(std::span<const TileRect> tiles,uint32_t width,uint32_t height)override{
    std::vector<renut::gpu::ClearRect> copy;copy.reserve(tiles.size());
    for(auto r:tiles)copy.push_back({int32_t(r.x1),int32_t(r.y1),int32_t(r.x2),int32_t(r.y2)});
    renut::gpu::render_thread::Queue([copy=std::move(copy),width,height]{
      renut::gpu::SetTilingExtentLocked(width,height);
      renut::gpu::SetTilesLocked(copy.data(),uint32_t(copy.size()));
    });
  }
  void FullViewport(uint32_t device)override{Call(__imp__sub_8263D008,{device,0x8205F338});}
  void RestoreViewport(uint32_t device,std::span<const uint8_t,28> bytes)override{
    Copy(frame_+128,bytes);Call(__imp__sub_8263D088,{device,frame_+128});
  }
  void FullScissor(uint32_t device)override{Call(__imp__sub_8263C858,{device,0x8205F350});}
  void RestoreScissor(uint32_t device,std::span<const uint8_t,16> bytes)override{
    Copy(frame_+160,bytes);Call(__imp__sub_8263C858,{device,frame_+160});
  }
  void Clear(uint32_t device,TileRect rect,uint32_t color,float depth,uint32_t stencil,bool whole)override{
    const auto host_rect=HostRect(rect);
    if(host_rect.x2<=host_rect.x1||host_rect.y2<=host_rect.y1)return;
    const auto values=Color(color);
    renut::gpu::QueueClear(device,63,whole?nullptr:&host_rect,values.data(),depth,stencil);
    Rect(frame_+192,rect);
    // Original CPU work remains authoritative, but bypass the public ClearF
    // hook so that the host clear is queued exactly once.
    Call(__imp__sub_8264A810,{device,63,frame_+192,color,0,stencil},depth);
  }
  void Resolve(uint32_t device,const TileResolve &resolve)override{
    ValidateResolveParameters(resolve.parameters);
    const auto rect=HostRect(resolve.rectangle);
    if(rect.x2<=rect.x1||rect.y2<=rect.y1)return;
    if(!resolve.destination)return;
    const auto values=Color(resolve.color);
    renut::gpu::QueueResolve(device,resolve.flags,&rect,resolve.destination,int32_t(resolve.x),int32_t(resolve.y),
      0,0,resolve.color?values.data():nullptr,resolve.depth,resolve.stencil);
    Rect(frame_+192,resolve.rectangle);Word(frame_+208,resolve.x);Word(frame_+212,resolve.y);
    Word(frame_+92,resolve.stencil);Word(frame_+100,resolve.parameters);
    Call(__imp__sub_8264C8E8,{device,resolve.flags,frame_+192,resolve.destination,frame_+208,0,0,resolve.color},resolve.depth);
  }
  void ValidateResolveParameters(uint32_t parameters)override{
    if(!parameters)return;
    auto p=memory_.Read(parameters,12);
    if(p.size()!=12)throw std::out_of_range("native tiling resolve parameters unavailable");
    DecodeResolveParameters(p);
  }
  void HiZ(uint32_t device,uint32_t value)override{Call(__imp__sub_8263BD20,{device,value});}
  void ResetState(uint32_t device,bool begin)override{Call(__imp__sub_8263CC98,{device,uint32_t(begin)});}
  void BeginSegment(uint32_t)override{
    if(!entry_.sources.BeginSegment())throw std::logic_error("native tiling segment already active");
  }
  bool EndSegment(uint32_t,uint32_t,uint32_t)override{return entry_.sources.EndSegment();}
 private:
  PPCContext &parent_;uint8_t *base_;SourceRuntime &memory_;DeviceRegistry::Entry &entry_;uint32_t frame_=0;
  void Copy(uint32_t at,std::span<const uint8_t> bytes){
    auto p=memory_.Map(at,uint32_t(bytes.size()));if(p.size()!=bytes.size())throw std::out_of_range("native tiling scratch unavailable");
    std::memmove(p.data(),bytes.data(),bytes.size());
  }
  void Word(uint32_t at,uint32_t value){
    std::array<uint8_t,4>b{};for(unsigned i=0;i<4;++i)b[i]=uint8_t(value>>(24-8*i));Copy(at,b);
  }
  void Rect(uint32_t at,TileRect r){Word(at,r.x1);Word(at+4,r.y1);Word(at+8,r.x2);Word(at+12,r.y2);}
  static renut::gpu::ClearRect HostRect(TileRect r){return {int32_t(r.x1),int32_t(r.y1),int32_t(r.x2),int32_t(r.y2)};}
  std::array<float,4> Color(uint32_t address){
    std::array<float,4> result{};if(!address)return result;
    auto p=memory_.Read(address,16);if(p.size()!=16)throw std::out_of_range("native tiling clear color unavailable");
    for(unsigned i=0;i<4;++i){const auto at=i*4;
      result[i]=std::bit_cast<float>(uint32_t(p[at])<<24|uint32_t(p[at+1])<<16|uint32_t(p[at+2])<<8|p[at+3]);}
    return result;
  }
  void Call(PPCFunc *function,std::initializer_list<uint32_t> words,float depth=0){
    if(!frame_||words.size()>8)throw std::logic_error("native tiling original call frame");
    auto call=parent_;call.r1.u64=frame_;call.f1.f64=depth;
    PPCRegister *args[]={&call.r3,&call.r4,&call.r5,&call.r6,&call.r7,&call.r8,&call.r9,&call.r10};
    unsigned i=0;for(auto word:words)args[i++]->u64=word;
    function(call,base_);parent_.fpscr=call.fpscr;
  }
};
}
void NativeBeginTiling(PPCContext &context,uint8_t *base,DeviceRegistry &registry){
  auto lease=registry.Acquire(context.r3.u32);if(!lease){context.r3.u64=0x8007000E;return;}
  SdkTilingHost host(context,base,registry,*lease,true);BeginGuestTiling(context,host);
}
void NativeEndTiling(PPCContext &context,uint8_t *base,DeviceRegistry &registry){
  auto lease=registry.Acquire(context.r3.u32);if(!lease){context.r3.u64=0x8007000E;return;}
  SdkTilingHost host(context,base,registry,*lease,true);EndGuestTiling(context,host);
}
void NativeSetPredication(PPCContext &context,uint8_t *base,DeviceRegistry &registry){
  auto lease=registry.Acquire(context.r3.u32);if(!lease)return;
  SdkTilingHost host(context,base,registry,*lease,false);
  SetTilePredication(host,context.r3.u32,context.r4.u32);
}
}
