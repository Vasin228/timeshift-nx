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
#include "renut_engine/gpu/guest/widescreen.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/frame_latency.h>
#include <rex/logging.h>
#include <spdlog/sinks/sink.h>
#include <string_view>
#include <xxhash.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <thread>
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
}

void FlushPerfWindowCsv(const PerfSample* samples, uint32_t count) {
  static char csv_buf[65536];
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
      "cmd_submits,upload_bytes,resolves_drawn,resolves_skipped,depth_only\n"));

  double sum_frame_ms = 0.0;
  double sum_render_ms = 0.0;
  double sum_gpu_ms = 0.0;
  double sum_wait_ms = 0.0;
  uint64_t sum_draws = 0;
  uint64_t sum_binds = 0;
  uint64_t sum_submits = 0;
  uint64_t sum_upload_bytes = 0;
  double sorted_frame_ms[kWindowFrames];

  for (uint32_t i = 0; i < count; ++i) {
    const PerfSample& s = samples[i];
    sum_frame_ms += s.frame_ms;
    sum_render_ms += s.render_busy_ms;
    sum_gpu_ms += s.gpu_ms;
    sum_wait_ms += s.game_wait_ms;
    sum_draws += s.draws;
    sum_binds += s.pipeline_binds;
    sum_submits += s.cmd_submits;
    sum_upload_bytes += s.upload_bytes;
    sorted_frame_ms[i] = s.frame_ms;

    if (pos + 160 < sizeof(csv_buf)) {
      append(std::snprintf(
          csv_buf + pos, sizeof(csv_buf) - pos,
          "%u,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%llu,%u,%u,%u\n",
          s.frame_idx, s.frame_ms, s.render_busy_ms, s.gpu_ms, s.game_wait_ms,
          s.draws, s.pipeline_binds, s.cmd_submits,
          static_cast<unsigned long long>(s.upload_bytes),
          s.resolves_drawn, s.resolves_skipped, s.depth_only));
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
  REXLOG_INFO(
      "[perf] 300f CSV: avg={:.2f}ms ({:.1f} FPS, 1%low={:.1f}), render={:.2f}ms, "
      "gpu={:.2f}ms, wait={:.2f}ms, draws={:.0f}, binds={:.0f}, submits={:.1f}, upload={:.1f}KB",
      avg_frame_ms, avg_fps, low_1pct_fps, avg_render_ms, avg_gpu_ms,
      avg_wait_ms, avg_draws, avg_binds, avg_submits, avg_upload_kb);
}
}  // namespace
}  // namespace timeshift::telemetry

// CVar for diagnostic shaders
REXCVAR_DEFINE_BOOL(banjo_renut_diag_gpu_shaders, false, "Renut", "Diagnostic shader logging");

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
  l.depth_words_lazy = false;   // Always update dest->words in ResolveDepth
  l.depth_handover = false;     // Always perform full shader resolve depth
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
bool g_pmull_hash = false;
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
  g_latched.depth_words_lazy = false;
  g_latched.depth_handover = false;
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
  return XXH3_64bits_withSeed(data, size, seed);
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
bool Deko3DDepthCompressionOn() { return false; }
bool Deko3DZcullOn() { return false; }
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
void CountHandOver(int what) { (void)what; }
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
      s_has_prev ? std::chrono::duration<double, std::milli>(now - s_prev_tp).count() : busy_ms;
  s_prev_tp = now;
  s_has_prev = true;

  const uint32_t frame_idx = s_total_frames++;
  if (frame_idx < timeshift::telemetry::kWarmupFrames) {
    timeshift::telemetry::ResetPerFrameCounters();
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

extern "C" {
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

