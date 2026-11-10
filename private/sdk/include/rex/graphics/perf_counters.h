// Temporary frame profiling counters (Part 103 debug build).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/logging.h>

namespace rex::graphics::perf {

inline std::atomic<uint64_t> translate_ns{0};
inline std::atomic<uint64_t> swap_ns{0};
inline std::atomic<uint64_t> flush_ns{0};
inline std::atomic<uint64_t> present_ns{0};
inline std::atomic<uint64_t> submit_record_ns{0};
inline std::atomic<uint64_t> submit_end_ns{0};
inline std::atomic<uint64_t> fence_wait_ns{0};
inline std::atomic<uint64_t> render_passes{0};
inline std::atomic<uint64_t> resolves{0};
inline std::atomic<uint64_t> resolve_bytes{0};
inline std::atomic<uint64_t> desc_allocs{0};
inline std::atomic<uint64_t> frame_end_ns{0};
inline std::atomic<uint64_t> tex_endframe_ns{0};
inline std::atomic<uint64_t> rt_endframe_ns{0};
inline std::atomic<uint64_t> swap_request_ns{0};
inline std::atomic<uint64_t> pipeline_ns{0};
inline std::atomic<uint64_t> draws{0};
inline std::atomic<uint64_t> pipeline_calls{0};
// Part 109: what the 443 ms of present_avg is actually spent in.
inline std::atomic<uint64_t> lambda_exec_ns{0};
inline std::atomic<uint64_t> out_create_ns{0};
inline std::atomic<uint64_t> out_submit_ns{0};
inline std::atomic<uint64_t> paint_ns{0};
inline std::atomic<uint64_t> queue_present_ns{0};
// Part 158: vkCreateGraphicsPipelines (driver shader JIT) and our own SPIR-V
// optimisation pass, so "the pipeline costs 820 ms" can be attributed.
inline std::atomic<uint64_t> vk_create_ns{0};
inline std::atomic<uint64_t> spv_opt_ns{0};
inline std::atomic<uint64_t> frames{0};
inline std::atomic<uint64_t> window_start_ns{0};

inline uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

struct ScopedTimer {
  std::atomic<uint64_t>& target;
  uint64_t start;
  explicit ScopedTimer(std::atomic<uint64_t>& t) : target(t), start(NowNs()) {}
  ~ScopedTimer() { target.fetch_add(NowNs() - start, std::memory_order_relaxed); }
};

inline void ReportFrame() {
  uint64_t frame = frames.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((frame % 60) != 0) {
    return;
  }
  uint64_t now = NowNs();
  uint64_t start = window_start_ns.exchange(now, std::memory_order_relaxed);
  if (start == 0) {
    start = now;
  }
  double dt_ms = double(now - start) / 1e6;
  double translate_ms = double(translate_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double swap_ms = double(swap_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double flush_ms = double(flush_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double present_ms = double(present_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double pipeline_ms = double(pipeline_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  uint64_t draw_count = draws.exchange(0, std::memory_order_relaxed);
  uint64_t pipe_calls = pipeline_calls.exchange(0, std::memory_order_relaxed);
  double record_ms = double(submit_record_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double submit_ms = double(submit_end_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double fence_ms = double(fence_wait_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  uint64_t passes = render_passes.exchange(0, std::memory_order_relaxed);
  uint64_t resolve_count = resolves.exchange(0, std::memory_order_relaxed);
  uint64_t resolve_b = resolve_bytes.exchange(0, std::memory_order_relaxed);
  uint64_t desc_allocs_count = desc_allocs.exchange(0, std::memory_order_relaxed);
  double frame_end_ms = double(frame_end_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double tex_end_ms = double(tex_endframe_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double rt_end_ms = double(rt_endframe_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double swap_req_ms = double(swap_request_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double lambda_exec_ms = double(lambda_exec_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double out_create_ms = double(out_create_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double out_submit_ms = double(out_submit_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double paint_ms = double(paint_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double queue_present_ms =
      double(queue_present_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double vk_create_ms = double(vk_create_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  double spv_opt_ms = double(spv_opt_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
  REXLOG_INFO(
      "[perf] frame={} window={:.0f}ms frame_avg={:.1f}ms translate_avg={:.1f}ms "
      "flush_avg={:.1f}ms present_avg={:.1f}ms swap_avg={:.1f}ms draws_avg={} pipe_calls={} "
      "pipe_ms={:.1f} fps={:.1f}",
      frame, dt_ms, dt_ms / 60.0, translate_ms / 60.0, flush_ms / 60.0, present_ms / 60.0,
      swap_ms / 60.0, double(draw_count) / 60.0, pipe_calls, pipeline_ms, 60000.0 / dt_ms);
  REXLOG_INFO(
      "[perfrt] frame={} passes={} resolves={} resolve_mb={:.1f} desc_sets={} "
      "record_avg={:.1f}ms submit_avg={:.1f}ms fence_avg={:.1f}ms",
      frame, double(passes) / 60.0, double(resolve_count) / 60.0,
      double(resolve_b) / 60.0 / (1024.0 * 1024.0), double(desc_allocs_count) / 60.0,
      record_ms / 60.0, submit_ms / 60.0, fence_ms / 60.0);
  REXLOG_INFO(
      "[perfend] frame={} frame_end_avg={:.1f}ms tex_end_avg={:.1f}ms rt_end_avg={:.1f}ms "
      "swap_req_avg={:.1f}ms",
      frame, frame_end_ms / 60.0, tex_end_ms / 60.0, rt_end_ms / 60.0, swap_req_ms / 60.0);
  REXLOG_INFO(
      "[perfswap] frame={} present_avg={:.1f}ms lambda_exec_avg={:.1f}ms "
      "out_create_avg={:.1f}ms out_submit_avg={:.1f}ms paint_avg={:.1f}ms "
      "queue_present_avg={:.1f}ms",
      frame, present_ms / 60.0, lambda_exec_ms / 60.0, out_create_ms / 60.0,
      out_submit_ms / 60.0, paint_ms / 60.0, queue_present_ms / 60.0);
  REXLOG_INFO(
      "[perfpipe] frame={} pipe_calls={} pipe_avg={:.1f}ms vkcreate_avg={:.1f}ms "
      "spvopt_avg={:.1f}ms",
      frame, pipe_calls, pipeline_ms / double(pipe_calls ? pipe_calls : 1), vk_create_ms / 60.0,
      spv_opt_ms / 60.0);
}

struct FrameScope {
  uint64_t start;
  FrameScope() : start(NowNs()) {}
  ~FrameScope() {
    swap_ns.fetch_add(NowNs() - start, std::memory_order_relaxed);
    ReportFrame();
  }
};

}  // namespace rex::graphics::perf
