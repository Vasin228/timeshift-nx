// timeshift_thunk_adjustors.cpp
// Hand-written C++ equivalents of PowerPC vtable adjustor thunks.
// These thunks were skipped by the static recompiler because they are
// tiny (4-instruction) adjustor stubs — not full functions.
//
// Pattern (PPC assembly):
//   lwz  r11, -4(r3)         ; load adjustment delta from vtable[-1]
//   subf r3, r11, r3         ; r3 = r3 - r11 (this ptr base)
//   addi r3, r3, <OFFSET>    ; subtract fixed offset to correct base
//   b    <TARGET>            ; tail call real method
//
// The equivalent in the recomp framework:
//   ctx.r3.u32 -= REX_LOAD_U32(ctx.r3.u32 - 4);
//   ctx.r3.u32 += (int16_t)OFFSET;              // OFFSET is negative, so += negative
//   <TARGET>(ctx, base);

#include "timeshift_funcs.h"

// ---------------------------------------------------------------------------
// 0x825B96B8 -> sub_827F6040  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B96B8) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_827F6040(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B96D8 -> sub_825B9630  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B96D8) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825B9630(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B96E8 -> sub_825B9D28  (addi offset = -828)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B96E8) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-828);
    sub_825B9D28(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9708 -> sub_825B9608  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9708) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825B9608(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9718 -> sub_825BC7E0  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9718) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BC7E0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9748 -> sub_825BECE0  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9748) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BECE0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9788 -> sub_825B9658  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9788) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825B9658(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B97B0 -> sub_825BC720  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B97B0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BC720(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B97D0 -> sub_825BD648  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B97D0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BD648(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B97F0 -> sub_825BEC38  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B97F0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BEC38(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9820 -> sub_825BC958  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9820) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825BC958(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B9850 -> sub_825B9638  (addi offset = -12)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B9850) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-12);
    sub_825B9638(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B98B0 -> sub_825B8D40  (addi offset = 0)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B98B0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj;
    sub_825B8D40(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825C38F0 -> sub_825C3668  (addi offset = 0)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825C38F0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj;
    sub_825C3668(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x822BD0F8 (vtable trampoline: offset +280)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_822BD0F8) {
    REX_FUNC_PROLOGUE();
    ctx.r12.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = REX_LOAD_U32(ctx.r12.u32 + 280);
    ctx.ctr.u64 = ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// ---------------------------------------------------------------------------
// 0x822BD118 (vtable trampoline: offset +284)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_822BD118) {
    REX_FUNC_PROLOGUE();
    ctx.r12.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = REX_LOAD_U32(ctx.r12.u32 + 284);
    ctx.ctr.u64 = ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// ---------------------------------------------------------------------------
// 0x822BD138 (vtable trampoline: offset +288)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_822BD138) {
    REX_FUNC_PROLOGUE();
    ctx.r12.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = REX_LOAD_U32(ctx.r12.u32 + 288);
    ctx.ctr.u64 = ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// ---------------------------------------------------------------------------
// 0x82566990 (vtable trampoline: offset +228)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_82566990) {
    REX_FUNC_PROLOGUE();
    ctx.r12.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = REX_LOAD_U32(ctx.r12.u32 + 228);
    ctx.ctr.u64 = ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// ---------------------------------------------------------------------------
// 0x82566A00 (vtable trampoline: offset +224)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_82566A00) {
    REX_FUNC_PROLOGUE();
    ctx.r12.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
    ctx.r11.u64 = REX_LOAD_U32(ctx.r12.u32 + 224);
    ctx.ctr.u64 = ctx.r11.u64;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// ---------------------------------------------------------------------------
// 0x825B7050 -> sub_82385E30  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7050) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E30(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7090 -> sub_82385E28  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7090) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E28(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6F80 -> sub_82382D60  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6F80) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382D60(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B70D0 -> sub_82382D38  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B70D0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382D38(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7110 -> sub_82385E88  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7110) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E88(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7130 -> sub_82383028  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7130) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82383028(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6F70 -> sub_82385F18  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6F70) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385F18(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6F90 -> sub_82382F48  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6F90) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382F48(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6FB0 -> sub_82385E98  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6FB0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E98(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6FC0 -> sub_82385E20  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6FC0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E20(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6FD0 -> sub_82385EC8  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6FD0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385EC8(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B6FE0 -> sub_825B6BB8  (addi offset = 0)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B6FE0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj;
    sub_825B6BB8(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7010 -> sub_82385EB8  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7010) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385EB8(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7020 -> sub_82382BC0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7020) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382BC0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7030 -> sub_82386160  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7030) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82386160(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7040 -> sub_82385E78  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7040) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E78(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7060 -> sub_82385ED0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7060) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385ED0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7070 -> sub_82385E90  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7070) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385E90(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7080 -> sub_82385DC8  (addi offset = -736)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7080) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-736);
    sub_82385DC8(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B70A0 -> sub_82382D58  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B70A0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382D58(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B70B0 -> sub_82385EA8  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B70B0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385EA8(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B70C0 -> sub_82382FE0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B70C0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82382FE0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B70E0 -> sub_825B71C0  (addi offset = -736)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B70E0) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-736);
    sub_825B71C0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7100 -> sub_82385EA0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7100) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385EA0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7120 -> sub_82385EB0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7120) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82385EB0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7140 -> sub_823860B0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7140) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_823860B0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7150 -> sub_823860C0  (addi offset = -28)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7150) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r3.u32 - 4);
    ctx.r3.u32 = ctx.r3.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_823860C0(ctx, base);
}

// ---------------------------------------------------------------------------
// 0x825B7160 -> sub_82386140  (addi offset = -28, r4)
// ---------------------------------------------------------------------------
DEFINE_REX_FUNC(sub_825B7160) {
    REX_FUNC_PROLOGUE();
    uint32_t adj = REX_LOAD_U32(ctx.r4.u32 - 4);
    ctx.r4.u32 = ctx.r4.u32 - adj + (uint32_t)(int32_t)(-28);
    sub_82386140(ctx, base);
}



