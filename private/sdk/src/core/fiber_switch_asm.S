// void rex_switch_context(SwitchContext* from /* x0 */, SwitchContext* to /* x1 */);
//
// Offsets must match struct SwitchContext in fiber_switch.h:
//   x19..x28  0..79   fp 80   lr 88   sp 96   d8..d15 104..167

.text
.global rex_switch_context
.type rex_switch_context, %function
rex_switch_context:
	stp x19, x20, [x0, #0]
	stp x21, x22, [x0, #16]
	stp x23, x24, [x0, #32]
	stp x25, x26, [x0, #48]
	stp x27, x28, [x0, #64]
	stp x29, x30, [x0, #80]
	mov x2, sp
	str x2, [x0, #96]
	stp d8,  d9,  [x0, #104]
	stp d10, d11, [x0, #120]
	stp d12, d13, [x0, #136]
	stp d14, d15, [x0, #152]

	ldp x19, x20, [x1, #0]
	ldp x21, x22, [x1, #16]
	ldp x23, x24, [x1, #32]
	ldp x25, x26, [x1, #48]
	ldp x27, x28, [x1, #64]
	ldp x29, x30, [x1, #80]
	ldr x2, [x1, #96]
	mov sp, x2
	ldp d8,  d9,  [x1, #104]
	ldp d10, d11, [x1, #120]
	ldp d12, d13, [x1, #136]
	ldp d14, d15, [x1, #152]

	ret
.size rex_switch_context, . - rex_switch_context

// First entry of a new fiber: x19 = entry function, x20 = its argument
// (set up by Fiber::Create in fiber_switch.cpp).
.global rex_fiber_trampoline
.type rex_fiber_trampoline, %function
rex_fiber_trampoline:
	mov x0, x20
	blr x19
	// A fiber entry is not supposed to return; report it instead of jumping into garbage.
	bl rex_fiber_returned
	brk #0
.size rex_fiber_trampoline, . - rex_fiber_trampoline
