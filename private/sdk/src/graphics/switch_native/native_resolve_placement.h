// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <rex/graphics/pipeline/texture/util.h>
namespace thps::native {
struct ResolvePlacement {
  uint32_t address;
  int32_t x, y;
  bool joined = false;
};
// D3D's tiled passes move the local EDRAM window and bias the destination
// memory base to that tile. Recover a shared texture base and global origin.
inline ResolvePlacement PlaceResolve(uint32_t address, int32_t x, int32_t y,
    int32_t window_x, int32_t window_y, bool window_enabled, bool is_array,
    uint32_t pitch, uint32_t bytes_per_pixel_log2) {
  ResolvePlacement out{address & 0x1FFFFFFFu, x, y, false};
  if (!window_enabled || is_array || window_x > 0 || window_y > 0 ||
      window_x <= -8192 || window_y <= -8192 ||
      (window_x == 0 && window_y == 0) || !pitch || bytes_per_pixel_log2 > 4)
    return out;
  int32_t origin_x = -window_x, origin_y = -window_y;
  if ((origin_x & 31) || (origin_y & 31) || origin_x >= 8192 || origin_y >= 8192 ||
      x < 0 || y < 0 || x + origin_x >= 8192 || y + origin_y >= 8192)
    return out;
  int32_t bias = rex::graphics::texture_util::GetTiledOffset2D(
      origin_x, origin_y, pitch, bytes_per_pixel_log2);
  if (bias < 0 || uint32_t(bias) > out.address) return out;
  uint32_t base = out.address - uint32_t(bias);
  if (base & 4095u) return out;
  return {base, x + origin_x, y + origin_y, true};
}
} // namespace thps::native
