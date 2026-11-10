// Switch (aarch64) context for rex::thread::Fiber.
//
// Horizon OS has no <ucontext.h> and libnx has no fiber primitive, so the runtime
// carries its own minimal context switch. Only the AAPCS64 callee-saved state has to
// be preserved: x19..x28, x29/fp, x30/lr, sp and d8..d15. Everything else is scratch
// by the call convention, so the C++ stack survives a switch untouched.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rex::thread::nx {

struct SwitchContext {
  uint64_t x19_x28[10];  // x19..x28   (offset   0, 80 bytes)
  uint64_t fp;           // x29         (offset  80)
  uint64_t lr;           // x30         (offset  88)
  uint64_t sp;           // stack ptr   (offset  96)
  uint64_t d8_d15[8];    // d8..d15     (offset 104, 64 bytes)
};

static_assert(offsetof(SwitchContext, fp) == 80, "layout is hand-written in the asm");
static_assert(offsetof(SwitchContext, sp) == 96, "layout is hand-written in the asm");
static_assert(offsetof(SwitchContext, d8_d15) == 104, "layout is hand-written in the asm");
static_assert(sizeof(SwitchContext) == 168, "the .S file stores to these exact offsets");

}  // namespace rex::thread::nx

extern "C" {
// Saves the current callee-saved state into `from` and restores `to`.
// Implemented in fiber_switch_asm.S (aarch64 only).
void rex_switch_context(rex::thread::nx::SwitchContext* from,
                        rex::thread::nx::SwitchContext* to);
}
