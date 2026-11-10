#pragma once
#include <array>
#include <cstdint>

namespace timeshift::renderer {
// ClearF 8264A810 normalizes a null rectangle BEFORE CoreClear. Tiling state
// lives beyond the queued device snapshot, so capture the effective extent now.
template<class Word, class Byte>
std::array<int32_t,4> DefaultClearRectangle(uint32_t device, Word word, Byte byte) {
  const uint32_t target=word(device+12432);
  const uint32_t surface=target?target:word(device+12448);
  const uint32_t size=word(surface+36);
  int32_t width=((size>>18)&0x3FFF)+1;
  int32_t height=((size>>3)&0x7FFF)+1;
  const auto flags=byte(device+10940);
  bool tiling=(flags&0x10)!=0;
  if(!tiling&&(flags&0x20)){
    tiling=true;
    for(uint32_t i=0;i<5;++i){
      const uint32_t bound=word(device+12432+4*i);
      if(bound&&bound!=word(device+12720+4*i)){tiling=false;break;}
    }
  }
  if(tiling){width=int32_t(word(device+13172));height=int32_t(word(device+13176));}
  return {0,0,width,height};
}
}
