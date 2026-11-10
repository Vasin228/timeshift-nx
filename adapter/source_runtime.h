#pragma once
#include "source_memory.h"
namespace timeshift::renderer {
// Concrete SDK heap + ReNut runtime integration, compiled but not installed.
// Must outlive every DeviceSources using it; invoke callbacks on a guest thread.
class SdkSourceRuntime final:public SourceRuntime {
 public:
  uint32_t Allocate(uint32_t bytes)override;
  bool Release(uint32_t address)override;
  std::span<uint8_t> Map(uint32_t address,uint32_t bytes)override;
  std::span<const uint8_t> Read(uint32_t address,uint32_t bytes)override;
  void FlushSources(uint32_t device)override;
  uint64_t LastCallback()override;
  void QueueCompletion(uint32_t address,uint32_t fence,uint64_t after_callback)override;
  uint32_t Completed(uint32_t address)override;
  void RunCallbacks()override;
  bool WaitProgress(std::chrono::milliseconds timeout)override;
  std::unique_ptr<DeviceCleanup> BeginDeviceCleanup(uint32_t device)override;
  void DeviceActivated(uint32_t device,bool title)override;
  void DeviceRetired(uint32_t device,bool title)override;
 private:
  std::span<uint8_t> MapChecked(uint32_t address,uint32_t bytes,bool writable);
};
}
