#include "tiling_guest_calls.h"
#include "tiling.h"
#include <rex/ppc.h>

namespace timeshift::renderer {
void BeginGuestTiling(PPCContext &c,TilingHost &host){
  BeginTiling(host,{c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,float(c.f1.f64),c.r9.u32});
}
void EndGuestTiling(PPCContext &c,TilingHost &host){
  c.r3.u64=EndTiling(host,{c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,float(c.f1.f64),c.r9.u32,c.r10.u32});
}
}
