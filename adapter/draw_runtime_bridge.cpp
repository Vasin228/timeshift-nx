#include "draw_runtime_bridge.h"
#include "render_drop_telemetry.h"
#include "shader_runtime_bridge.h"
#include "clear_rectangle.h"
#include <bit>
#include <limits>
#include <stdexcept>
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/guest/callbacks.h"
#include "renut_engine/gpu/guest/command_buffers.h"
#include "renut_engine/gpu/guest/guest_memory.h"

namespace timeshift::renderer {
using namespace renut::gpu;
bool DrawHooksEnabled(){return ResourceHooksEnabled();} // Still false in production.
namespace {
uint32_t Bytes(uint64_t count,uint64_t stride){
  const uint64_t size=count*stride;
  if(size>std::numeric_limits<uint32_t>::max())throw std::overflow_error("TimeShift draw byte extent");
  return uint32_t(size);
}
void RequireImmediateDevice(uint32_t device){
  if(IsRecordingCommandBuffer(device))
    throw std::logic_error("TimeShift command-buffer replay is not integrated; keep backend disabled");
}
ClearRect Rect(uint32_t address){
  const auto *p=GuestPtr<const be_u32>(address);
  return {int32_t(uint32_t(p[0])),int32_t(uint32_t(p[1])),int32_t(uint32_t(p[2])),int32_t(uint32_t(p[3]))};
}
void Color(uint32_t address,float color[4]){
  if(const auto *p=GuestPtr<const be_u32>(address))
    for(unsigned i=0;i<4;++i)color[i]=std::bit_cast<float>(uint32_t(p[i]));
}
}

void FlushPendingDraw(uint32_t device){
  FlushDeferredVertices(device);
  RunReadyCallbacks();
}
void DrawIndexed(uint32_t device,uint32_t primitive,int32_t base,uint32_t start,uint32_t count){
  RequireImmediateDevice(device);
  DrawCall call{};call.primitive=primitive;call.indexed=true;call.base_vertex=base;call.start_index=start;call.count=count;
  QueueDraw(device,call,0,0);
}
void DrawVerticesUP(uint32_t device,uint32_t primitive,uint32_t count,uint32_t data,uint32_t stride){
  RequireImmediateDevice(device);
  DrawCall call{};call.primitive=primitive;call.count=count;call.up_vertices=GuestPtr<const u8>(data);
  call.up_stride=stride;call.up_vertex_count=count;
  QueueDraw(device,call,Bytes(count,stride),0);
}
void DrawIndexedUP(uint32_t device,uint32_t primitive,uint32_t minimum,uint32_t vertices,uint32_t count,
                   uint32_t indices,uint32_t format,uint32_t data,uint32_t stride){
  RequireImmediateDevice(device);
  const uint64_t total=uint64_t(minimum)+vertices;
  if(total>std::numeric_limits<uint32_t>::max())throw std::overflow_error("TimeShift UP vertex range");
  DrawCall call{};call.primitive=primitive;call.indexed=true;call.count=count;
  call.up_vertices=GuestPtr<const u8>(data);call.up_stride=stride;call.up_vertex_count=uint32_t(total);
  call.up_indices=GuestPtr<const u8>(indices);call.up_indices_32=(format&4)!=0;
  QueueDraw(device,call,Bytes(total,stride),Bytes(count,call.up_indices_32?4:2));
}
void BeginVerticesCompleted(uint32_t device,uint32_t primitive,uint32_t count,uint32_t stride,uint32_t data){
  RequireImmediateDevice(device);
  DrawCall call{};call.primitive=primitive;call.count=count;call.up_stride=stride;call.up_vertex_count=count;
  call.up_vertices=GuestPtr<const u8>(data);
  QueueDeferredDraw(device,call,call.up_vertices,Bytes(count,stride));
}
void BeginIndexedCompleted(uint32_t device,uint32_t primitive,int32_t base,uint32_t vertices,uint32_t count,
                           uint32_t format,uint32_t stride,uint32_t data,uint32_t indices){
  RequireImmediateDevice(device);
  DrawCall call{};call.primitive=primitive;call.indexed=true;call.base_vertex=base;call.count=count;
  call.up_stride=stride;call.up_vertex_count=vertices;call.up_vertices=GuestPtr<const u8>(data);
  call.up_indices=GuestPtr<const u8>(indices);call.up_indices_32=(format&4)!=0;
  QueueDeferredDraw(device,call,call.up_vertices,Bytes(vertices,stride),call.up_indices,
                    Bytes(count,call.up_indices_32?4:2));
}
void ClearFrame(uint32_t device,uint32_t flags,uint32_t rectangle,uint32_t color,float depth,uint32_t stencil){
  RequireImmediateDevice(device);ClearRect rect{};if(rectangle)rect=Rect(rectangle);
  else {
    const auto bounds=DefaultClearRectangle(device,
      [](uint32_t at){return uint32_t(*GuestPtr<const be_u32>(at));},
      [](uint32_t at){return *GuestPtr<const u8>(at);});
    rect={bounds[0],bounds[1],bounds[2],bounds[3]};
  }
  if(rect.x2<=rect.x1||rect.y2<=rect.y1) {
    timeshift::telemetry::g_drop_clear_empty_rect.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  float value[4]={};Color(color,value);
  QueueClear(device,flags,&rect,value,depth,stencil);
}
void ResolveFrame(uint32_t device,uint32_t flags,uint32_t rectangle,uint32_t destination,uint32_t point,
                  uint32_t level,uint32_t slice,uint32_t color,float depth,uint32_t stencil,uint32_t parameters){
  RequireImmediateDevice(device);
  if(!destination) {
    timeshift::telemetry::g_drop_resolve_invalid.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if(parameters){
    const auto *p=GuestPtr<const uint8_t>(parameters);
    if(!p)throw std::out_of_range("TimeShift resolve parameters unavailable");
    DecodeResolveParameters(std::span<const uint8_t>(p,12));
  }
  ClearRect rect{};
  if(rectangle){
    rect=Rect(rectangle);
    if(rect.x2<=rect.x1||rect.y2<=rect.y1) {
      timeshift::telemetry::g_drop_resolve_invalid.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  int32_t x=0,y=0;
  if(const auto *p=GuestPtr<const be_u32>(point)){x=int32_t(uint32_t(p[0]));y=int32_t(uint32_t(p[1]));}
  float value[4]={};Color(color,value);
  QueueResolve(device,flags,rectangle?&rect:nullptr,destination,x,y,level,slice,color?value:nullptr,depth,stencil);
}
void DrawPrimitive(uint32_t device,uint32_t primitive,uint32_t start,uint32_t count){
  RequireImmediateDevice(device);
  throw std::logic_error("TimeShift bound non-indexed draws are unsupported; all draws must be indexed or UP");
}
}
