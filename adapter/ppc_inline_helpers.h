#pragma once

#if defined(__SWITCH__) && defined(TIMESHIFT_RECOMP)

#include <cstdint>
#include <cstring>

namespace ts_ppc_inline {

[[gnu::always_inline]] inline void St64(uint8_t* __restrict base, uint32_t ea, uint64_t val) noexcept {
  const uint64_t be = __builtin_bswap64(val);
  std::memcpy(base + ea, &be, sizeof(be));
}

[[gnu::always_inline]] inline uint64_t Ld64(const uint8_t* __restrict base, uint32_t ea) noexcept {
  uint64_t be = 0;
  std::memcpy(&be, base + ea, sizeof(be));
  return __builtin_bswap64(be);
}

[[gnu::always_inline]] inline void St32(uint8_t* __restrict base, uint32_t ea, uint32_t val) noexcept {
  const uint32_t be = __builtin_bswap32(val);
  std::memcpy(base + ea, &be, sizeof(be));
}

[[gnu::always_inline]] inline uint32_t Ld32(const uint8_t* __restrict base, uint32_t ea) noexcept {
  uint32_t be = 0;
  std::memcpy(&be, base + ea, sizeof(be));
  return __builtin_bswap32(be);
}

// Inline PPC __savegprlr_N (N in 14..31): stores rN..r31 as 64-bit BE and r12.u32 at -8(r1).
template <int StartReg>
[[gnu::always_inline]] inline void SaveGprLr(PPCContext& __restrict ctx, uint8_t* base) noexcept {
  const uint32_t sp = ctx.r1.u32;
  if constexpr (StartReg <= 14) St64(base, sp + static_cast<uint32_t>(-152), ctx.r14.u64);
  if constexpr (StartReg <= 15) St64(base, sp + static_cast<uint32_t>(-144), ctx.r15.u64);
  if constexpr (StartReg <= 16) St64(base, sp + static_cast<uint32_t>(-136), ctx.r16.u64);
  if constexpr (StartReg <= 17) St64(base, sp + static_cast<uint32_t>(-128), ctx.r17.u64);
  if constexpr (StartReg <= 18) St64(base, sp + static_cast<uint32_t>(-120), ctx.r18.u64);
  if constexpr (StartReg <= 19) St64(base, sp + static_cast<uint32_t>(-112), ctx.r19.u64);
  if constexpr (StartReg <= 20) St64(base, sp + static_cast<uint32_t>(-104), ctx.r20.u64);
  if constexpr (StartReg <= 21) St64(base, sp + static_cast<uint32_t>(-96),  ctx.r21.u64);
  if constexpr (StartReg <= 22) St64(base, sp + static_cast<uint32_t>(-88),  ctx.r22.u64);
  if constexpr (StartReg <= 23) St64(base, sp + static_cast<uint32_t>(-80),  ctx.r23.u64);
  if constexpr (StartReg <= 24) St64(base, sp + static_cast<uint32_t>(-72),  ctx.r24.u64);
  if constexpr (StartReg <= 25) St64(base, sp + static_cast<uint32_t>(-64),  ctx.r25.u64);
  if constexpr (StartReg <= 26) St64(base, sp + static_cast<uint32_t>(-56),  ctx.r26.u64);
  if constexpr (StartReg <= 27) St64(base, sp + static_cast<uint32_t>(-48),  ctx.r27.u64);
  if constexpr (StartReg <= 28) St64(base, sp + static_cast<uint32_t>(-40),  ctx.r28.u64);
  if constexpr (StartReg <= 29) St64(base, sp + static_cast<uint32_t>(-32),  ctx.r29.u64);
  if constexpr (StartReg <= 30) St64(base, sp + static_cast<uint32_t>(-24),  ctx.r30.u64);
  if constexpr (StartReg <= 31) St64(base, sp + static_cast<uint32_t>(-16),  ctx.r31.u64);
  St32(base, sp + static_cast<uint32_t>(-8), ctx.r12.u32);
}

// Inline PPC __restgprlr_N (N in 14..31): loads rN..r31 and restores lr from -8(r1).
template <int StartReg>
[[gnu::always_inline]] inline void RestGprLr(PPCContext& __restrict ctx, uint8_t* base) noexcept {
  const uint32_t sp = ctx.r1.u32;
  if constexpr (StartReg <= 14) ctx.r14.u64 = Ld64(base, sp + static_cast<uint32_t>(-152));
  if constexpr (StartReg <= 15) ctx.r15.u64 = Ld64(base, sp + static_cast<uint32_t>(-144));
  if constexpr (StartReg <= 16) ctx.r16.u64 = Ld64(base, sp + static_cast<uint32_t>(-136));
  if constexpr (StartReg <= 17) ctx.r17.u64 = Ld64(base, sp + static_cast<uint32_t>(-128));
  if constexpr (StartReg <= 18) ctx.r18.u64 = Ld64(base, sp + static_cast<uint32_t>(-120));
  if constexpr (StartReg <= 19) ctx.r19.u64 = Ld64(base, sp + static_cast<uint32_t>(-112));
  if constexpr (StartReg <= 20) ctx.r20.u64 = Ld64(base, sp + static_cast<uint32_t>(-104));
  if constexpr (StartReg <= 21) ctx.r21.u64 = Ld64(base, sp + static_cast<uint32_t>(-96));
  if constexpr (StartReg <= 22) ctx.r22.u64 = Ld64(base, sp + static_cast<uint32_t>(-88));
  if constexpr (StartReg <= 23) ctx.r23.u64 = Ld64(base, sp + static_cast<uint32_t>(-80));
  if constexpr (StartReg <= 24) ctx.r24.u64 = Ld64(base, sp + static_cast<uint32_t>(-72));
  if constexpr (StartReg <= 25) ctx.r25.u64 = Ld64(base, sp + static_cast<uint32_t>(-64));
  if constexpr (StartReg <= 26) ctx.r26.u64 = Ld64(base, sp + static_cast<uint32_t>(-56));
  if constexpr (StartReg <= 27) ctx.r27.u64 = Ld64(base, sp + static_cast<uint32_t>(-48));
  if constexpr (StartReg <= 28) ctx.r28.u64 = Ld64(base, sp + static_cast<uint32_t>(-40));
  if constexpr (StartReg <= 29) ctx.r29.u64 = Ld64(base, sp + static_cast<uint32_t>(-32));
  if constexpr (StartReg <= 30) ctx.r30.u64 = Ld64(base, sp + static_cast<uint32_t>(-24));
  if constexpr (StartReg <= 31) ctx.r31.u64 = Ld64(base, sp + static_cast<uint32_t>(-16));
  const uint32_t lr32 = Ld32(base, sp + static_cast<uint32_t>(-8));
  ctx.r12.u64 = lr32;
  ctx.lr = lr32;
}

// Inline PPC __savefpr_N (N in 14..31): stores fN..f31 relative to r12.
template <int StartReg>
[[gnu::always_inline]] inline void SaveFpr(PPCContext& __restrict ctx, uint8_t* base) noexcept {
  ctx.fpscr.disableFlushMode();
  const uint32_t sp = ctx.r12.u32;
  if constexpr (StartReg <= 14) St64(base, sp + static_cast<uint32_t>(-144), ctx.f14.u64);
  if constexpr (StartReg <= 15) St64(base, sp + static_cast<uint32_t>(-136), ctx.f15.u64);
  if constexpr (StartReg <= 16) St64(base, sp + static_cast<uint32_t>(-128), ctx.f16.u64);
  if constexpr (StartReg <= 17) St64(base, sp + static_cast<uint32_t>(-120), ctx.f17.u64);
  if constexpr (StartReg <= 18) St64(base, sp + static_cast<uint32_t>(-112), ctx.f18.u64);
  if constexpr (StartReg <= 19) St64(base, sp + static_cast<uint32_t>(-104), ctx.f19.u64);
  if constexpr (StartReg <= 20) St64(base, sp + static_cast<uint32_t>(-96),  ctx.f20.u64);
  if constexpr (StartReg <= 21) St64(base, sp + static_cast<uint32_t>(-88),  ctx.f21.u64);
  if constexpr (StartReg <= 22) St64(base, sp + static_cast<uint32_t>(-80),  ctx.f22.u64);
  if constexpr (StartReg <= 23) St64(base, sp + static_cast<uint32_t>(-72),  ctx.f23.u64);
  if constexpr (StartReg <= 24) St64(base, sp + static_cast<uint32_t>(-64),  ctx.f24.u64);
  if constexpr (StartReg <= 25) St64(base, sp + static_cast<uint32_t>(-56),  ctx.f25.u64);
  if constexpr (StartReg <= 26) St64(base, sp + static_cast<uint32_t>(-48),  ctx.f26.u64);
  if constexpr (StartReg <= 27) St64(base, sp + static_cast<uint32_t>(-40),  ctx.f27.u64);
  if constexpr (StartReg <= 28) St64(base, sp + static_cast<uint32_t>(-32),  ctx.f28.u64);
  if constexpr (StartReg <= 29) St64(base, sp + static_cast<uint32_t>(-24),  ctx.f29.u64);
  if constexpr (StartReg <= 30) St64(base, sp + static_cast<uint32_t>(-16),  ctx.f30.u64);
  if constexpr (StartReg <= 31) St64(base, sp + static_cast<uint32_t>(-8),   ctx.f31.u64);
}

// Inline PPC __restfpr_N (N in 14..31): loads fN..f31 relative to r12.
template <int StartReg>
[[gnu::always_inline]] inline void RestFpr(PPCContext& __restrict ctx, uint8_t* base) noexcept {
  ctx.fpscr.disableFlushMode();
  const uint32_t sp = ctx.r12.u32;
  if constexpr (StartReg <= 14) ctx.f14.u64 = Ld64(base, sp + static_cast<uint32_t>(-144));
  if constexpr (StartReg <= 15) ctx.f15.u64 = Ld64(base, sp + static_cast<uint32_t>(-136));
  if constexpr (StartReg <= 16) ctx.f16.u64 = Ld64(base, sp + static_cast<uint32_t>(-128));
  if constexpr (StartReg <= 17) ctx.f17.u64 = Ld64(base, sp + static_cast<uint32_t>(-120));
  if constexpr (StartReg <= 18) ctx.f18.u64 = Ld64(base, sp + static_cast<uint32_t>(-112));
  if constexpr (StartReg <= 19) ctx.f19.u64 = Ld64(base, sp + static_cast<uint32_t>(-104));
  if constexpr (StartReg <= 20) ctx.f20.u64 = Ld64(base, sp + static_cast<uint32_t>(-96));
  if constexpr (StartReg <= 21) ctx.f21.u64 = Ld64(base, sp + static_cast<uint32_t>(-88));
  if constexpr (StartReg <= 22) ctx.f22.u64 = Ld64(base, sp + static_cast<uint32_t>(-80));
  if constexpr (StartReg <= 23) ctx.f23.u64 = Ld64(base, sp + static_cast<uint32_t>(-72));
  if constexpr (StartReg <= 24) ctx.f24.u64 = Ld64(base, sp + static_cast<uint32_t>(-64));
  if constexpr (StartReg <= 25) ctx.f25.u64 = Ld64(base, sp + static_cast<uint32_t>(-56));
  if constexpr (StartReg <= 26) ctx.f26.u64 = Ld64(base, sp + static_cast<uint32_t>(-48));
  if constexpr (StartReg <= 27) ctx.f27.u64 = Ld64(base, sp + static_cast<uint32_t>(-40));
  if constexpr (StartReg <= 28) ctx.f28.u64 = Ld64(base, sp + static_cast<uint32_t>(-32));
  if constexpr (StartReg <= 29) ctx.f29.u64 = Ld64(base, sp + static_cast<uint32_t>(-24));
  if constexpr (StartReg <= 30) ctx.f30.u64 = Ld64(base, sp + static_cast<uint32_t>(-16));
  if constexpr (StartReg <= 31) ctx.f31.u64 = Ld64(base, sp + static_cast<uint32_t>(-8));
}

// Inline 9-word struct copy helper sub_82907C38 (7,099 static call sites).
[[gnu::always_inline]] inline void Sub82907C38(PPCContext& __restrict ctx, uint8_t* base) noexcept {
  const uint32_t dst = ctx.r3.u32;
  const uint32_t sp = ctx.r1.u32;
  St32(base, dst + 8u, ctx.r6.u32);
  const uint32_t v11 = Ld32(base, sp + 84u);
  const uint32_t v7  = Ld32(base, sp + 92u);
  const uint32_t v6  = Ld32(base, sp + 100u);
  ctx.r11.u64 = v11;
  ctx.r7.u64  = v7;
  ctx.r6.u64  = v6;
  St32(base, dst + 0u,  ctx.r4.u32);
  St32(base, dst + 4u,  ctx.r5.u32);
  St32(base, dst + 12u, ctx.r8.u32);
  St32(base, dst + 16u, ctx.r9.u32);
  St32(base, dst + 20u, ctx.r10.u32);
  St32(base, dst + 24u, v11);
  St32(base, dst + 28u, v7);
  St32(base, dst + 32u, v6);
}

}  // namespace ts_ppc_inline

#define __savegprlr_14(c, b) ::ts_ppc_inline::SaveGprLr<14>((c), (b))
#define __savegprlr_15(c, b) ::ts_ppc_inline::SaveGprLr<15>((c), (b))
#define __savegprlr_16(c, b) ::ts_ppc_inline::SaveGprLr<16>((c), (b))
#define __savegprlr_17(c, b) ::ts_ppc_inline::SaveGprLr<17>((c), (b))
#define __savegprlr_18(c, b) ::ts_ppc_inline::SaveGprLr<18>((c), (b))
#define __savegprlr_19(c, b) ::ts_ppc_inline::SaveGprLr<19>((c), (b))
#define __savegprlr_20(c, b) ::ts_ppc_inline::SaveGprLr<20>((c), (b))
#define __savegprlr_21(c, b) ::ts_ppc_inline::SaveGprLr<21>((c), (b))
#define __savegprlr_22(c, b) ::ts_ppc_inline::SaveGprLr<22>((c), (b))
#define __savegprlr_23(c, b) ::ts_ppc_inline::SaveGprLr<23>((c), (b))
#define __savegprlr_24(c, b) ::ts_ppc_inline::SaveGprLr<24>((c), (b))
#define __savegprlr_25(c, b) ::ts_ppc_inline::SaveGprLr<25>((c), (b))
#define __savegprlr_26(c, b) ::ts_ppc_inline::SaveGprLr<26>((c), (b))
#define __savegprlr_27(c, b) ::ts_ppc_inline::SaveGprLr<27>((c), (b))
#define __savegprlr_28(c, b) ::ts_ppc_inline::SaveGprLr<28>((c), (b))
#define __savegprlr_29(c, b) ::ts_ppc_inline::SaveGprLr<29>((c), (b))
#define __savegprlr_30(c, b) ::ts_ppc_inline::SaveGprLr<30>((c), (b))
#define __savegprlr_31(c, b) ::ts_ppc_inline::SaveGprLr<31>((c), (b))

#define __restgprlr_14(c, b) ::ts_ppc_inline::RestGprLr<14>((c), (b))
#define __restgprlr_15(c, b) ::ts_ppc_inline::RestGprLr<15>((c), (b))
#define __restgprlr_16(c, b) ::ts_ppc_inline::RestGprLr<16>((c), (b))
#define __restgprlr_17(c, b) ::ts_ppc_inline::RestGprLr<17>((c), (b))
#define __restgprlr_18(c, b) ::ts_ppc_inline::RestGprLr<18>((c), (b))
#define __restgprlr_19(c, b) ::ts_ppc_inline::RestGprLr<19>((c), (b))
#define __restgprlr_20(c, b) ::ts_ppc_inline::RestGprLr<20>((c), (b))
#define __restgprlr_21(c, b) ::ts_ppc_inline::RestGprLr<21>((c), (b))
#define __restgprlr_22(c, b) ::ts_ppc_inline::RestGprLr<22>((c), (b))
#define __restgprlr_23(c, b) ::ts_ppc_inline::RestGprLr<23>((c), (b))
#define __restgprlr_24(c, b) ::ts_ppc_inline::RestGprLr<24>((c), (b))
#define __restgprlr_25(c, b) ::ts_ppc_inline::RestGprLr<25>((c), (b))
#define __restgprlr_26(c, b) ::ts_ppc_inline::RestGprLr<26>((c), (b))
#define __restgprlr_27(c, b) ::ts_ppc_inline::RestGprLr<27>((c), (b))
#define __restgprlr_28(c, b) ::ts_ppc_inline::RestGprLr<28>((c), (b))
#define __restgprlr_29(c, b) ::ts_ppc_inline::RestGprLr<29>((c), (b))
#define __restgprlr_30(c, b) ::ts_ppc_inline::RestGprLr<30>((c), (b))
#define __restgprlr_31(c, b) ::ts_ppc_inline::RestGprLr<31>((c), (b))

#define __savefpr_14(c, b) ::ts_ppc_inline::SaveFpr<14>((c), (b))
#define __savefpr_15(c, b) ::ts_ppc_inline::SaveFpr<15>((c), (b))
#define __savefpr_16(c, b) ::ts_ppc_inline::SaveFpr<16>((c), (b))
#define __savefpr_17(c, b) ::ts_ppc_inline::SaveFpr<17>((c), (b))
#define __savefpr_18(c, b) ::ts_ppc_inline::SaveFpr<18>((c), (b))
#define __savefpr_19(c, b) ::ts_ppc_inline::SaveFpr<19>((c), (b))
#define __savefpr_20(c, b) ::ts_ppc_inline::SaveFpr<20>((c), (b))
#define __savefpr_21(c, b) ::ts_ppc_inline::SaveFpr<21>((c), (b))
#define __savefpr_22(c, b) ::ts_ppc_inline::SaveFpr<22>((c), (b))
#define __savefpr_23(c, b) ::ts_ppc_inline::SaveFpr<23>((c), (b))
#define __savefpr_24(c, b) ::ts_ppc_inline::SaveFpr<24>((c), (b))
#define __savefpr_25(c, b) ::ts_ppc_inline::SaveFpr<25>((c), (b))
#define __savefpr_26(c, b) ::ts_ppc_inline::SaveFpr<26>((c), (b))
#define __savefpr_27(c, b) ::ts_ppc_inline::SaveFpr<27>((c), (b))
#define __savefpr_28(c, b) ::ts_ppc_inline::SaveFpr<28>((c), (b))
#define __savefpr_29(c, b) ::ts_ppc_inline::SaveFpr<29>((c), (b))
#define __savefpr_30(c, b) ::ts_ppc_inline::SaveFpr<30>((c), (b))
#define __savefpr_31(c, b) ::ts_ppc_inline::SaveFpr<31>((c), (b))

#define __restfpr_14(c, b) ::ts_ppc_inline::RestFpr<14>((c), (b))
#define __restfpr_15(c, b) ::ts_ppc_inline::RestFpr<15>((c), (b))
#define __restfpr_16(c, b) ::ts_ppc_inline::RestFpr<16>((c), (b))
#define __restfpr_17(c, b) ::ts_ppc_inline::RestFpr<17>((c), (b))
#define __restfpr_18(c, b) ::ts_ppc_inline::RestFpr<18>((c), (b))
#define __restfpr_19(c, b) ::ts_ppc_inline::RestFpr<19>((c), (b))
#define __restfpr_20(c, b) ::ts_ppc_inline::RestFpr<20>((c), (b))
#define __restfpr_21(c, b) ::ts_ppc_inline::RestFpr<21>((c), (b))
#define __restfpr_22(c, b) ::ts_ppc_inline::RestFpr<22>((c), (b))
#define __restfpr_23(c, b) ::ts_ppc_inline::RestFpr<23>((c), (b))
#define __restfpr_24(c, b) ::ts_ppc_inline::RestFpr<24>((c), (b))
#define __restfpr_25(c, b) ::ts_ppc_inline::RestFpr<25>((c), (b))
#define __restfpr_26(c, b) ::ts_ppc_inline::RestFpr<26>((c), (b))
#define __restfpr_27(c, b) ::ts_ppc_inline::RestFpr<27>((c), (b))
#define __restfpr_28(c, b) ::ts_ppc_inline::RestFpr<28>((c), (b))
#define __restfpr_29(c, b) ::ts_ppc_inline::RestFpr<29>((c), (b))
#define __restfpr_30(c, b) ::ts_ppc_inline::RestFpr<30>((c), (b))
#define __restfpr_31(c, b) ::ts_ppc_inline::RestFpr<31>((c), (b))

#define sub_82907C38(c, b) ::ts_ppc_inline::Sub82907C38((c), (b))

#endif
