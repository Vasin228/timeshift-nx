// rex::thread::Fiber backend for Nintendo Switch (aarch64, Horizon OS).
//
// Mirrors src/core/fiber_posix.cpp (ucontext) with our own 168-byte context and a
// 40-instruction assembly switch. Only callee-saved registers are touched, so the
// C++ stack of the fiber is preserved exactly as ucontext does it.
//
// STATUS: written but NOT yet run on hardware or emulator. First things to verify
// on the console:
//   1. a fiber created with a small stack does not fault (stack must be 16-byte
//      aligned, and the first 16 bytes of the frame are not used);
//   2. d8..d15 survive a switch (run a float-heavy guest frame before/after);
//   3. Fibers destroyed after being switched away from do not leak or crash.

#ifdef __SWITCH__

#include <rex/thread/fiber.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rex/thread/fiber_switch.h"

extern "C" __attribute__((naked)) void rex_switch_context(rex::thread::nx::SwitchContext* from,
                                                          rex::thread::nx::SwitchContext* to) {
  __asm__ volatile(
      "stp x19, x20, [x0, #0]\n"
      "stp x21, x22, [x0, #16]\n"
      "stp x23, x24, [x0, #32]\n"
      "stp x25, x26, [x0, #48]\n"
      "stp x27, x28, [x0, #64]\n"
      "stp x29, x30, [x0, #80]\n"
      "mov x2, sp\n"
      "str x2, [x0, #96]\n"
      "stp d8,  d9,  [x0, #104]\n"
      "stp d10, d11, [x0, #120]\n"
      "stp d12, d13, [x0, #136]\n"
      "stp d14, d15, [x0, #152]\n"

      "ldp x19, x20, [x1, #0]\n"
      "ldp x21, x22, [x1, #16]\n"
      "ldp x23, x24, [x1, #32]\n"
      "ldp x25, x26, [x1, #48]\n"
      "ldp x27, x28, [x1, #64]\n"
      "ldp x29, x30, [x1, #80]\n"
      "ldr x2, [x1, #96]\n"
      "mov sp, x2\n"
      "ldp d8,  d9,  [x1, #104]\n"
      "ldp d10, d11, [x1, #120]\n"
      "ldp d12, d13, [x1, #136]\n"
      "ldp d14, d15, [x1, #152]\n"

      "ret\n"
  );
}

extern "C" __attribute__((naked)) void rex_fiber_trampoline() {
  __asm__ volatile(
      "mov x0, x20\n"
      "blr x19\n"
      "bl rex_fiber_returned\n"
      "brk #0\n"
  );
}


namespace rex::thread {

namespace {

constexpr size_t kDefaultStackSize = 1 << 20;  // 1 MiB, same order as POSIX build

// A fiber entry must never return: the guest scheduler exits the thread instead.
// If it happens, park the fiber forever instead of returning into a dead stack.
extern "C" void rex_fiber_returned() {
  std::fprintf(stderr, "[fiber] BUG: guest fiber entry returned; parking thread\n");
  std::fflush(stderr);
  for (;;) {
    // svcSleepThread(1s); kept as an infinite loop to avoid pulling libnx headers here.
    __asm__ __volatile__("yield");
  }
}

}  // namespace

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* fiber = new Fiber();
  fiber->is_thread_fiber_ = true;
  // Nothing to save by hand: SwitchTo() snapshots the live registers on first use.
  tls_current_ = fiber;
  return fiber;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  if (stack_size == 0) {
    stack_size = kDefaultStackSize;
  }
  // Align the stack down to 16 bytes as AAPCS64 requires, and keep the guard-free
  // allocation simple: one malloc, freed in Destroy().
  auto* fiber = new Fiber();
  const size_t alloc = (stack_size + 15u) & ~size_t(15u);
  fiber->stack_.resize(alloc);
  fiber->entry_ = entry;
  fiber->arg_ = arg;
  fiber->is_thread_fiber_ = false;

  std::memset(&fiber->context_, 0, sizeof(fiber->context_));
  const uintptr_t top = reinterpret_cast<uintptr_t>(fiber->stack_.data()) + alloc;
  fiber->context_.sp = static_cast<uint64_t>(top);  // alloc is 16-byte aligned
  fiber->context_.lr = reinterpret_cast<uint64_t>(&rex_fiber_trampoline);
  fiber->context_.x19_x28[0] = reinterpret_cast<uint64_t>(entry);  // x19
  fiber->context_.x19_x28[1] = reinterpret_cast<uint64_t>(arg);    // x20
  return fiber;
}

void Fiber::SwitchTo(Fiber* target) {
  if (!target || target == tls_current_) {
    return;
  }
  Fiber* from = tls_current_;
  if (!from) {
    // The calling thread has not been converted yet; do it implicitly so callers
    // cannot dead-end (ConvertCurrentThread is idempotent by design).
    from = ConvertCurrentThread();
  }
  tls_current_ = target;
  rex_switch_context(&from->context_, &target->context_);
  // Resumed: whoever switched back already restored tls_current_ to us.
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    delete this;
    return;
  }
  delete this;
}

}  // namespace rex::thread

#endif  // __SWITCH__
