#include "device_bootstrap.h"
#include <cstdio>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace timeshift::d3d6274 {
namespace {
uint32_t Read(std::span<std::byte> bytes, uint32_t offset = 0) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value = (value << 8) | std::to_integer<uint8_t>(bytes[offset + i]);
  return value;
}
void Write(std::span<std::byte> bytes, uint32_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = std::byte((value >> (24 - i * 8)) & 255);
}
uint32_t Call(Host& host, uint32_t address, std::initializer_list<uint32_t> words) {
  return host.InvokeOriginal(address, std::span(words.begin(), words.size()));
}
bool ValidRange(uint32_t address, uint32_t bytes) {
  return address && bytes && uint64_t(address) + bytes <= uint64_t{1} << 32;
}
bool Overlap(uint32_t a, uint32_t a_bytes, uint32_t b, uint32_t b_bytes) {
  return uint64_t(a) < uint64_t(b) + b_bytes && uint64_t(b) < uint64_t(a) + a_bytes;
}
void Rewind(std::span<std::byte> device, const BootstrapMemory& memory) {
  Write(device, offset::ring, memory.ring);
  Write(device, offset::ring_limit, memory.ring + memory.ring_bytes);
  Write(device, offset::ring_guarantee, memory.ring + memory.ring_bytes - memory.guarantee_margin);
}
}

BootstrapResult BootstrapDevice(Host& host, const CreateArguments& args,
                                const BootstrapMemory& memory) {
  auto output = host.Map(args.out_device, 4);
  if (output.size() != 4) {
    std::fprintf(stderr, "[bootstrap] output map failed: out_device=0x%08X\n", args.out_device);
    std::fflush(stderr);
    return {kInvalidArgument, 0};
  }
  Write(output, 0, 0);
  const bool title = args.device_type != kNoHardwareType;
  if (!ValidRange(memory.ring, memory.ring_bytes) || memory.ring % 128 ||
      uint64_t(memory.ring) + memory.ring_bytes >= uint64_t{1} << 32 ||
      memory.guarantee_margin < 160 || memory.guarantee_margin >= memory.ring_bytes ||
      memory.ring_bytes % 32 || memory.guarantee_margin % 32 ||
      !ValidRange(memory.fence, memory.fence_bytes) || memory.fence % 128 || memory.fence_bytes < 132 ||
      Overlap(memory.ring, memory.ring_bytes, memory.fence, memory.fence_bytes) ||
      Overlap(memory.ring, memory.ring_bytes, args.out_device, 4) ||
      Overlap(memory.fence, memory.fence_bytes, args.out_device, 4)) {
    std::fprintf(stderr, "[bootstrap] memory range validation failed\n");
    std::fflush(stderr);
    return {kInvalidArgument, 0};
  }
  auto ring = host.Map(memory.ring, memory.ring_bytes);
  auto fence = host.Map(memory.fence, memory.fence_bytes);
  auto global_slot = title ? host.Map(kGlobalDeviceImport, 4) : std::span<std::byte>{};
  if (ring.size() != memory.ring_bytes || fence.size() != memory.fence_bytes ||
      (title && (global_slot.size() != 4 || !args.presentation_parameters ||
                 host.Map(args.presentation_parameters, 124).size() != 124))) {
    std::fprintf(stderr, "[bootstrap] ring/fence/global_slot map failed (title=%d, global_slot.size=%zu)\n",
                 int(title), global_slot.size());
    std::fflush(stderr);
    return {kInvalidArgument, 0};
  }
  auto global = title ? host.Map(Read(global_slot), 4) : std::span<std::byte>{};
  const float frequency = title ? static_cast<float>(host.PerformanceFrequency()) : 0.0f;
  if (title && (global.size() != 4 || !std::isfinite(frequency) || frequency <= 0.0f ||
                Overlap(memory.ring, memory.ring_bytes, args.presentation_parameters, 124) ||
                Overlap(memory.fence, memory.fence_bytes, args.presentation_parameters, 124))) {
    std::fprintf(stderr, "[bootstrap] title global/frequency validation failed: global.size=%zu, freq=%f\n",
                 global.size(), frequency);
    std::fflush(stderr);
    return {kInvalidArgument, 0};
  }
  std::fill(ring.begin(), ring.end(), std::byte{});
  std::fill(fence.begin(), fence.end(), std::byte{});

  const uint32_t result = Call(host, kCreateDevice,
      {args.adapter, kNoHardwareType, args.reserved, args.behavior_flags,
       args.presentation_parameters, args.out_device});
  if (result != 0) {
    std::fprintf(stderr, "[bootstrap] Call(kCreateDevice) returned 0x%08X\n", result);
    std::fflush(stderr);
    return {result, 0};
  }
  const uint32_t address = Read(output);
  auto device = host.Map(address, kDeviceBytes);
  if (!address || address % 128 || device.size() != kDeviceBytes) {
    std::fprintf(stderr, "[bootstrap] created device invalid: address=0x%08X, size=%zu\n", address, device.size());
    std::fflush(stderr);
    // An invalid original result is an integration-contract violation. Do not
    // dereference it or call the original destructor with an invalid address.
    Write(output, 0, 0);
    return {kOutOfMemory, 0};
  }
  device[offset::flags1] = (device[offset::flags1] & std::byte{0xDF}) | std::byte{2};
  Write(device, offset::fence_completion, memory.fence);
  Write(device, offset::presentation_scratch, memory.fence + 128);
  Rewind(device, memory);
  // CDevice ring initialization 8264F9B4..8264F9E0: zero becomes tag3,
  // completion starts two behind. Tag0 is the public "no fence" sentinel.
  uint32_t tag = Read(device, 10908);
  if (!tag) tag = 3;
  Write(device, 10908, tag);
  Write(fence, 0, tag - 2);
  Write(fence, 4, memory.ring | (Read(device, 14904) & 3));
  if (!host.DeviceBound(address)) {
    Write(device, offset::ring, 0); Write(device, offset::ring_limit, 0);
    Write(device, offset::ring_guarantee, 0); Write(device, offset::fence_completion, 0);
    Write(device, offset::presentation_scratch, 0);
    Write(output, 0, 0);
    Call(host, kReleaseDevice, {address});
    return {kOutOfMemory, 0};
  }
  if (!title)
    return {0, address};

  Call(host, kInitializeCriticalSection, {address + offset::critical_section0});
  Call(host, kInitializeCriticalSection, {address + offset::critical_section1});
  const uint32_t previous_global = Read(global);
  Write(global, 0, address);
  uint32_t flags = args.behavior_flags;
  if (!(flags & 0x100) && !(flags & 0x3F000000))
    flags |= 0x0C000000;
  Write(device, offset::behavior_flags, flags);
  Write(device, offset::hardware_sentinel0, 0xFFFFFFFF);
  Write(device, offset::hardware_sentinel1, 0xFFFFFFFF);
  if (!Call(host, kInitializePresentation, {address, args.presentation_parameters})) {
    // Restore the global/output before a destructor can free/reuse the VA or
    // run callbacks. A source owner may retain a failed device for safe retry.
    Write(global, 0, previous_global);
    Write(output, 0, 0);
    if (host.BeforeFailedRelease(address)) Call(host, kReleaseDevice, {address});
    return {kOutOfMemory, 0};
  }
  Call(host, kInitializeApiState, {address});
  Write(device, offset::performance_frequency, std::bit_cast<uint32_t>(frequency));
  Write(device, offset::performance_frequency + 4, std::bit_cast<uint32_t>(1.0f / frequency));
  Rewind(device, memory);
  // Later render-thread fence completion replaces this temporary CPU mode.
  device[offset::flags1] &= std::byte{0xFD};
  std::fprintf(stderr, "[bootstrap] BootstrapDevice SUCCESS: address=0x%08X\n", address);
  std::fflush(stderr);
  return {0, address};
}
}  // namespace timeshift::d3d6274
