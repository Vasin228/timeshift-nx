#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

namespace timeshift::renderer {

// Exact 3-field layout from owned TimeShift Xbox 360 CPU disassembly
// (sub_8264C8E8, sub_8264C810, sub_8264C378):
// +0: ColorFormat index (low 6 bits: 0..63)
// +4: FormatFlags / conversion flags / exponent bias
// +8: DepthFormat index (low 6 bits: 0..63)
struct ResolveParameters {
  uint32_t color_format = 0;
  uint32_t format_flags = 0;
  uint32_t depth_format = 0;
};

inline ResolveParameters DecodeResolveParameters(std::span<const uint8_t> bytes) {
  if (bytes.size() < 12) {
    throw std::out_of_range("native tiling resolve parameters unavailable");
  }
  ResolveParameters params;
  params.color_format = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                        (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
  params.format_flags = (uint32_t(bytes[4]) << 24) | (uint32_t(bytes[5]) << 16) |
                        (uint32_t(bytes[6]) << 8) | uint32_t(bytes[7]);
  params.depth_format = (uint32_t(bytes[8]) << 24) | (uint32_t(bytes[9]) << 16) |
                        (uint32_t(bytes[10]) << 8) | uint32_t(bytes[11]);
  if (params.color_format >= 64 || params.depth_format >= 64) {
    throw std::invalid_argument("invalid resolve format index");
  }
  return params;
}

}  // namespace timeshift::renderer
