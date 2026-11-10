/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <thread>

#include <rex/thread.h>

namespace rex::thread {

// =============================================================================
// Common code
// =============================================================================

uint32_t logical_processor_count() {
  static uint32_t value = 0;
  if (!value) {
#if defined(__SWITCH__)
    // std::thread::hardware_concurrency() returns 0 on Horizon OS because the
    // kernel does not expose CPU count via standard POSIX interfaces. Tegra X1
    // has 4 ARM Cortex-A57 cores. Horizon gives NRO applications access to
    // cores 0-3 (docked) or 0-2 (handheld). Hard-code 4 so that shader
    // compiler worker pools, pipeline caches, and thread affinity masks are
    // initialised correctly. Returning 0 causes GPU command processor to never
    // submit compiled shaders, resulting in a permanent black screen.
    value = 4;
#else
    value = std::thread::hardware_concurrency();
#endif
  }
  return value;
}

thread_local uint32_t current_thread_id_ = UINT_MAX;

uint32_t current_thread_id() {
  return current_thread_id_ == UINT_MAX ? current_thread_system_id() : current_thread_id_;
}

void set_current_thread_id(uint32_t id) {
  current_thread_id_ = id;
}

}  // namespace rex::thread
