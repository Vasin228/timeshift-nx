#pragma once
#include <cstdint>
#include "resolve_parameters.h"
struct PPCContext;
namespace timeshift::renderer {
class DeviceRegistry;
void NativeBeginTiling(PPCContext &context,uint8_t *base,DeviceRegistry &registry);
void NativeEndTiling(PPCContext &context,uint8_t *base,DeviceRegistry &registry);
void NativeSetPredication(PPCContext &context,uint8_t *base,DeviceRegistry &registry);
}
