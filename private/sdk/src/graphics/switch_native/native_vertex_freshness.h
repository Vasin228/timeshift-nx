// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <xxhash.h>
namespace thps::native {
// Guest dynamic vertices/indices can be rewritten without a fetch-register
// change. A frame number is not a content identity. The caller still bounds
// this span through the proven shader fetch path before reading it.
inline bool RefreshVertexContent(uint64_t& last_frame, uint64_t& last_hash,
                                 uint64_t frame, const void* data, uint32_t bytes) {
  uint64_t hash = XXH3_64bits(data, bytes);
  bool changed_within_frame = last_frame == frame && last_hash != hash;
  last_frame = frame;
  last_hash = hash;
  return changed_within_frame;
}
} // namespace thps::native
