#pragma once

#include <cstdint>
#include <rex/ppc.h>

namespace timeshift::renderer {

bool PresentationHooksEnabled();

void NativeSwap(PPCContext& ctx, uint8_t* base);
void NativeSynchronizeToPresentationInterval(PPCContext& ctx, uint8_t* base);

}  // namespace timeshift::renderer
