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
#include <xxhash.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <pthread.h>
#include <thread>

#if defined(PLUME_DEKO3D) || defined(__SWITCH__) || defined(REX_PLATFORM_SWITCH)
#include <plume_deko3d.h>
#endif
#if defined(__SWITCH__)
#include <switch.h>
#endif

// CVar for diagnostic shaders
REXCVAR_DEFINE_BOOL(banjo_renut_diag_gpu_shaders, false, "Renut", "Diagnostic shader logging");

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
void CountKickSubmit(int kicks) { (void)kicks; }
void CountExactMemoHit(uint64_t bytes) { (void)bytes; }
void CountRingPublished(uint64_t bytes) { (void)bytes; }
void CountDeviceSwizzle(uint32_t frame) { (void)frame; }
void CountHeaderDescDiffer(uint32_t frame) { (void)frame; }
void CountDrawnResolve() {}
void CountSkippedResolve(bool by_clear) { (void)by_clear; }
void CountLazyWords(int what) { (void)what; }
void CountHandOver(int what) { (void)what; }
void CountPushedConstants(uint64_t bytes, int what) { (void)bytes; (void)what; }
void CountFetchAlignment(bool aligned) { (void)aligned; }
void NotePipelineBuilt() {}
void CountTessGrid(uint64_t corners, uint64_t vertices) { (void)corners; (void)vertices; }
void CountBoolSpec(BoolSpecEvent event) { (void)event; }
void ReportNvk() {}
void BeforePipelineBind(plume::RenderPipeline *pipeline) { (void)pipeline; }
int DiagBuildDelayMs() { return 0; }
void ResolveTraceFrameStart() {}
void BeforeDeviceCreated() {
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
void CountDepthOnly() {}
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
  (void)ms;
}
void OnRenderThreadFrame(double busy_ms) {
  (void)busy_ms;
}
void OnGameThreadWait(double ms) {
  (void)ms;
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

