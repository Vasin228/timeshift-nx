#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <rex/logging.h>

namespace timeshift::telemetry {

inline std::atomic<uint64_t> g_drop_draw_null_address{0};
inline std::atomic<uint64_t> g_drop_clear_empty_rect{0};
inline std::atomic<uint64_t> g_drop_resolve_invalid{0};
inline std::atomic<uint64_t> g_drop_texture_null_base{0};

inline void CheckAndReportDrops(uint64_t frame_index) {
  const uint64_t drop_draw = g_drop_draw_null_address.load(std::memory_order_relaxed);
  const uint64_t drop_clear = g_drop_clear_empty_rect.load(std::memory_order_relaxed);
  const uint64_t drop_resolve = g_drop_resolve_invalid.load(std::memory_order_relaxed);
  const uint64_t drop_tex = g_drop_texture_null_base.load(std::memory_order_relaxed);
  if (drop_draw > 0 || drop_clear > 0 || drop_resolve > 0 || drop_tex > 0) {
    REXLOG_WARN("[telemetry] Silent render drops at frame #{}: draw_null_addr={}, clear_empty_rect={}, resolve_invalid={}, tex_null_base={}",
                frame_index, drop_draw, drop_clear, drop_resolve, drop_tex);
    std::fflush(stdout);
  }
}

}  // namespace timeshift::telemetry
