// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <cstdint>
namespace thps::native {
struct AliasRect {int32_t x0=0,y0=0,x1=0,y1=0;};
struct AliasLayout {uint32_t base=0,pitch=0,msaa=0,width=0,height=0;};
inline uint32_t AliasScaleX(uint32_t msaa){return msaa>=2?2:1;}
inline uint32_t AliasScaleY(uint32_t msaa){return msaa>=1?2:1;}
inline uint32_t AliasPitchTiles(const AliasLayout& a){return (a.pitch*AliasScaleX(a.msaa)+79)/80;}
// Both Xenos D24 variants encode depth=0/stencil=0 as an all-zero word.
// Float24 uses a 0..0.5 host viewport; scaling zero still produces zero.
// The caller additionally proves W=1/Z=0, no pixel shader or depth bias.
inline bool IsZeroAliasDepthTransform(uint32_t format,float min_depth,float max_depth,
                                     float ndc_scale,float ndc_offset){
 return format<=1&&min_depth==0&&std::isfinite(max_depth)&&max_depth>=0&&max_depth<=1&&
        std::isfinite(ndc_scale)&&ndc_scale>=0&&ndc_offset==0;
}
// A uniform word survives the depth/color tile-column permutation. Only
// whole physical 80x16-sample tiles with the same pitch/base can be mirrored.
inline bool MapZeroAliasClear(const AliasLayout& source,const AliasLayout& dest,
                              AliasRect rect,AliasRect& out){
 out={};
 if(source.msaa>2||dest.msaa>2||!source.pitch||!dest.pitch||source.base!=dest.base||
    AliasPitchTiles(source)!=AliasPitchTiles(dest))return false;
 rect.x0=std::clamp(rect.x0,0,int32_t(source.width));rect.x1=std::clamp(rect.x1,0,int32_t(source.width));
 rect.y0=std::clamp(rect.y0,0,int32_t(source.height));rect.y1=std::clamp(rect.y1,0,int32_t(source.height));
 if(rect.x0>=rect.x1||rect.y0>=rect.y1)return false;
 int64_t x0=int64_t(rect.x0)*AliasScaleX(source.msaa),x1=int64_t(rect.x1)*AliasScaleX(source.msaa);
 int64_t y0=int64_t(rect.y0)*AliasScaleY(source.msaa),y1=int64_t(rect.y1)*AliasScaleY(source.msaa);
 if(x0%80||x1%80||y0%16||y1%16||source.base+(y1/16-1)*AliasPitchTiles(source)+x1/80>2048)return false;
 const int64_t dx=AliasScaleX(dest.msaa),dy=AliasScaleY(dest.msaa);
 x0/=dx;x1/=dx;y0/=dy;y1/=dy;
 if(x0<0||y0<0||x1>dest.width||y1>dest.height)return false;
 out={int32_t(x0),int32_t(y0),int32_t(x1),int32_t(y1)};return true;
}
// Quantize constant D24 clears to their stored UNORM word. Float24 nonzero packing
// and arbitrary transformed Z are deliberately outside this narrow mirror.
inline bool PackConstantAliasClear(uint32_t format,float depth,uint32_t stencil,
                                  float min_depth,float max_depth,float ndc_scale,
                                  float ndc_offset,uint32_t& word){
 word=0;
 if(stencil>255||!IsZeroAliasDepthTransform(format,min_depth,max_depth,ndc_scale,ndc_offset)||
    !std::isfinite(depth)||depth<0||depth>1)return false;
 if(depth==0){word=stencil;return true;}
 if(format!=0||max_depth!=1||ndc_scale!=1)return false;
 // D3D uses deliberately biased floats for clears (run180: 7.566065e-6).
 // The attachment stores the quantized value, not an exact float roundtrip.
 // Keep exact half-way cases out until the guest rounding mode is modeled.
 double scaled=double(depth)*16777215.0;
 if(scaled-std::floor(scaled)==.5)return false;
 uint32_t n=uint32_t(std::floor(scaled+.5));
 word=(n<<8)|stencil;return true;
}
inline std::array<float,4> AliasClearColor(uint32_t word){
 return {float(word&255)/255,float((word>>8)&255)/255,
         float((word>>16)&255)/255,float(word>>24)/255};
}
// The vertex interpreter supplies final exports. W=1 and Z=0 make the
// constant depth proof independent of reciprocal/predivided vertex formats.
inline bool ProveZeroClearRect(const std::array<std::array<float,4>,3>& p,
                              float sx,float sy,float ox,float oy,
                              const AliasRect& clip,AliasRect& rect){
 std::array<float,3> x{},y{};rect={};
 for(size_t i=0;i<3;++i){
  if(p[i][3]!=1||p[i][2]!=0)return false;
  x[i]=p[i][0]*sx+ox;y[i]=p[i][1]*sy+oy;
  if(!std::isfinite(x[i])||!std::isfinite(y[i])||std::abs(x[i])>16384||std::abs(y[i])>16384||
     x[i]!=std::round(x[i])||y[i]!=std::round(y[i]))return false;
 }
 auto [xmin,xmax]=std::minmax_element(x.begin(),x.end());auto [ymin,ymax]=std::minmax_element(y.begin(),y.end());
 if(*xmin>=*xmax||*ymin>=*ymax)return false;
 uint32_t corners=0;
 for(size_t i=0;i<3;++i){if((x[i]!=*xmin&&x[i]!=*xmax)||(y[i]!=*ymin&&y[i]!=*ymax))return false;
   corners|=1u<<(uint32_t(x[i]==*xmax)|(uint32_t(y[i]==*ymax)<<1));}
 if(std::popcount(corners)!=3)return false;
 rect={std::max(int32_t(*xmin),clip.x0),std::max(int32_t(*ymin),clip.y0),
       std::min(int32_t(*xmax),clip.x1),std::min(int32_t(*ymax),clip.y1)};
 return rect.x0<rect.x1&&rect.y0<rect.y1;
}
inline bool ProveConstantClearRect(const std::array<std::array<float,4>,3>& p,
                                  float sx,float sy,float ox,float oy,
                                  const AliasRect& clip,AliasRect& rect,float& depth){
 depth=p[0][2];
 if(!std::isfinite(depth)||p[1][2]!=depth||p[2][2]!=depth)return false;
 auto zero=p;for(auto& v:zero)v[2]=0;
 return ProveZeroClearRect(zero,sx,sy,ox,oy,clip,rect);
}
} // namespace thps::native
