#include "fence_read.h"
#include "device_registry.h"

namespace timeshift::renderer {
std::optional<uint32_t> ReadOwnedFenceCompletion(uint32_t device,uint32_t completion){
  if(!DeviceLifecycleHooksEnabled())return {};
  return DeviceRegistryRuntime().ReadOwnedCompletion(device,completion);
}
}
