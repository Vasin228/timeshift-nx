/**
 * @file        core/seh_switch.cpp
 * @brief       Switch (Horizon OS) SEH stub implementation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/platform.h>
#include <rex/platform/seh.h>

#include <atomic>
#include <cstdlib>

namespace rex::platform {

static thread_local SehThreadState tls_seh_state;
static thread_local bool tls_seh_active = false;

SehThreadState& seh_thread_state() {
  return tls_seh_state;
}

int seh_filter(uint32_t /*code*/, void* /*ep*/) {
  return 0;
}

[[noreturn]] void seh_rethrow() {
  std::abort();
}

void seh_initialize() {
  g_seh_initialized.store(true, std::memory_order_relaxed);
}

bool& seh_active() {
  return tls_seh_active;
}

}  // namespace rex::platform
