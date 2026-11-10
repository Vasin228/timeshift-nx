#include "device_guest_calls.h"
#include <rex/ppc.h>
#include <stdexcept>

REX_EXTERN(__imp__sub_826478E0);
REX_EXTERN(__imp__sub_826539A0);
REX_EXTERN(__imp__sub_82653738);
REX_EXTERN(__imp__sub_82647600);
REX_EXTERN(__imp__RtlInitializeCriticalSection);
REX_EXTERN(__imp__KeQueryPerformanceFrequency);

namespace timeshift::d3d6274 {
namespace {
class GuestHost final : public Host {
 public:
  GuestHost(PPCContext &parent, uint8_t *base, GuestMapping mapping)
      : parent_(parent), base_(base), mapping_(mapping) {}
  std::span<std::byte> Map(uint32_t address, size_t bytes) override {
    return mapping_.map ? mapping_.map(mapping_.user, address, bytes) : std::span<std::byte>{};
  }
  uint32_t InvokeOriginal(uint32_t address, std::span<const uint32_t> words) override {
    PPCFunc *function = nullptr;
    switch (address) {
      case kCreateDevice: function = __imp__sub_826478E0; break;
      case kInitializePresentation: function = __imp__sub_826539A0; break;
      case kInitializeApiState: function = __imp__sub_82653738; break;
      case kReleaseDevice: function = __imp__sub_82647600; break;
      case kInitializeCriticalSection: function = __imp__RtlInitializeCriticalSection; break;
      default: throw std::logic_error("unreviewed device initialization call");
    }
    if (words.size() > 6) throw std::logic_error("device initialization argument count");
    // A complete SDK context preserves even the nonvolatile values saved by
    // the generated function's helpers. ResolveIndirectFunction is deliberately
    // not used: it could resolve back to the replacement being implemented.
    PPCContext call = parent_;
    PPCRegister *args[] = {&call.r3,&call.r4,&call.r5,&call.r6,&call.r7,&call.r8};
    for (size_t i = 0; i < words.size(); ++i) args[i]->u64 = words[i];
    function(call, base_);
    parent_.fpscr = call.fpscr;
    return call.r3.u32;
  }
  uint64_t PerformanceFrequency() override {
    PPCContext call = parent_;
    __imp__KeQueryPerformanceFrequency(call, base_);
    parent_.fpscr = call.fpscr;
    // The frozen SDK exports KeQueryPerformanceFrequency_entry as u32.
    return call.r3.u32;
  }
  bool DeviceBound(uint32_t device) override {
    return !mapping_.bound || mapping_.bound(mapping_.user,device);
  }
  bool BeforeFailedRelease(uint32_t device) override {
    return !mapping_.before_failed_release || mapping_.before_failed_release(mapping_.user,device);
  }
 private:
  PPCContext &parent_;
  uint8_t *base_;
  GuestMapping mapping_;
};
}
BootstrapResult BootstrapGuestDevice(PPCContext &context, uint8_t *base,
                                    const BootstrapMemory &memory, GuestMapping mapping) {
  const CreateArguments args{context.r3.u32,context.r4.u32,context.r5.u32,
      context.r6.u32,context.r7.u32,context.r8.u32};
  GuestHost host(context,base,mapping);
  auto result = BootstrapDevice(host,args,memory);
  context.r3.u64 = result.hresult;
  return result;
}
}
