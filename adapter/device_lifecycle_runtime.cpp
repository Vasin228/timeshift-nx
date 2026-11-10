#include "device_registry.h"
#include "source_runtime.h"
#include "shader_runtime_bridge.h"
namespace timeshift::renderer {
bool DeviceLifecycleHooksEnabled(){return ResourceHooksEnabled();} // Production false.
DeviceRegistry &DeviceRegistryRuntime(){
  static SdkSourceRuntime runtime;
  static DeviceRegistry registry(runtime);
  return registry;
}
}
