#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace timeshift::d3d6274 {

// Static evidence: the owned title's code. Runtime verification is pending.
inline constexpr uint32_t kCreateDevice = 0x826478E0;
inline constexpr uint32_t kInitializePresentation = 0x826539A0;
inline constexpr uint32_t kInitializeApiState = 0x82653738;
inline constexpr uint32_t kReleaseDevice = 0x82647600;
inline constexpr uint32_t kInitializeCriticalSection = 0x82B04604;
inline constexpr uint32_t kGlobalDeviceImport = 0x82000914;
inline constexpr uint32_t kDeviceBytes = 22400;
inline constexpr uint32_t kNoHardwareType = 2;
inline constexpr uint32_t kOutOfMemory = 0x8007000E;
inline constexpr uint32_t kInvalidArgument = 0x80070057;

namespace offset {
inline constexpr uint32_t ring = 48;
inline constexpr uint32_t ring_limit = 52;
inline constexpr uint32_t ring_guarantee = 56;
inline constexpr uint32_t fence_completion = 10896;
inline constexpr uint32_t presentation_scratch = 10900;
inline constexpr uint32_t flags = 10940;
inline constexpr uint32_t flags1 = 10941;
inline constexpr uint32_t critical_section0 = 14928;
inline constexpr uint32_t critical_section1 = 14956;
inline constexpr uint32_t hardware_sentinel0 = 21540;
inline constexpr uint32_t hardware_sentinel1 = 21544;
inline constexpr uint32_t performance_frequency = 21568;
inline constexpr uint32_t behavior_flags = 22280;
}

// InvokeOriginal must bypass replacement hooks, preserve the caller's PowerPC
// stack/nonvolatile state, and marshal these words to r3 onward. This interface
// deliberately has no dependency on a particular upstream renderer or SDK ABI.
class Host {
 public:
  virtual ~Host() = default;
  virtual std::span<std::byte> Map(uint32_t address, size_t bytes) = 0;
  virtual uint32_t InvokeOriginal(uint32_t address, std::span<const uint32_t> words) = 0;
  virtual uint64_t PerformanceFrequency() = 0;
  // Publish the owned device BEFORE presentation/API initialization can call
  // its allocator hooks. Abort may defer destruction while sources are busy.
  virtual bool DeviceBound(uint32_t) { return true; }
  virtual bool BeforeFailedRelease(uint32_t) { return true; }
};

struct CreateArguments {
  uint32_t adapter;
  uint32_t device_type;
  uint32_t reserved;
  uint32_t behavior_flags;
  uint32_t presentation_parameters;
  uint32_t out_device;
};

// Borrowed guest allocations; their owner must retain them for the device's
// entire lifetime. Device release and render-thread retirement are separate.
struct BootstrapMemory {
  uint32_t ring;
  uint32_t ring_bytes;
  uint32_t guarantee_margin;
  uint32_t fence;
  uint32_t fence_bytes;
};

struct BootstrapResult {
  uint32_t hresult;
  uint32_t device;
};

// Isolated CPU bootstrap only. Do not install it as a live hook until the draw,
// tiling, resource, shader and fence integration is complete. No GPU is started.
BootstrapResult BootstrapDevice(Host& host, const CreateArguments& args,
                                const BootstrapMemory& memory);

}  // namespace timeshift::d3d6274
