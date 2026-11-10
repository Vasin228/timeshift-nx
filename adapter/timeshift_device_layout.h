#pragma once

#include <cstdint>

// TimeShift's D3D 2.0.6274 device. Evidence is recorded in timeshift_profile.json
// and checked against the owned image by tools/check_profile.py. These are byte
// offsets, not a blanket displacement of the SA2 or Nuts & Bolts device.
namespace timeshift::d3d6274::layout {
inline constexpr uint32_t allocation_bytes = 22400;
inline constexpr uint32_t alignment = 128;
inline constexpr uint32_t vertex_declaration = 11812;
inline constexpr uint32_t index_buffer = 12428;
inline constexpr uint32_t render_targets = 12432;
inline constexpr uint32_t depth_stencil = 12448;
inline constexpr uint32_t stream_sources = 12452;
inline constexpr uint32_t stream_strides = 12520;
inline constexpr uint32_t textures = 12536;
inline constexpr uint32_t viewport = 12640;
inline constexpr uint32_t scissor_rect = 12668;
inline constexpr uint32_t pixel_shader = 12684;
inline constexpr uint32_t vertex_shader = 12688;
inline constexpr uint32_t predication = 12700;
inline constexpr uint32_t snapshot_bytes = predication + 4;
inline constexpr uint32_t command_buffer = 13500;
inline constexpr uint32_t inline_ring_end = 13428;
inline constexpr uint32_t swap_count = 16544;
inline constexpr uint32_t fence_tag = 10908;
inline constexpr uint32_t swap_fence_tag = 10928;
inline constexpr uint32_t present_fence_tag = 14924;
inline constexpr uint32_t swap_flags = 10941;
inline constexpr uint32_t gamma_ramp = 14988;
// Do not name the unreviewed public/private tail as SA2's MiscPacket,
// PointPacket, MaxAnisotropy or ZFilter. The original setters retain ownership.
}
