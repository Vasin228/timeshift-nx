// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <rex/graphics/xenos.h>
#include <rex/ui/vulkan/device.h>
namespace thps::native {
struct ColorResolveStorage {
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t swizzle = uint32_t(rex::graphics::xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA);
};
inline ColorResolveStorage ColorResolveFormat(rex::graphics::xenos::ColorFormat format) {
  namespace x = rex::graphics::xenos;
  using F = x::ColorFormat;
  switch(format) {
    case F::k_8: case F::k_8_A: case F::k_8_B:
      return {VK_FORMAT_R8_UNORM,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RRRR)};
    case F::k_8_8: return {VK_FORMAT_R8G8_UNORM,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RGGG)};
    case F::k_8_8_8_8: case F::k_8_8_8_8_A: case F::k_8_8_8_8_AS_16_16_16_16:
      return {VK_FORMAT_R8G8B8A8_UNORM};
    case F::k_2_10_10_10: case F::k_2_10_10_10_AS_16_16_16_16:
      return {VK_FORMAT_A2B10G10R10_UNORM_PACK32};
    // Resolve destinations are unsigned even when the source fixed-point RT
    // is signed. See xenos::IsColorResolveFormatBitwiseEquivalent.
    case F::k_16: return {VK_FORMAT_R16_UNORM,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RRRR)};
    case F::k_16_16: return {VK_FORMAT_R16G16_UNORM,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RGGG)};
    case F::k_16_16_16_16: return {VK_FORMAT_R16G16B16A16_UNORM};
    case F::k_16_FLOAT: return {VK_FORMAT_R16_SFLOAT,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RRRR)};
    case F::k_16_16_FLOAT: return {VK_FORMAT_R16G16_SFLOAT,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RGGG)};
    case F::k_16_16_16_16_FLOAT: return {VK_FORMAT_R16G16B16A16_SFLOAT};
    case F::k_32_FLOAT: return {VK_FORMAT_R32_SFLOAT,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RRRR)};
    case F::k_32_32_FLOAT: return {VK_FORMAT_R32G32_SFLOAT,uint32_t(x::XE_GPU_TEXTURE_SWIZZLE_RGGG)};
    case F::k_32_32_32_32_FLOAT: return {VK_FORMAT_R32G32B32A32_SFLOAT};
    default: return {};
  }
}
inline int32_t ColorResolveExponent(rex::graphics::xenos::ColorRenderTargetFormat source,
                                    int32_t guest_bias) {
  using F=rex::graphics::xenos::ColorRenderTargetFormat;
  // SDK draw_util::GetResolveInfo compensates the native SNORM [-1,1]
  // representation of the guest fixed-point [-32,32] render targets.
  if(source==F::k_16_16 || source==F::k_16_16_16_16)
    return std::min(guest_bias+5,31);
  return guest_bias;
}
} // namespace thps::native
