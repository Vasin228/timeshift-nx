#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace timeshift::renderer {
struct TileRect {uint32_t x1,y1,x2,y2;};
struct TileResolve {
  uint32_t flags,destination;
  TileRect rectangle;
  uint32_t x,y,color;
  float depth;
  uint32_t stencil,parameters;
};
struct BeginTiles {
  uint32_t device,flags,count,rectangles,color;
  float depth;
  uint32_t stencil;
};
struct EndTiles {
  uint32_t device,flags,rectangles,destination,color;
  float depth;
  uint32_t stencil,parameters;
};

// Required services, not optional no-ops. The concrete SDK host supplies source
// ownership and retained Clear/Resolve CPU calls. Production hooks stay dormant.
class TilingHost {
 public:
  virtual ~TilingHost()=default;
  virtual std::span<uint8_t> Map(uint32_t address,uint32_t bytes)=0;
  virtual std::span<const uint8_t> Read(uint32_t address,uint32_t bytes){return Map(address,bytes);}
  virtual void Flush(uint32_t device)=0;
  virtual void Tiles(std::span<const TileRect> rectangles,uint32_t width,uint32_t height)=0;
  virtual void FullViewport(uint32_t device)=0;
  virtual void RestoreViewport(uint32_t device,std::span<const uint8_t,28> bytes)=0;
  virtual void FullScissor(uint32_t device)=0;
  virtual void RestoreScissor(uint32_t device,std::span<const uint8_t,16> bytes)=0;
  virtual void Clear(uint32_t device,TileRect rect,uint32_t color,float depth,uint32_t stencil,
                     bool whole_host_target)=0;
  virtual void Resolve(uint32_t device,const TileResolve &resolve)=0;
  // Must validate parameters before ANY End mutation/queue submission.
  virtual void ValidateResolveParameters(uint32_t parameters)=0;
  virtual void HiZ(uint32_t device,uint32_t value)=0;
  virtual void ResetState(uint32_t device,bool begin)=0;
  virtual void BeginSegment(uint32_t device)=0;
  // Return the original out-of-memory status. Complete source ownership via
  // host work/callback acknowledgements; never invoke worker82652698 here.
  virtual bool EndSegment(uint32_t device,uint32_t tile_count,uint32_t flags)=0;
};

void BeginTiling(TilingHost &host,const BeginTiles &args);
uint32_t EndTiling(TilingHost &host,const EndTiles &args);
void SetTilePredication(TilingHost &host,uint32_t device,uint32_t mask);
}
