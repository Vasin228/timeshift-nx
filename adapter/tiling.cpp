#include "tiling.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace timeshift::renderer {
namespace {
struct Memory {
  TilingHost &host;
  std::span<uint8_t> At(uint32_t at,uint32_t size){
    if(uint64_t(at)+size>0x100000000ull)throw std::out_of_range("tiling guest range wraps");
    auto result=host.Map(at,size);
    if(result.size()!=size)throw std::out_of_range("tiling guest range is unavailable");
    return result;
  }
  std::span<const uint8_t> Read(uint32_t at,uint32_t size){
    if(uint64_t(at)+size>0x100000000ull)throw std::out_of_range("tiling guest range wraps");
    auto result=host.Read(at,size);
    if(result.size()!=size)throw std::out_of_range("tiling guest read range is unavailable");
    return result;
  }
  uint8_t B(uint32_t at){return Read(at,1)[0];}
  void B(uint32_t at,uint8_t value){At(at,1)[0]=value;}
  uint32_t W(uint32_t at){auto p=Read(at,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
  void W(uint32_t at,uint32_t value){auto p=At(at,4);for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(24-8*i));}
  uint64_t D(uint32_t at){return uint64_t(W(at))<<32|W(at+4);}
  void D(uint32_t at,uint64_t value){W(at,uint32_t(value>>32));W(at+4,uint32_t(value));}
  TileRect Rect(uint32_t at){return {W(at),W(at+4),W(at+8),W(at+12)};}
  void Rect(uint32_t at,TileRect r){W(at,r.x1);W(at+4,r.y1);W(at+8,r.x2);W(at+12,r.y2);}
};
constexpr uint32_t max_tiles=15;
void Device(Memory &m,uint32_t device){
  if(device>0xFFFFFFFFu-22400)throw std::out_of_range("tiling device wraps");
  m.At(device,22400);
  if(m.W(device+13500))throw std::invalid_argument("TimeShift tiling command-buffer replay is not integrated");
}
std::vector<TileRect> Rectangles(Memory &m,uint32_t at,uint32_t count){
  if(count>max_tiles)throw std::invalid_argument("TimeShift device has fifteen tile slots");
  if(count)m.Read(at,count*16);
  std::vector<TileRect> result;
  for(uint32_t i=0;i<count;++i)result.push_back(m.Rect(at+i*16));
  return result;
}
void UpdatePredicated(Memory &m,uint32_t d){
  const uint8_t flags=m.B(d+10940);
  bool tiled=flags&0x10;
  if(!tiled&&(flags&0x20)){
    tiled=true;
    for(uint32_t i=0;i<5;++i){
      const auto bound=m.W(d+12432+i*4);
      if(bound&&bound!=m.W(d+12720+i*4)){tiled=false;break;}
    }
  }
  const bool enabled=!(flags&0xC)&&!m.B(d+12179)&&tiled;
  m.B(d+10940,uint8_t((flags&0xFE)|enabled));
}
void ResetPredication(Memory &m,uint32_t d){
  m.W(d+12708,0);UpdatePredicated(m,d);m.W(d+12704,0);
  m.W(d+10932,0);m.W(d+10936,0);m.W(d+12700,0xFFFFFFFF);
}
}

void SetTilePredication(TilingHost &host,uint32_t device,uint32_t mask){
  Memory m{host};Device(m,device);
  if(mask){m.W(device+12700,mask);m.W(device+12708,1);m.B(device+10940,m.B(device+10940)&0xFE);}
  else {m.W(device+12708,0);UpdatePredicated(m,device);m.W(device+12700,0xFFFFFFFF);}
  m.W(device+12704,0);m.W(device+10932,mask);m.W(device+10936,0);
}

void BeginTiling(TilingHost &host,const BeginTiles &a){
  Memory m{host};const auto d=a.device;Device(m,d);
  const auto rectangles=Rectangles(m,a.rectangles,a.count);
  if(a.color)m.Read(a.color&~15u,16);
  host.Flush(d);
  uint32_t width=0,height=0;
  m.W(d+12740,a.count);
  for(uint32_t i=0;i<a.count;++i){
    const auto r=rectangles[i];width=std::max(width,r.x2);height=std::max(height,r.y2);
    m.Rect(d+12744+i*16,r);m.W(d+12984+i*8,r.x1&~31u);m.W(d+12988+i*8,r.y1&~31u);
  }
  host.Tiles(rectangles,width,height);
  if(a.flags&2){
    m.B(d+10943,m.B(d+10943)|0x20|((a.flags&4)?0x10:0));
    const auto samples=(m.W(d+10368)>>16)&3;
    const uint32_t block_x=samples<2?32:16,block_y=samples?8:16,unit=samples<2?80:40;
    const uint32_t pitch=(width+block_x-1)&~(block_x-1);
    for(uint32_t i=0;i<a.count;++i)
      m.W(d+13104+i*4,(m.W(d+12988+i*8)/block_y)*(pitch/block_x)+m.W(d+12984+i*8)/block_x);
    m.W(d+13164,pitch);m.W(d+13168,((width+unit-1)/unit)*unit);
    m.W(d+10368,(m.W(d+10368)&0x3FFFF)|(pitch<<18));
    m.D(d+16,m.D(d+16)|256);
    if(!(m.B(d+10943)&0x20)&&(m.W(d+11844)&0xE0000)==0x40000)m.D(d+40,m.D(d+40)&~uint64_t(256));
    else m.D(d+40,m.D(d+40)|256);
  }
  m.W(d+13172,width);m.W(d+13176,height);m.W(d+13180,a.flags);
  if(!(a.flags&1)&&!(m.B(d+10943)&0x20)){
    uint32_t clear_width=0,clear_height=0;
    for(uint32_t i=0;i<a.count;++i){
      clear_width=std::max(clear_width,rectangles[i].x2-m.W(d+12984+i*8));
      clear_height=std::max(clear_height,rectangles[i].y2-m.W(d+12988+i*8));
    }
    std::array<uint8_t,28> viewport;std::array<uint8_t,16> scissor;
    std::ranges::copy(m.At(d+12640,28),viewport.begin());std::ranges::copy(m.At(d+12668,16),scissor.begin());
    host.FullViewport(d);host.FullScissor(d);
    host.Clear(d,{0,0,clear_width,clear_height},a.color,a.depth,a.stencil,true);
    host.RestoreViewport(d,viewport);host.RestoreScissor(d,scissor);
  }
  if(a.color)std::memmove(m.At(d+13184,16).data(),m.Read(a.color&~15u,16).data(),16);
  else std::ranges::fill(m.At(d+13184,16),0);
  m.W(d+13200,std::bit_cast<uint32_t>(a.depth));m.W(d+13204,a.stencil);
  const auto target=m.W(d+12432);m.W(d+12716,target?target:m.W(d+12448));
  for(uint32_t i=0;i<5;++i)m.W(d+12720+i*4,m.W(d+12432+i*4));
  m.W(d+12712,2);m.B(d+10940,m.B(d+10940)|0x20);ResetPredication(m,d);
  host.BeginSegment(d);host.FullViewport(d);host.ResetState(d,true);
}

uint32_t EndTiling(TilingHost &host,const EndTiles &a){
  Memory m{host};const auto d=a.device;Device(m,d);
  const uint32_t count=m.W(d+12740);
  if(count>max_tiles)throw std::invalid_argument("TimeShift tile count is outside device storage");
  const auto rectangles=a.destination?Rectangles(m,a.rectangles?a.rectangles:d+12744,count):std::vector<TileRect>{};
  if(a.destination&&count)host.ValidateResolveParameters(a.parameters);
  uint32_t size=0;
  if(a.destination&&count)size=m.W(m.W(d+12716)+36);
  host.Flush(d);
  for(uint32_t i=0;i<rectangles.size();++i){
    auto r=rectangles[i];const auto origin=r;
    if(r.x2==(size>>18)+1)r.x2=(r.x2+7)&~7u;
    if(r.y2==((size>>3)&0x7FFF)+1)r.y2=(r.y2+7)&~7u;
    SetTilePredication(host,d,3u<<(i*2));host.HiZ(d,((~m.B(d+10943))>>4)&2);
    TileResolve resolve{a.flags,a.destination,r,origin.x1,origin.y1,a.color,a.depth,a.stencil,a.parameters};
    if(i+1<count){
      if(m.W(d+12432))resolve.flags|=0x100;
      if(m.W(d+12448))resolve.flags|=0x200;
      resolve.color=d+13184;resolve.depth=std::bit_cast<float>(m.W(d+13200));
      resolve.stencil=m.W(d+13204);resolve.parameters=0;
    }
    host.Resolve(d,resolve);
  }
  SetTilePredication(host,d,0);host.HiZ(d,2);
  const uint32_t begin_flags=m.W(d+13180)&0x3F000000;
  const uint32_t shift=m.W(d+11312)&63;
  const uint32_t submit_flags=begin_flags?begin_flags:shift<32?0x01000000u<<shift:0;
  m.W(d+12716,0);m.B(d+10940,m.B(d+10940)&0xDF);m.B(d+10943,m.B(d+10943)&0xCF);
  ResetPredication(m,d);
  const bool out_of_memory=host.EndSegment(d,count,submit_flags);
  host.FullViewport(d);host.Tiles({},0,0);
  if(out_of_memory){host.ResetState(d,false);return 0x8007000E;}
  return 0;
}
}
