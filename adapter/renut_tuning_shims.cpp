/**
 * @file renut_tuning_shims.cpp
 * @brief Clean adapter stubs and definitions for ReNut tuning, metrics, and telemetry symbols.
 *
 * Provides neutral default tuning and profiling shims without pulling in any foreign game
 * hooks from SA2 or Banjo. Pure, portable, zero-foreign-hook adapter.
 */

#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/banjo_renut_record.h"
#include "renut_engine/gpu/banjo_renut_checks.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/guest/widescreen.h"
#include "me/masseffect/banjo_pmull_fingerprint.h"
#include "perf_breakdown.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/frame_latency.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <spdlog/sinks/sink.h>
#include <string_view>
#include <xxhash.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(PLUME_DEKO3D) || defined(__SWITCH__) || defined(REX_PLATFORM_SWITCH)
#include <plume_deko3d.h>
#endif
#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace timeshift::telemetry {
extern std::atomic<uint32_t> g_telemetry_frame_draws;

namespace {
constexpr uint32_t kWarmupFrames = 60;
constexpr uint32_t kWindowFrames = 300;
constexpr uint32_t kOnePctTailCount = 3;  // 1% of 300 frames

std::atomic<uint32_t> g_frame_kicks{0};
std::atomic<uint32_t> g_frame_pipeline_binds{0};
std::atomic<uint64_t> g_frame_upload_bytes{0};
std::atomic<uint32_t> g_frame_resolves_drawn{0};
std::atomic<uint32_t> g_frame_resolves_skipped{0};
std::atomic<uint32_t> g_frame_depth_only{0};
std::atomic<uint64_t> g_frame_gpu_us{0};
std::atomic<uint64_t> g_frame_game_wait_us{0};

struct PerfSample {
  uint32_t frame_idx;
  double frame_ms;
  double render_busy_ms;
  double gpu_ms;
  double game_wait_ms;
  uint32_t draws;
  uint32_t pipeline_binds;
  uint32_t cmd_submits;
  uint64_t upload_bytes;
  uint32_t resolves_drawn;
  uint32_t resolves_skipped;
  uint32_t depth_only;
  double draw_adp_ms;
  double orig_d3d_ms;
  double fence_wait_ms;
  double fence_qry_ms;
  double tiling_ms;
  double swap_ms;
  double game_frame_ms;
  uint64_t fence_wait_cnt;
  uint64_t fence_qry_cnt;
  uint64_t tiling_cnt;
  double ren_dev_ms;
  double ren_tex_ms;
  double ren_geo_ms;
  double ren_const_ms;
  double ren_pipe_ms;
  double ren_res_ms;
  double ren_pres_ms;
  uint64_t tex_memo_hits;
  uint64_t tex_bind_calls;
  uint64_t tex_create_cnt;
  uint64_t tex_upload_cnt;
  uint64_t tex_upload_bytes;
  double tex_sample_ms;
  double tex_linear_ms;
  double tex_upload_ms;
  double tex_create_ms;
  double tex_handover_ms;
  double tex_sampler_ms;
  double orig_didx_ms;
  uint64_t orig_didx_cnt;
  double orig_up_ms;
  uint64_t orig_up_cnt;
  double orig_clr_ms;
  uint64_t orig_clr_cnt;
  double orig_res_ms;
  uint64_t orig_res_cnt;
};

void ResetPerFrameCounters() {
  g_telemetry_frame_draws.store(0, std::memory_order_relaxed);
  g_frame_kicks.store(0, std::memory_order_relaxed);
  g_frame_pipeline_binds.store(0, std::memory_order_relaxed);
  g_frame_upload_bytes.store(0, std::memory_order_relaxed);
  g_frame_resolves_drawn.store(0, std::memory_order_relaxed);
  g_frame_resolves_skipped.store(0, std::memory_order_relaxed);
  g_frame_depth_only.store(0, std::memory_order_relaxed);
  g_frame_gpu_us.store(0, std::memory_order_relaxed);
  g_frame_game_wait_us.store(0, std::memory_order_relaxed);
  g_ticks_draw_adapter.store(0, std::memory_order_relaxed);
  g_ticks_orig_d3d.store(0, std::memory_order_relaxed);
  g_ticks_fence_wait.store(0, std::memory_order_relaxed);
  g_count_fence_wait.store(0, std::memory_order_relaxed);
  g_ticks_fence_query.store(0, std::memory_order_relaxed);
  g_count_fence_query.store(0, std::memory_order_relaxed);
  g_ticks_tiling.store(0, std::memory_order_relaxed);
  g_count_tiling.store(0, std::memory_order_relaxed);
  g_ticks_swap.store(0, std::memory_order_relaxed);
  g_ticks_game_frame.store(0, std::memory_order_relaxed);
  g_ren_ticks_dev.store(0, std::memory_order_relaxed);
  g_ren_ticks_tex.store(0, std::memory_order_relaxed);
  g_ren_ticks_geo.store(0, std::memory_order_relaxed);
  g_ren_ticks_const.store(0, std::memory_order_relaxed);
  g_ren_ticks_pipe.store(0, std::memory_order_relaxed);
  g_ren_ticks_res.store(0, std::memory_order_relaxed);
  g_ren_ticks_pres.store(0, std::memory_order_relaxed);
  g_tex_memo_hits.store(0, std::memory_order_relaxed);
  g_tex_bind_calls.store(0, std::memory_order_relaxed);
  g_tex_create_cnt.store(0, std::memory_order_relaxed);
  g_tex_upload_cnt.store(0, std::memory_order_relaxed);
  g_tex_upload_bytes.store(0, std::memory_order_relaxed);
  g_tex_ticks_sample.store(0, std::memory_order_relaxed);
  g_tex_ticks_linear.store(0, std::memory_order_relaxed);
  g_tex_ticks_upload.store(0, std::memory_order_relaxed);
  g_tex_ticks_create.store(0, std::memory_order_relaxed);
  g_tex_ticks_handover.store(0, std::memory_order_relaxed);
  g_tex_ticks_sampler.store(0, std::memory_order_relaxed);
  g_orig_ticks_didx.store(0, std::memory_order_relaxed);
  g_orig_cnt_didx.store(0, std::memory_order_relaxed);
  g_orig_ticks_up.store(0, std::memory_order_relaxed);
  g_orig_cnt_up.store(0, std::memory_order_relaxed);
  g_orig_ticks_clr.store(0, std::memory_order_relaxed);
  g_orig_cnt_clr.store(0, std::memory_order_relaxed);
  g_orig_ticks_res.store(0, std::memory_order_relaxed);
  g_orig_cnt_res.store(0, std::memory_order_relaxed);
}

void FlushPerfWindowCsv(const PerfSample* samples, uint32_t count) {
  static char csv_buf[131072];
  size_t pos = 0;
  auto append = [&](int n) {
    if (n > 0 && pos + size_t(n) < sizeof(csv_buf)) {
      pos += size_t(n);
    } else if (n > 0) {
      pos = sizeof(csv_buf) - 1;
    }
  };

  append(std::snprintf(
      csv_buf + pos, sizeof(csv_buf) - pos,
      "frame_idx,frame_ms,render_busy_ms,gpu_ms,game_wait_ms,draws,pipeline_binds,"
      "cmd_submits,upload_bytes,resolves_drawn,resolves_skipped,depth_only,"
      "draw_adp_ms,orig_d3d_ms,fence_wait_ms,fence_qry_ms,tiling_ms,swap_ms\n"));

  double sum_frame_ms = 0.0;
  double sum_render_ms = 0.0;
  double sum_gpu_ms = 0.0;
  double sum_wait_ms = 0.0;
  double sum_adp_ms = 0.0;
  double sum_orig_d3d_ms = 0.0;
  double sum_fwait_ms = 0.0;
  double sum_fqry_ms = 0.0;
  double sum_tiling_ms = 0.0;
  double sum_swap_ms = 0.0;
  double sum_gframe_ms = 0.0;
  double sum_ren_dev_ms = 0.0;
  double sum_ren_tex_ms = 0.0;
  double sum_ren_geo_ms = 0.0;
  double sum_ren_const_ms = 0.0;
  double sum_ren_pipe_ms = 0.0;
  double sum_ren_res_ms = 0.0;
  double sum_ren_pres_ms = 0.0;
  uint64_t sum_tex_memo_hits = 0;
  uint64_t sum_tex_bind_calls = 0;
  uint64_t sum_tex_create_cnt = 0;
  uint64_t sum_tex_upload_cnt = 0;
  uint64_t sum_tex_upload_bytes = 0;
  double sum_tex_sample_ms = 0.0;
  double sum_tex_linear_ms = 0.0;
  double sum_tex_upload_ms = 0.0;
  double sum_tex_create_ms = 0.0;
  double sum_tex_handover_ms = 0.0;
  double sum_tex_sampler_ms = 0.0;
  double sum_orig_didx_ms = 0.0;
  uint64_t sum_orig_didx_cnt = 0;
  double sum_orig_up_ms = 0.0;
  uint64_t sum_orig_up_cnt = 0;
  double sum_orig_clr_ms = 0.0;
  uint64_t sum_orig_clr_cnt = 0;
  double sum_orig_res_ms = 0.0;
  uint64_t sum_orig_res_cnt = 0;
  uint64_t sum_draws = 0;
  uint64_t sum_binds = 0;
  uint64_t sum_submits = 0;
  uint64_t sum_upload_bytes = 0;
  uint64_t sum_fwait_cnt = 0;
  uint64_t sum_fqry_cnt = 0;
  uint64_t sum_tiling_cnt = 0;
  double sorted_frame_ms[kWindowFrames];

  for (uint32_t i = 0; i < count; ++i) {
    const PerfSample& s = samples[i];
    sum_frame_ms += s.frame_ms;
    sum_render_ms += s.render_busy_ms;
    sum_gpu_ms += s.gpu_ms;
    sum_wait_ms += s.game_wait_ms;
    sum_adp_ms += s.draw_adp_ms;
    sum_orig_d3d_ms += s.orig_d3d_ms;
    sum_fwait_ms += s.fence_wait_ms;
    sum_fqry_ms += s.fence_qry_ms;
    sum_tiling_ms += s.tiling_ms;
    sum_swap_ms += s.swap_ms;
    sum_gframe_ms += s.game_frame_ms;
    sum_ren_dev_ms += s.ren_dev_ms;
    sum_ren_tex_ms += s.ren_tex_ms;
    sum_ren_geo_ms += s.ren_geo_ms;
    sum_ren_const_ms += s.ren_const_ms;
    sum_ren_pipe_ms += s.ren_pipe_ms;
    sum_ren_res_ms += s.ren_res_ms;
    sum_ren_pres_ms += s.ren_pres_ms;
    sum_tex_memo_hits += s.tex_memo_hits;
    sum_tex_bind_calls += s.tex_bind_calls;
    sum_tex_create_cnt += s.tex_create_cnt;
    sum_tex_upload_cnt += s.tex_upload_cnt;
    sum_tex_upload_bytes += s.tex_upload_bytes;
    sum_tex_sample_ms += s.tex_sample_ms;
    sum_tex_linear_ms += s.tex_linear_ms;
    sum_tex_upload_ms += s.tex_upload_ms;
    sum_tex_create_ms += s.tex_create_ms;
    sum_tex_handover_ms += s.tex_handover_ms;
    sum_tex_sampler_ms += s.tex_sampler_ms;
    sum_orig_didx_ms += s.orig_didx_ms;
    sum_orig_didx_cnt += s.orig_didx_cnt;
    sum_orig_up_ms += s.orig_up_ms;
    sum_orig_up_cnt += s.orig_up_cnt;
    sum_orig_clr_ms += s.orig_clr_ms;
    sum_orig_clr_cnt += s.orig_clr_cnt;
    sum_orig_res_ms += s.orig_res_ms;
    sum_orig_res_cnt += s.orig_res_cnt;
    sum_draws += s.draws;
    sum_binds += s.pipeline_binds;
    sum_submits += s.cmd_submits;
    sum_upload_bytes += s.upload_bytes;
    sum_fwait_cnt += s.fence_wait_cnt;
    sum_fqry_cnt += s.fence_qry_cnt;
    sum_tiling_cnt += s.tiling_cnt;
    sorted_frame_ms[i] = s.frame_ms;

    if (pos + 256 < sizeof(csv_buf)) {
      append(std::snprintf(
          csv_buf + pos, sizeof(csv_buf) - pos,
          "%u,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%llu,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
          s.frame_idx, s.frame_ms, s.render_busy_ms, s.gpu_ms, s.game_wait_ms,
          s.draws, s.pipeline_binds, s.cmd_submits,
          static_cast<unsigned long long>(s.upload_bytes),
          s.resolves_drawn, s.resolves_skipped, s.depth_only,
          s.draw_adp_ms, s.orig_d3d_ms, s.fence_wait_ms, s.fence_qry_ms, s.tiling_ms, s.swap_ms));
    }
  }

  std::sort(sorted_frame_ms, sorted_frame_ms + count, [](double a, double b) { return a > b; });
  const uint32_t tail_n = std::min(kOnePctTailCount, count);
  double worst_sum = 0.0;
  for (uint32_t i = 0; i < tail_n; ++i) {
    worst_sum += sorted_frame_ms[i];
  }
  const double avg_frame_ms = sum_frame_ms / double(count);
  const double avg_fps = avg_frame_ms > 0.001 ? 1000.0 / avg_frame_ms : 0.0;
  const double worst_avg_ms = worst_sum / double(tail_n);
  const double low_1pct_fps = worst_avg_ms > 0.001 ? 1000.0 / worst_avg_ms : 0.0;
  const double avg_render_ms = sum_render_ms / double(count);
  const double avg_gpu_ms = sum_gpu_ms / double(count);
  const double avg_wait_ms = sum_wait_ms / double(count);
  const double avg_adp = sum_adp_ms / double(count);
  const double avg_orig_d3d = sum_orig_d3d_ms / double(count);
  const double avg_fwait = sum_fwait_ms / double(count);
  const double avg_fqry = sum_fqry_ms / double(count);
  const double avg_tiling = sum_tiling_ms / double(count);
  const double avg_swap = sum_swap_ms / double(count);
  const double avg_gframe = sum_gframe_ms / double(count);
  const double avg_game_logic =
      std::max(0.0, avg_gframe - (avg_adp + avg_orig_d3d + avg_fwait + avg_fqry + avg_tiling + avg_swap));
  const double avg_ren_dev = sum_ren_dev_ms / double(count);
  const double avg_ren_tex = sum_ren_tex_ms / double(count);
  const double avg_ren_geo = sum_ren_geo_ms / double(count);
  const double avg_ren_const = sum_ren_const_ms / double(count);
  const double avg_ren_pipe = sum_ren_pipe_ms / double(count);
  const double avg_ren_res = sum_ren_res_ms / double(count);
  const double avg_ren_pres = sum_ren_pres_ms / double(count);
  const double avg_ren_sum =
      avg_ren_dev + avg_ren_tex + avg_ren_geo + avg_ren_const + avg_ren_pipe + avg_ren_res + avg_ren_pres;
  const double avg_tex_memo = double(sum_tex_memo_hits) / double(count);
  const double avg_tex_bind = double(sum_tex_bind_calls) / double(count);
  const double avg_tex_sample = sum_tex_sample_ms / double(count);
  const double avg_tex_linear = sum_tex_linear_ms / double(count);
  const double avg_tex_upload = sum_tex_upload_ms / double(count);
  const double avg_tex_upload_cnt = double(sum_tex_upload_cnt) / double(count);
  const double avg_tex_upload_kb = (double(sum_tex_upload_bytes) / 1024.0) / double(count);
  const double avg_tex_create = sum_tex_create_ms / double(count);
  const double avg_tex_create_cnt = double(sum_tex_create_cnt) / double(count);
  const double avg_tex_handover = sum_tex_handover_ms / double(count);
  const double avg_tex_sampler = sum_tex_sampler_ms / double(count);
  const double avg_orig_didx = sum_orig_didx_ms / double(count);
  const double avg_orig_didx_cnt = double(sum_orig_didx_cnt) / double(count);
  const double avg_orig_up = sum_orig_up_ms / double(count);
  const double avg_orig_up_cnt = double(sum_orig_up_cnt) / double(count);
  const double avg_orig_clr = sum_orig_clr_ms / double(count);
  const double avg_orig_clr_cnt = double(sum_orig_clr_cnt) / double(count);
  const double avg_orig_res = sum_orig_res_ms / double(count);
  const double avg_orig_res_cnt = double(sum_orig_res_cnt) / double(count);
  const double avg_draws = double(sum_draws) / double(count);
  const double avg_binds = double(sum_binds) / double(count);
  const double avg_submits = double(sum_submits) / double(count);
  const double avg_upload_kb = (double(sum_upload_bytes) / 1024.0) / double(count);

  append(std::snprintf(
      csv_buf + pos, sizeof(csv_buf) - pos,
      "# summary,avg_frame_ms,avg_fps,one_pct_low_fps,avg_render_busy_ms,avg_gpu_ms,"
      "avg_game_wait_ms,avg_draws,avg_pipeline_binds,avg_cmd_submits,avg_upload_kb\n"
      "# summary_values,%.3f,%.2f,%.2f,%.3f,%.3f,%.3f,%.1f,%.1f,%.2f,%.1f\n",
      avg_frame_ms, avg_fps, low_1pct_fps, avg_render_ms, avg_gpu_ms,
      avg_wait_ms, avg_draws, avg_binds, avg_submits, avg_upload_kb));

#if defined(__SWITCH__)
  const char* path = "sdmc:/switch/timeshift/perf_baseline.csv";
#else
  const char* path = "perf_baseline.csv";
#endif
  if (FILE* f = std::fopen(path, "w")) {
    std::fwrite(csv_buf, 1, pos, f);
    std::fclose(f);
  }

  uint32_t cpu_mhz = 0, gpu_mhz = 0, emc_mhz = 0;
#if defined(__SWITCH__)
  if (R_SUCCEEDED(clkrstInitialize())) {
    ClkrstSession sess{};
    u32 hz = 0;
    if (R_SUCCEEDED(clkrstOpenSession(&sess, PcvModuleId_CpuBus, 3))) {
      if (R_SUCCEEDED(clkrstGetClockRate(&sess, &hz))) cpu_mhz = hz / 1000000u;
      clkrstCloseSession(&sess);
    }
    if (R_SUCCEEDED(clkrstOpenSession(&sess, PcvModuleId_GPU, 3))) {
      if (R_SUCCEEDED(clkrstGetClockRate(&sess, &hz))) gpu_mhz = hz / 1000000u;
      clkrstCloseSession(&sess);
    }
    if (R_SUCCEEDED(clkrstOpenSession(&sess, PcvModuleId_EMC, 3))) {
      if (R_SUCCEEDED(clkrstGetClockRate(&sess, &hz))) emc_mhz = hz / 1000000u;
      clkrstCloseSession(&sess);
    }
    clkrstExit();
  }
#endif
  const uint32_t core_mask = g_game_core_mask.exchange(0, std::memory_order_relaxed);
  uint64_t ho[6]{};
  for (int i = 0; i < 6; ++i) {
    ho[i] = g_handover_counts[i].exchange(0, std::memory_order_relaxed);
  }
  const std::string delta_rep = renut::gpu::SnapshotDeltaReport();

  REXLOG_INFO(
      "[perf] 300f CSV: avg={:.2f}ms ({:.1f} FPS, 1%low={:.1f}), render={:.2f}ms, "
      "gpu={:.2f}ms, wait={:.2f}ms, draws={:.0f}, binds={:.0f}, submits={:.1f}, upload={:.1f}KB",
      avg_frame_ms, avg_fps, low_1pct_fps, avg_render_ms, avg_gpu_ms,
      avg_wait_ms, avg_draws, avg_binds, avg_submits, avg_upload_kb);
  REXLOG_INFO(
      "[perf-cpu] 300f: gframe={:.2f}ms (logic={:.2f}, adp={:.2f}, orig_d3d={:.2f}, "
      "fwait={:.2f}[{:.1f}/f], fqry={:.2f}[{:.1f}/f], tile={:.2f}[{:.1f}/f], swap={:.2f}) "
      "cores=0x{:X} clk={}/{}/{}MHz ho=[{},{},{},{},{},{}]",
      avg_gframe, avg_game_logic, avg_adp, avg_orig_d3d,
      avg_fwait, double(sum_fwait_cnt) / double(count),
      avg_fqry, double(sum_fqry_cnt) / double(count),
      avg_tiling, double(sum_tiling_cnt) / double(count),
      avg_swap, core_mask, cpu_mhz, gpu_mhz, emc_mhz,
      ho[0], ho[1], ho[2], ho[3], ho[4], ho[5]);
  REXLOG_INFO(
      "[perf-ren] 300f: dev={:.2f} tex={:.2f} geo={:.2f} const={:.2f} "
      "pipe={:.2f} res={:.2f} pres={:.2f} (sum={:.2f}ms)",
      avg_ren_dev, avg_ren_tex, avg_ren_geo, avg_ren_const,
      avg_ren_pipe, avg_ren_res, avg_ren_pres, avg_ren_sum);
  REXLOG_INFO(
      "[perf-tex] 300f: memo={:.0f} bind={:.0f} sample={:.2f}ms linear={:.2f}ms "
      "upload={:.2f}ms[{:.1f}/f,{:.1f}KB] create={:.2f}ms[{:.1f}/f] ho={:.2f}ms samp={:.2f}ms",
      avg_tex_memo, avg_tex_bind, avg_tex_sample, avg_tex_linear,
      avg_tex_upload, avg_tex_upload_cnt, avg_tex_upload_kb,
      avg_tex_create, avg_tex_create_cnt, avg_tex_handover, avg_tex_sampler);
  REXLOG_INFO(
      "[perf-orig] 300f: didx={:.2f}ms[{:.0f}/f] up={:.2f}ms[{:.0f}/f] "
      "clr={:.2f}ms[{:.0f}/f] res={:.2f}ms[{:.0f}/f]",
      avg_orig_didx, avg_orig_didx_cnt, avg_orig_up, avg_orig_up_cnt,
      avg_orig_clr, avg_orig_clr_cnt, avg_orig_res, avg_orig_res_cnt);
  if (!delta_rep.empty()) {
    REXLOG_INFO("[perf-delta] {}", delta_rep);
  }
}
}  // namespace
}  // namespace timeshift::telemetry

// CVar for diagnostic shaders
REXCVAR_DEFINE_BOOL(banjo_renut_diag_gpu_shaders, false, "Renut", "Diagnostic shader logging");
REXCVAR_DEFINE_BOOL(renut_deko3d_zcull, true, "Renut", "Enable Deko3D hardware ZCull");
REXCVAR_DEFINE_BOOL(renut_deko3d_depth_compression, false, "Renut", "Enable Deko3D hardware depth compression");

namespace {

class BenignNtCreateFileFilterSink final : public spdlog::sinks::sink {
public:
  explicit BenignNtCreateFileFilterSink(spdlog::sink_ptr inner) : inner_(std::move(inner)) {}

  void log(const spdlog::details::log_msg &msg) override {
    const std::string_view payload(msg.payload.data(), msg.payload.size());
    if (payload.find("[NtCreateFile] FAILED") != std::string_view::npos) {
      if (payload.find("SHADERS_cache") != std::string_view::npos ||
          payload.find(".sdc") != std::string_view::npos ||
          payload.find(".sdrc") != std::string_view::npos ||
          payload.find("dictionary.str") != std::string_view::npos ||
          payload.find("DATABASE") != std::string_view::npos ||
          payload.find("SCENES") != std::string_view::npos ||
          payload.find("VOSplines") != std::string_view::npos ||
          payload.find(".rain") != std::string_view::npos ||
          payload.find("0xc000000f") != std::string_view::npos) {
        return;  // Suppress benign optional file/shader-cache probes from the HUD
      }
    }
    inner_->log(msg);
  }

  void flush() override { inner_->flush(); }
  void set_pattern(const std::string &pattern) override { inner_->set_pattern(pattern); }
  void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override {
    inner_->set_formatter(std::move(sink_formatter));
  }

private:
  spdlog::sink_ptr inner_;
};

void InstallHudLogFilter() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::vector<std::pair<spdlog::sinks::sink *, spdlog::sink_ptr>> wrapped;
    for (const auto &cat : rex::GetAllCategories()) {
      if (!cat.logger)
        continue;
      auto &sinks = cat.logger->sinks();
      for (auto &sink : sinks) {
        if (!sink || dynamic_cast<BenignNtCreateFileFilterSink *>(sink.get())) {
          continue;
        }
        spdlog::sink_ptr replacement;
        for (const auto &entry : wrapped) {
          if (entry.first == sink.get()) {
            replacement = entry.second;
            break;
          }
        }
        if (!replacement) {
          replacement = std::make_shared<BenignNtCreateFileFilterSink>(sink);
          wrapped.emplace_back(sink.get(), replacement);
        }
        sink = replacement;
      }
    }
  });
}

}  // namespace

namespace banjo::renut_gpu {

Latched g_latched = []() {
  Latched l;
  l.half_pixel_offset = true;   // D3D9 0.5f texel center: fixes blurriness and bottom/right border bleeding
  l.depth24 = false;            // Unified 32-bit float depth (ZF32_X24S8): prevents mid-frame D24S8/D24FS8 target split
  l.keep_gpu_written = true;    // Prevent evicting GPU resolve targets
  l.depth_words_stencil = false;// Do NOT inject stencil into low byte of depth words
  l.depth_words_view = true;    // Direct read from dest->words view without extra copy
  l.point_minmax_radius = true; // D3D9 point sizes are radii (8.0f divisor for bokeh/point sprites)
  l.alpha_to_mask_test = false; // Keep translucent smoke/fire particles with alpha < 0.5
  l.draw_resolves = true;       // Hardware-drawn color resolves on deko3d
  l.resolve_skip = true;        // Skip duplicate resolves of unchanged targets
  l.exact_streams = false;      // No CPU XXH3 hash on every kick
  l.depth_words_lazy = true;    // Skip redundant 8-bit dest->words writes unless read
  l.depth_handover = true;      // Zero-copy depth handover when whole target matches
  l.cpu_memos = true;           // Frame memo cache of texture slots and surfaces
  l.pipeline_memo = true;       // 512-set L1 pipeline cache
  l.push_constants = true;      // Fast deko3d push uniform constant lines
  l.shared_dedupe = true;
  l.neon_swap = true;           // NEON SIMD byte-swap of vertices on ARM64
  l.capture_pool = true;        // Pool for render command allocations without heap churn
  l.queue_notify_idle = true;   // Don't spam wake syscalls when render thread is running
  l.push_point_only = true;
  l.depth_only = true;          // Fast Z-prepass without pixel shader
  l.output_trim = false;        // Eliminate specialization 901 runtime compilation storms
  l.alpha_spec = false;         // Eliminate specialization 901 runtime compilation storms
  l.tess_spec = true;
  return l;
}();
std::atomic<bool> g_latched_ready{true};
std::atomic<int> g_ab_off{0};
bool g_pmull_hash = banjo::pmull_fingerprint::kAvailable;
std::atomic<bool> g_trace_frame{false};

void LatchTuning() {
  g_latched.half_pixel_offset = true;
  g_latched.depth24 = false;
  g_latched.keep_gpu_written = true;
  g_latched.depth_words_stencil = false;
  g_latched.depth_words_view = true;
  g_latched.point_minmax_radius = true;
  g_latched.alpha_to_mask_test = false;
  g_latched.draw_resolves = true;
  g_latched.resolve_skip = true;
  g_latched.exact_streams = false;
  g_latched.depth_words_lazy = true;
  g_latched.depth_handover = true;
  g_latched.cpu_memos = true;
  g_latched.pipeline_memo = true;
  g_latched.push_constants = true;
  g_latched.shared_dedupe = true;
  g_latched.neon_swap = true;
  g_latched.capture_pool = true;
  g_latched.queue_notify_idle = true;
  g_latched.push_point_only = true;
  g_latched.depth_only = true;
  g_latched.output_trim = false;
  g_latched.alpha_spec = false;
  g_latched.tess_spec = true;
  g_latched_ready.store(true, std::memory_order_release);
}

uint64_t PmullHash(const void *data, size_t size, uint64_t seed) {
#if BANJO_PMULL_FINGERPRINT
  return banjo::pmull_fingerprint::Fingerprint(static_cast<const uint8_t *>(data), size, seed);
#else
  return XXH3_64bits_withSeed(data, size, seed);
#endif
}

std::filesystem::path NextToExecutable(const std::string &path) {
  const std::filesystem::path p(path);
  return p.is_relative() ? rex::filesystem::GetExecutableFolder() / p : p;
}

void StartLongLivedThread(void (*fn)(), size_t stack_bytes) {
#if defined(__SWITCH__) || defined(REX_PLATFORM_SWITCH)
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (stack_bytes > 0) {
    pthread_attr_setstacksize(&attr, stack_bytes);
  }
  pthread_t thread;
  const int error = pthread_create(
      &thread, &attr, [](void *arg) -> void * {
        reinterpret_cast<void (*)()>(arg)();
        return nullptr;
      },
      reinterpret_cast<void *>(fn));
  pthread_attr_destroy(&attr);
  if (error) {
    std::fprintf(stderr, "[renut] pthread_create failed (%d), %zu KB stack: running as heap thread\n",
                 error, stack_bytes >> 10);
    std::fflush(stderr);
    new std::thread(fn);
  }
#else
  (void)stack_bytes;
  new std::thread(fn);
#endif
}

void OnRenderThreadStart() {
#if defined(__SWITCH__)
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1u << 2);
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x24);
#endif
}
void OnPrebuildWorkerStart() {
#if defined(__SWITCH__)
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, 3, 1u << 3);
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2E);
#endif
}
void OnBackgroundThreadStart(const char *what) {
  (void)what;
#if defined(__SWITCH__)
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, 3, 1u << 3);
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2E);
#endif
}
uint32_t PrebuildWorkers(uint32_t renut_count) { return renut_count; }
bool UploadCached() { return true; }
void CountCards(uint64_t bytes, bool clean) { (void)bytes; (void)clean; }
void CountCardsMissed(uint64_t bytes) { (void)bytes; }
void CountTextureCards(uint64_t bytes, int what) { (void)bytes; (void)what; }
void CountSharedConstants(bool reused) { (void)reused; }
void CountExactCheck(uint64_t bytes, bool stale, bool repeat_in_frame) {
  (void)bytes; (void)stale; (void)repeat_in_frame;
}
void CountKick() {}
void CountDrawKind(bool depth_only, bool depth_only_plain, bool writes_nothing, bool partial_mask) {
  (void)depth_only; (void)depth_only_plain; (void)writes_nothing; (void)partial_mask;
}
uint64_t TextureReserveBytes() { return 256ull << 20; }
uint64_t StreamCacheMaxBytes(uint64_t renut_max) { return renut_max; }
uint64_t StreamCacheIdleFrames(uint64_t renut_frames) { return renut_frames; }
uint32_t UploadTrimResets(uint32_t renut_resets, uint32_t frames_in_flight) {
  (void)frames_in_flight;
  return renut_resets;
}
void CountReserveEviction(uint64_t textures) { (void)textures; }
void ControlNvkLocked() {}
bool RecordWorkerAbOn() { return false; }
void CountDepthWords(bool viewed) { (void)viewed; }
void CountDepthFloatSampled() {}
bool Deko3DDepthCompressionOn() {
  static const bool on = REXCVAR_GET(renut_deko3d_depth_compression);
  return on;
}
bool Deko3DZcullOn() {
  static const bool on = REXCVAR_GET(renut_deko3d_zcull);
  return on;
}
bool Deko3DBarrierMergeOn() { return true; }
bool Deko3DBarrierLeanOn() { return true; }
bool Deko3DVsOutputsOn() { return true; }
bool Deko3DUamPrmtOn() { return false; }
bool EarlyHoistOn() { return true; }
uint32_t TxqMode() { return 1; }
bool StallPresentOn() { return false; }
bool EarlyZOn() { return true; }
bool Gpr32On() { return false; }
bool LightBranchOn() { return false; }
bool SkinOnceOn() { return false; }
bool EagerKicksOn() { return false; }
void CountSkinOnce(uint32_t kind) { (void)kind; }
void CountExpAdjust(int adjust) { (void)adjust; }
void CountSkinMiss(uint32_t fields) { (void)fields; }
bool NullPsOn() { return false; }
bool DeviceSwizzleOn() { return true; }
bool LatencyTrimOn() { return false; }
uint32_t FurShellEvery() { return 1; }
uint32_t PortraitFurShellEvery() { return 1; }
void CountFurShell(bool drawn) { (void)drawn; }
unsigned Deko3DUamFlags() { return 0; }
int Deko3DCompileCore() { return 3; }
int Deko3DCompilePriority() { return 0x2C; }
void CountKickSubmit(int kicks) {
  // Only kicks == 0 represents an actual GPU command list submit; 1 and 2 track RingCleaner flushes.
  if (kicks == 0) {
    timeshift::telemetry::g_frame_kicks.fetch_add(1, std::memory_order_relaxed);
  }
}
void CountExactMemoHit(uint64_t bytes) { (void)bytes; }
void CountRingPublished(uint64_t bytes) {
  timeshift::telemetry::g_frame_upload_bytes.fetch_add(bytes, std::memory_order_relaxed);
}
void CountDeviceSwizzle(uint32_t frame) { (void)frame; }
void CountHeaderDescDiffer(uint32_t frame) { (void)frame; }
void CountDrawnResolve() {
  timeshift::telemetry::g_frame_resolves_drawn.fetch_add(1, std::memory_order_relaxed);
}
void CountSkippedResolve(bool by_clear) {
  (void)by_clear;
  timeshift::telemetry::g_frame_resolves_skipped.fetch_add(1, std::memory_order_relaxed);
}
void CountLazyWords(int what) { (void)what; }
void CountHandOver(int what) {
  if (what >= 0 && what < 6) {
    timeshift::telemetry::g_handover_counts[what].fetch_add(1, std::memory_order_relaxed);
  }
}
void CountPushedConstants(uint64_t bytes, int what) { (void)bytes; (void)what; }
void CountFetchAlignment(bool aligned) { (void)aligned; }
void NotePipelineBuilt() {}
void CountTessGrid(uint64_t corners, uint64_t vertices) { (void)corners; (void)vertices; }
void CountBoolSpec(BoolSpecEvent event) { (void)event; }
void ReportNvk() {}
void BeforePipelineBind(plume::RenderPipeline *pipeline) {
  (void)pipeline;
  timeshift::telemetry::g_frame_pipeline_binds.fetch_add(1, std::memory_order_relaxed);
}
int DiagBuildDelayMs() { return 0; }
void ResolveTraceFrameStart() {}
void BeforeDeviceCreated() {
  InstallHudLogFilter();
#if defined(PLUME_DEKO3D)
  plume::Deko3DOptions opts{};
  opts.zcull = Deko3DZcullOn();
  opts.depthCompression = Deko3DDepthCompressionOn();
  opts.vsOutputs = Deko3DVsOutputsOn();
  opts.uamFlags = Deko3DUamFlags();
  opts.compileCore = Deko3DCompileCore();
  opts.compilePriority = Deko3DCompilePriority();
  opts.mergeBarriers = Deko3DBarrierMergeOn();
  opts.leanBarriers = Deko3DBarrierLeanOn();
  plume::Deko3DSetOptions(opts);
#endif
}
void OnDeviceCreated(plume::RenderDevice *device) { (void)device; }
void BeforeDeviceDestroyed() {}
void StatsGameFrame() {}
void StatsRenderThreadFrame(double busy_ms) { (void)busy_ms; }
void StatsGpuFrame(double ms) { (void)ms; }
void CountDepthOnly() {
  timeshift::telemetry::g_frame_depth_only.fetch_add(1, std::memory_order_relaxed);
}
void CountLateStale(uint64_t copies) { (void)copies; }

}  // namespace banjo::renut_gpu

namespace banjo::renut_gpu::record {

plume::RenderCommandList *Wrap(plume::RenderCommandList *real) { return real; }
void Drain() {}
bool SetBlendConstants(plume::RenderCommandList *cmd, const float rgba[4]) {
  (void)cmd; (void)rgba;
  return false;
}
bool IsProxy(const plume::RenderCommandList *cmd) {
  (void)cmd;
  return false;
}
void Shutdown() {}
void Report(int seconds) { (void)seconds; }

}  // namespace banjo::renut_gpu::record

namespace banjo::renut_gpu::checks {

void QueueKickFlush() {}

}  // namespace banjo::renut_gpu::checks

namespace renut::gpu {

uint32_t GuestScreenWidth() {
  return 1280;
}

}  // namespace renut::gpu

namespace timeshift::telemetry {
namespace {

uint32_t QuantizeTenth(double val) {
  if (val <= 0.0) return 0u;
  const long long rounded = std::llround(val * 10.0);
  if (rounded <= 0) return 0u;
  if (rounded >= 999999LL) return 999999u;
  return static_cast<uint32_t>(rounded);
}

void UpdateLiveFpsStats(double frame_ms, double ren_ms, double gpu_ms, uint32_t draws) {
  const double clamped_ms = std::max(0.01, frame_ms);

  // 0.25s rolling window for the in-game FPS HUD overlay.
  static double hud_ms = 0.0;
  static double hud_max_ms = 0.0;
  static double hud_gpu_ms = 0.0;
  static uint64_t hud_draws = 0;
  static uint32_t hud_frames = 0;
  static bool hud_primed = false;

  hud_ms += clamped_ms;
  hud_max_ms = std::max(hud_max_ms, clamped_ms);
  hud_gpu_ms += gpu_ms;
  hud_draws += draws;
  ++hud_frames;

  if (!hud_primed || hud_ms >= 250.0) {
    const double fps = (1000.0 * double(hud_frames)) / hud_ms;
    const double min_fps = hud_max_ms > 0.1 ? (1000.0 / hud_max_ms) : fps;
    const double avg_ms = hud_ms / double(hud_frames);
    const double avg_gpu = hud_gpu_ms / double(hud_frames);
    g_hud_fps_x10.store(QuantizeTenth(fps), std::memory_order_relaxed);
    g_hud_min_fps_x10.store(QuantizeTenth(min_fps), std::memory_order_relaxed);
    g_hud_frame_ms_x10.store(QuantizeTenth(avg_ms), std::memory_order_relaxed);
    g_hud_gpu_ms_x10.store(QuantizeTenth(avg_gpu), std::memory_order_relaxed);
    g_hud_draws.store(static_cast<uint32_t>(hud_draws / hud_frames), std::memory_order_relaxed);
    hud_primed = true;
    hud_ms = 0.0;
    hud_max_ms = 0.0;
    hud_gpu_ms = 0.0;
    hud_draws = 0;
    hud_frames = 0;
  }

  // 1.0s continuous session logger (fps_log.csv + [fps] log line).
  static double session_time_s = 0.0;
  static double sec_ms = 0.0;
  static double sec_min_ms = 1e9;
  static double sec_max_ms = 0.0;
  static double sec_gpu_ms = 0.0;
  static double sec_ren_ms = 0.0;
  static uint64_t sec_draws = 0;
  static uint32_t sec_frames = 0;
  static bool header_written = false;
  static std::vector<double> sec_samples = [] {
    std::vector<double> v;
    v.reserve(128);
    return v;
  }();

  sec_ms += clamped_ms;
  sec_min_ms = std::min(sec_min_ms, clamped_ms);
  sec_max_ms = std::max(sec_max_ms, clamped_ms);
  sec_gpu_ms += gpu_ms;
  sec_ren_ms += ren_ms;
  sec_draws += draws;
  ++sec_frames;
  sec_samples.push_back(clamped_ms);

  if (sec_ms >= 1000.0) {
    session_time_s += sec_ms / 1000.0;
    std::sort(sec_samples.begin(), sec_samples.end());
    const size_t p95_idx = std::min((sec_samples.size() * 95) / 100, sec_samples.size() - 1);
    const double p95_ms = sec_samples[p95_idx];
    const double fps = (1000.0 * double(sec_frames)) / sec_ms;
    const double min_fps = sec_max_ms > 0.1 ? (1000.0 / sec_max_ms) : 0.0;
    const double max_fps = sec_min_ms > 0.1 ? (1000.0 / sec_min_ms) : 0.0;
    const double avg_ms = sec_ms / double(sec_frames);
    const double avg_gpu = sec_gpu_ms / double(sec_frames);
    const double avg_ren = sec_ren_ms / double(sec_frames);
    const double avg_draws = double(sec_draws) / double(sec_frames);

#if defined(__SWITCH__)
    const char* fps_path = "sdmc:/switch/timeshift/fps_log.csv";
#else
    const char* fps_path = "fps_log.csv";
#endif
    if (FILE* f = std::fopen(fps_path, header_written ? "a" : "w")) {
      if (!header_written) {
        std::fprintf(f, "time_s,frames,fps,min_fps,max_fps,avg_ms,p95_ms,max_ms,gpu_ms,ren_ms,draws\n");
        header_written = true;
      }
      std::fprintf(f, "%.2f,%u,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
                   session_time_s, sec_frames, fps, min_fps, max_fps,
                   avg_ms, p95_ms, sec_max_ms, avg_gpu, avg_ren, avg_draws);
      std::fclose(f);
    }

    REXLOG_INFO(
        "[fps] t={:.1f}s: {:.1f} FPS (min {:.1f}, max {:.1f}) avg={:.1f}ms p95={:.1f}ms "
        "max={:.1f}ms gpu={:.1f}ms ren={:.1f}ms draws={:.0f}",
        session_time_s, fps, min_fps, max_fps, avg_ms, p95_ms, sec_max_ms, avg_gpu, avg_ren, avg_draws);

    sec_ms = 0.0;
    sec_min_ms = 1e9;
    sec_max_ms = 0.0;
    sec_gpu_ms = 0.0;
    sec_ren_ms = 0.0;
    sec_draws = 0;
    sec_frames = 0;
    sec_samples.clear();
  }
}

}  // namespace
}  // namespace timeshift::telemetry

namespace renut::frame_stats {

void OnGameFrame() {}
void OnGpuFrame(double ms) {
  const uint64_t us = ms > 0.0 ? static_cast<uint64_t>(ms * 1000.0) : 0ULL;
  timeshift::telemetry::g_frame_gpu_us.fetch_add(us, std::memory_order_relaxed);
}
void OnRenderThreadFrame(double busy_ms) {
  using Clock = std::chrono::steady_clock;
  static uint32_t s_total_frames = 0;
  static uint32_t s_sample_count = 0;
  static bool s_has_prev = false;
  static Clock::time_point s_prev_tp{};
  static timeshift::telemetry::PerfSample s_samples[timeshift::telemetry::kWindowFrames]{};

  const Clock::time_point now = Clock::now();
  const double frame_ms =
      s_has_prev ? std::chrono::duration<double, std::milli>(now - s_prev_tp).count()
                 : (busy_ms > 0.5 ? busy_ms : 16.67);
  s_prev_tp = now;
  s_has_prev = true;

  const uint32_t frame_idx = s_total_frames++;
  if (frame_idx < timeshift::telemetry::kWarmupFrames) {
    const double cur_gpu_ms =
        double(timeshift::telemetry::g_frame_gpu_us.load(std::memory_order_relaxed)) / 1000.0;
    const uint32_t cur_draws =
        timeshift::telemetry::g_telemetry_frame_draws.load(std::memory_order_relaxed);
    timeshift::telemetry::UpdateLiveFpsStats(frame_ms, busy_ms, cur_gpu_ms, cur_draws);
    timeshift::telemetry::ResetPerFrameCounters();
    if (frame_idx + 1 == timeshift::telemetry::kWarmupFrames) {
      timeshift::telemetry::g_game_core_mask.store(0, std::memory_order_relaxed);
      for (auto& c : timeshift::telemetry::g_handover_counts) {
        c.store(0, std::memory_order_relaxed);
      }
    }
    return;
  }

  timeshift::telemetry::PerfSample& dst = s_samples[s_sample_count++];
  dst.frame_idx = frame_idx;
  dst.frame_ms = frame_ms;
  dst.render_busy_ms = busy_ms;
  dst.gpu_ms =
      double(timeshift::telemetry::g_frame_gpu_us.exchange(0, std::memory_order_relaxed)) / 1000.0;
  dst.game_wait_ms =
      double(timeshift::telemetry::g_frame_game_wait_us.exchange(0, std::memory_order_relaxed)) /
      1000.0;
  dst.draws =
      timeshift::telemetry::g_telemetry_frame_draws.exchange(0, std::memory_order_relaxed);
  dst.pipeline_binds =
      timeshift::telemetry::g_frame_pipeline_binds.exchange(0, std::memory_order_relaxed);
  dst.cmd_submits =
      1u + timeshift::telemetry::g_frame_kicks.exchange(0, std::memory_order_relaxed);
  dst.upload_bytes =
      timeshift::telemetry::g_frame_upload_bytes.exchange(0, std::memory_order_relaxed);
  dst.resolves_drawn =
      timeshift::telemetry::g_frame_resolves_drawn.exchange(0, std::memory_order_relaxed);
  dst.resolves_skipped =
      timeshift::telemetry::g_frame_resolves_skipped.exchange(0, std::memory_order_relaxed);
  dst.depth_only =
      timeshift::telemetry::g_frame_depth_only.exchange(0, std::memory_order_relaxed);
  dst.draw_adp_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_draw_adapter.exchange(0, std::memory_order_relaxed));
  dst.orig_d3d_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_orig_d3d.exchange(0, std::memory_order_relaxed));
  dst.fence_wait_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_fence_wait.exchange(0, std::memory_order_relaxed));
  dst.fence_wait_cnt =
      timeshift::telemetry::g_count_fence_wait.exchange(0, std::memory_order_relaxed);
  dst.fence_qry_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_fence_query.exchange(0, std::memory_order_relaxed));
  dst.fence_qry_cnt =
      timeshift::telemetry::g_count_fence_query.exchange(0, std::memory_order_relaxed);
  dst.tiling_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_tiling.exchange(0, std::memory_order_relaxed));
  dst.tiling_cnt =
      timeshift::telemetry::g_count_tiling.exchange(0, std::memory_order_relaxed);
  dst.swap_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_swap.exchange(0, std::memory_order_relaxed));
  dst.game_frame_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ticks_game_frame.exchange(0, std::memory_order_relaxed));
  dst.ren_dev_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_dev.exchange(0, std::memory_order_relaxed));
  dst.ren_tex_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_tex.exchange(0, std::memory_order_relaxed));
  dst.ren_geo_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_geo.exchange(0, std::memory_order_relaxed));
  dst.ren_const_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_const.exchange(0, std::memory_order_relaxed));
  dst.ren_pipe_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_pipe.exchange(0, std::memory_order_relaxed));
  dst.ren_res_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_res.exchange(0, std::memory_order_relaxed));
  dst.ren_pres_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_ren_ticks_pres.exchange(0, std::memory_order_relaxed));
  dst.tex_memo_hits =
      timeshift::telemetry::g_tex_memo_hits.exchange(0, std::memory_order_relaxed);
  dst.tex_bind_calls =
      timeshift::telemetry::g_tex_bind_calls.exchange(0, std::memory_order_relaxed);
  dst.tex_create_cnt =
      timeshift::telemetry::g_tex_create_cnt.exchange(0, std::memory_order_relaxed);
  dst.tex_upload_cnt =
      timeshift::telemetry::g_tex_upload_cnt.exchange(0, std::memory_order_relaxed);
  dst.tex_upload_bytes =
      timeshift::telemetry::g_tex_upload_bytes.exchange(0, std::memory_order_relaxed);
  dst.tex_sample_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_sample.exchange(0, std::memory_order_relaxed));
  dst.tex_linear_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_linear.exchange(0, std::memory_order_relaxed));
  dst.tex_upload_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_upload.exchange(0, std::memory_order_relaxed));
  dst.tex_create_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_create.exchange(0, std::memory_order_relaxed));
  dst.tex_handover_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_handover.exchange(0, std::memory_order_relaxed));
  dst.tex_sampler_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_tex_ticks_sampler.exchange(0, std::memory_order_relaxed));
  dst.orig_didx_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_orig_ticks_didx.exchange(0, std::memory_order_relaxed));
  dst.orig_didx_cnt =
      timeshift::telemetry::g_orig_cnt_didx.exchange(0, std::memory_order_relaxed);
  dst.orig_up_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_orig_ticks_up.exchange(0, std::memory_order_relaxed));
  dst.orig_up_cnt =
      timeshift::telemetry::g_orig_cnt_up.exchange(0, std::memory_order_relaxed);
  dst.orig_clr_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_orig_ticks_clr.exchange(0, std::memory_order_relaxed));
  dst.orig_clr_cnt =
      timeshift::telemetry::g_orig_cnt_clr.exchange(0, std::memory_order_relaxed);
  dst.orig_res_ms = timeshift::telemetry::TicksToMs(
      timeshift::telemetry::g_orig_ticks_res.exchange(0, std::memory_order_relaxed));
  dst.orig_res_cnt =
      timeshift::telemetry::g_orig_cnt_res.exchange(0, std::memory_order_relaxed);

  timeshift::telemetry::UpdateLiveFpsStats(dst.frame_ms, dst.render_busy_ms, dst.gpu_ms, dst.draws);

  if (s_sample_count == timeshift::telemetry::kWindowFrames) {
    timeshift::telemetry::FlushPerfWindowCsv(s_samples, timeshift::telemetry::kWindowFrames);
    s_sample_count = 0;
  }
}
void OnGameThreadWait(double ms) {
  const uint64_t us = ms > 0.0 ? static_cast<uint64_t>(ms * 1000.0) : 0ULL;
  timeshift::telemetry::g_frame_game_wait_us.fetch_add(us, std::memory_order_relaxed);
}

}  // namespace renut::frame_stats

namespace rex::latency {

void NoteGpuDone(unsigned long frame) {
  (void)frame;
}

}  // namespace rex::latency

namespace rex::system {
struct GpuCreateInfo;
class IGraphicsSystem;
}

REXCVAR_DEFINE_BOOL(renut_skip_shadows, false, "Renut",
                    "Skip 1024x1024 depth-only shadow map draws for higher FPS");
REXCVAR_DEFINE_INT32(renut_far_plane_m, 0, "Renut",
                     "Camera far-plane max distance in meters (0 = original 10000m)");
REXCVAR_DEFINE_STRING(renut_force_level, "", "Renut",
                      "Override level name for automated shader harvesting");
REXCVAR_DECLARE(bool, renut_fps_overlay);
REXCVAR_DECLARE(bool, renut_vsync);
REXCVAR_DECLARE(std::string, renut_anisotropic_filtering);
REXCVAR_DECLARE(bool, renut_skip_orig_d3d);
REXCVAR_DECLARE(bool, renut_deko3d_zcull);
REXCVAR_DECLARE(bool, renut_deko3d_depth_compression);

extern "C" {
void __real_sub_821EFB10(PPCContext& ctx, uint8_t* base);
void __wrap_sub_821EFB10(PPCContext& ctx, uint8_t* base) {
  if (ctx.r5.u32 == 1004u && ctx.r6.u32 >= 0x10000u && ctx.r6.u32 < 0xF0000000u) {
    const uint16_t msg_sz =
        __builtin_bswap16(*reinterpret_cast<const uint16_t*>(base + ctx.r6.u32 + 2));
    char* lvl = reinterpret_cast<char*>(base + ctx.r6.u32 + 4);
    char* sub = reinterpret_cast<char*>(base + ctx.r6.u32 + 264);
    REXLOG_INFO("[level-hook] GS_MSG_LOAD_LEVEL(1004) sz={} lvl='{}' sub='{}'",
                msg_sz, lvl, (msg_sz >= 328) ? sub : "");
    const std::string force = REXCVAR_GET(renut_force_level);
    const bool is_menu = (std::strcmp(lvl, "menu") == 0 || std::strcmp(lvl, "menu.lg") == 0);
    if (!force.empty() && !is_menu && lvl[0] != '\0') {
      std::string target = force;
      const size_t lvl_len = std::strlen(lvl);
      if (lvl_len >= 3 && std::strcmp(lvl + lvl_len - 3, ".lg") == 0 &&
          (target.size() < 3 || target.compare(target.size() - 3, 3, ".lg") != 0)) {
        target += ".lg";
      }
      REXLOG_INFO("[level-hook] Overriding level '{}' -> '{}' (sz={})", lvl, target, msg_sz);
      const size_t n = std::min<size_t>(target.size(), 255);
      std::memcpy(lvl, target.data(), n);
      lvl[n] = '\0';
      if (msg_sz >= 328) {
        sub[0] = '\0';
      }
    }
  }
  __real_sub_821EFB10(ctx, base);
}

void __real_sub_822720D8(PPCContext& ctx, uint8_t* base);
void __wrap_sub_822720D8(PPCContext& ctx, uint8_t* base) {
  const uint32_t r3 = ctx.r3.u32;
  const uint32_t r4 = ctx.r4.u32;
  const uint32_t r5 = ctx.r5.u32;
  char name[64]{};
  if (r4 >= 0x10000u && r4 < 0xF0000000u) {
    for (size_t i = 0; i + 1 < sizeof(name); ++i) {
      const unsigned char ch = base[r4 + i];
      if (!ch) break;
      name[i] = (ch >= 32 && ch < 127) ? char(ch) : '.';
    }
  }
  REXLOG_INFO("[level-hook] sub_822720D8 r3=0x{:08X} r4=0x{:08X}('{}') r5=0x{:X}", r3, r4, name, r5);
  __real_sub_822720D8(ctx, base);
}

void __real_sub_821E1E50(PPCContext& ctx, uint8_t* base);

void __wrap_sub_821E1E50(PPCContext& ctx, uint8_t* base) {
  const uint32_t cam_va = ctx.r4.u32;
  const int32_t far_m = REXCVAR_GET(renut_far_plane_m);
  if (cam_va != 0) {
    struct FarEntry { uint32_t last_written_be = 0; uint32_t orig_be = 0; };
    static std::mutex s_far_mtx;
    static std::unordered_map<uint32_t, FarEntry> s_far_map;
    std::lock_guard<std::mutex> lock(s_far_mtx);
    uint32_t cur_be = *reinterpret_cast<const uint32_t*>(base + cam_va + 132);
    FarEntry& ent = s_far_map[cam_va];
    if (cur_be != ent.last_written_be || ent.orig_be == 0) {
      ent.orig_be = cur_be;
    }
    const uint32_t orig_host = __builtin_bswap32(ent.orig_be);
    float orig_far = 0.0f;
    std::memcpy(&orig_far, &orig_host, sizeof(orig_far));
    if (std::isfinite(orig_far) && orig_far > 25.0f) {
      const float new_far = (far_m <= 0) ? orig_far : std::min(orig_far, float(far_m));
      uint32_t new_host = 0;
      std::memcpy(&new_host, &new_far, sizeof(new_host));
      ent.last_written_be = __builtin_bswap32(new_host);
      *reinterpret_cast<uint32_t*>(base + cam_va + 132) = ent.last_written_be;
    }
  }
  __real_sub_821E1E50(ctx, base);
}

void __real_sub_82281840(PPCContext& ctx, uint8_t* base);

void __wrap_sub_82281840(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj_va = ctx.r3.u32;
  __real_sub_82281840(ctx, base);
  if (REXCVAR_GET(renut_skip_shadows) && obj_va != 0) {
    base[obj_va + 1145] = 0;
  }
}

void __real___imp__XamInputGetState(PPCContext& ctx, uint8_t* base);

void __wrap___imp__XamInputGetState(PPCContext& ctx, uint8_t* base) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  const uint32_t state_va = ctx.r5.u32;
  __real___imp__XamInputGetState(ctx, base);

  if (ctx.r3.u32 != 0 || state_va == 0)
    return;
  if ((user_index & 0xFFu) != 0 && (user_index & 0xFFu) != 0xFFu && (flags & (1u << 30)) == 0)
    return;

  static std::mutex s_input_mtx;
  static uint16_t s_prev_buttons = 0;
  static uint64_t s_start_down_ms = 0;
  static bool s_start_hold_fired = false;
  static uint64_t s_start_tap_until_ms = 0;
  static uint16_t s_repeat_btn = 0;
  static uint64_t s_repeat_next_ms = 0;

  std::lock_guard<std::mutex> lock(s_input_mtx);
  const uint64_t now_ms =
      static_cast<uint64_t>(timeshift::telemetry::TicksToMs(timeshift::telemetry::NowTicks()));
  uint16_t raw_buttons = __builtin_bswap16(*reinterpret_cast<const uint16_t*>(base + state_va + 4));
  uint16_t buttons = raw_buttons;

  constexpr uint16_t kBtnUp = 0x0001;
  constexpr uint16_t kBtnDown = 0x0002;
  constexpr uint16_t kBtnLeft = 0x0004;
  constexpr uint16_t kBtnRight = 0x0008;
  constexpr uint16_t kBtnStart = 0x0010;
  constexpr uint16_t kBtnA = 0x1000;
  constexpr uint16_t kBtnB = 0x2000;

  const bool start_now = (buttons & kBtnStart) != 0;
  const bool start_prev = (s_prev_buttons & kBtnStart) != 0;

  if (start_now && !start_prev) {
    s_start_down_ms = now_ms;
    s_start_hold_fired = false;
  } else if (start_now && !s_start_hold_fired && (now_ms - s_start_down_ms) >= 450) {
    const bool next_open = !timeshift::telemetry::g_menu_open.load(std::memory_order_relaxed);
    timeshift::telemetry::g_menu_open.store(next_open, std::memory_order_relaxed);
    s_start_hold_fired = true;
    s_repeat_btn = 0;
  } else if (!start_now && start_prev && !s_start_hold_fired &&
             !timeshift::telemetry::g_menu_open.load(std::memory_order_relaxed)) {
    // Quick '+' tap still pauses the game normally.
    s_start_tap_until_ms = now_ms + 80;
  }

  const bool menu_open = timeshift::telemetry::g_menu_open.load(std::memory_order_relaxed);
  if (menu_open) {
    auto step_dir = [&](uint16_t mask) -> bool {
      if ((buttons & mask) == 0)
        return false;
      if ((s_prev_buttons & mask) == 0) {
        s_repeat_btn = mask;
        s_repeat_next_ms = now_ms + 350;
        return true;
      }
      if (s_repeat_btn == mask && now_ms >= s_repeat_next_ms) {
        s_repeat_next_ms = now_ms + 120;
        return true;
      }
      return false;
    };

    int cur = timeshift::telemetry::g_menu_cursor.load(std::memory_order_relaxed);
    if (step_dir(kBtnUp)) {
      cur = (cur + 9) % 10;
      timeshift::telemetry::g_menu_cursor.store(cur, std::memory_order_relaxed);
    } else if (step_dir(kBtnDown)) {
      cur = (cur + 1) % 10;
      timeshift::telemetry::g_menu_cursor.store(cur, std::memory_order_relaxed);
    }

    const int delta = step_dir(kBtnRight) ? 1 : (step_dir(kBtnLeft) ? -1 : 0);
    const bool press_a = (buttons & kBtnA) != 0 && (s_prev_buttons & kBtnA) == 0;
    const bool press_b = (buttons & kBtnB) != 0 && (s_prev_buttons & kBtnB) == 0;

    if (press_b) {
      timeshift::telemetry::g_menu_open.store(false, std::memory_order_relaxed);
    } else if (delta != 0 || press_a) {
      const int dir = (delta != 0) ? delta : 1;
      switch (cur) {
        case 0: REXCVAR_SET(renut_fps_overlay, !REXCVAR_GET(renut_fps_overlay)); break;
        case 1: REXCVAR_SET(renut_skip_shadows, !REXCVAR_GET(renut_skip_shadows)); break;
        case 2: {
          static constexpr int32_t kFarModes[] = {0, 200, 120, 80, 55, 40};
          const int32_t cur_m = REXCVAR_GET(renut_far_plane_m);
          int idx = 0;
          for (int i = 0; i < 6; ++i) {
            if (cur_m == kFarModes[i]) { idx = i; break; }
          }
          idx = (idx + dir + 6) % 6;
          REXCVAR_SET(renut_far_plane_m, kFarModes[idx]);
          break;
        }
        case 3: REXCVAR_SET(renut_vsync, !REXCVAR_GET(renut_vsync)); break;
        case 4: {
          static constexpr const char* kAfModes[] = {"Game", "2x", "4x", "8x"};
          const std::string cur_af = REXCVAR_GET(renut_anisotropic_filtering);
          int idx = 1;
          for (int i = 0; i < 4; ++i) {
            if (cur_af == kAfModes[i]) { idx = i; break; }
          }
          idx = (idx + dir + 4) % 4;
          REXCVAR_SET(renut_anisotropic_filtering, std::string(kAfModes[idx]));
          break;
        }
        case 5: REXCVAR_SET(renut_skip_orig_d3d, !REXCVAR_GET(renut_skip_orig_d3d)); break;
        case 6: REXCVAR_SET(renut_deko3d_zcull, !REXCVAR_GET(renut_deko3d_zcull)); break;
        case 7: REXCVAR_SET(renut_deko3d_depth_compression, !REXCVAR_GET(renut_deko3d_depth_compression)); break;
        case 8: {
          const std::string cur_lvl = REXCVAR_GET(renut_force_level);
          int idx = 0;
          for (int i = 0; i < timeshift::telemetry::kCampaignLevelCount; ++i) {
            if (cur_lvl == timeshift::telemetry::kCampaignLevels[i].pak) { idx = i; break; }
          }
          idx = (idx + dir + timeshift::telemetry::kCampaignLevelCount) %
                timeshift::telemetry::kCampaignLevelCount;
          REXCVAR_SET(renut_force_level,
                      std::string(timeshift::telemetry::kCampaignLevels[idx].pak));
          break;
        }
        case 9:
#if defined(__SWITCH__)
          rex::cvar::SaveConfig("sdmc:/switch/timeshift/timeshift.toml");
#else
          rex::cvar::SaveConfig("timeshift.toml");
#endif
          timeshift::telemetry::g_menu_saved_until_ms.store(now_ms + 1500, std::memory_order_relaxed);
          break;
      }
    }

    // Mask all gamepad input from the game while menu is open.
    std::memset(base + state_va + 4, 0, 12);
  } else {
    if (start_now && !s_start_hold_fired) {
      buttons &= static_cast<uint16_t>(~kBtnStart);
    } else if (now_ms < s_start_tap_until_ms) {
      buttons |= kBtnStart;
    }
    *reinterpret_cast<uint16_t*>(base + state_va + 4) = __builtin_bswap16(buttons);
  }

  s_prev_buttons = raw_buttons;
}

uint32_t rex_gpu_abi_version(void) {
  return 1;
}

rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
                                             const rex::system::GpuCreateInfo* info) {
  (void)abi_version;
  (void)info;
  return nullptr;
}
}

