#pragma once
#include "source_memory.h"
namespace timeshift::renderer {
// Invoke only AFTER source/callback quiescence and a real GPU-idle barrier.
// False is a conservative preflight refusal and makes no guest mutations.
bool ReleaseGuestDeviceBindings(PPCContext &context,uint8_t *base,SourceRuntime &memory,uint32_t device);
std::unique_ptr<DeviceCleanup> MakeSdkDeviceCleanup(uint32_t device);
}
