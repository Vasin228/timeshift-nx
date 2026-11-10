#pragma once
#include "device_bootstrap.h"
struct PPCContext;

namespace timeshift::d3d6274 {
struct GuestMapping {
  void *user = nullptr;
  std::span<std::byte> (*map)(void *user, uint32_t address, size_t bytes) = nullptr;
  bool (*bound)(void *user, uint32_t device) = nullptr;
  bool (*before_failed_release)(void *user, uint32_t device) = nullptr;
};
// Real SDK ABI bridge. Memory is borrowed from the future lifecycle owner, not
// allocated/shared implicitly between devices. No CreateDevice hook is installed.
BootstrapResult BootstrapGuestDevice(PPCContext &context, uint8_t *base,
                                    const BootstrapMemory &memory, GuestMapping mapping);
}
