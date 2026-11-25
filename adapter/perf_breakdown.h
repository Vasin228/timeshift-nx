#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace timeshift::telemetry {

inline uint64_t NowTicks() {
#if defined(__SWITCH__)
  uint64_t ticks = 0;
  __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(ticks));
  return ticks;
#else
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}

inline double TicksToMs(uint64_t ticks) {
#if defined(__SWITCH__)
  return double((ticks * 625ull) / 12ull) * 1e-6;
#else
  return double(ticks) * 1e-6;
#endif
}

inline std::atomic<uint64_t> g_ticks_draw_adapter{0};
inline std::atomic<uint64_t> g_ticks_orig_d3d{0};
inline std::atomic<uint64_t> g_ticks_fence_wait{0};
inline std::atomic<uint64_t> g_count_fence_wait{0};
inline std::atomic<uint64_t> g_ticks_fence_query{0};
inline std::atomic<uint64_t> g_count_fence_query{0};
inline std::atomic<uint64_t> g_ticks_tiling{0};
inline std::atomic<uint64_t> g_count_tiling{0};
inline std::atomic<uint64_t> g_ticks_swap{0};
inline std::atomic<uint64_t> g_ticks_game_frame{0};
inline std::atomic<uint64_t> g_ren_ticks_dev{0};
inline std::atomic<uint64_t> g_ren_ticks_tex{0};
inline std::atomic<uint64_t> g_ren_ticks_geo{0};
inline std::atomic<uint64_t> g_ren_ticks_const{0};
inline std::atomic<uint64_t> g_ren_ticks_pipe{0};
inline std::atomic<uint64_t> g_ren_ticks_res{0};
inline std::atomic<uint64_t> g_ren_ticks_pres{0};
inline std::atomic<uint64_t> g_tex_memo_hits{0};
inline std::atomic<uint64_t> g_tex_bind_calls{0};
inline std::atomic<uint64_t> g_tex_create_cnt{0};
inline std::atomic<uint64_t> g_tex_upload_cnt{0};
inline std::atomic<uint64_t> g_tex_upload_bytes{0};
inline std::atomic<uint64_t> g_tex_ticks_sample{0};
inline std::atomic<uint64_t> g_tex_ticks_linear{0};
inline std::atomic<uint64_t> g_tex_ticks_upload{0};
inline std::atomic<uint64_t> g_tex_ticks_create{0};
inline std::atomic<uint64_t> g_tex_ticks_handover{0};
inline std::atomic<uint64_t> g_tex_ticks_sampler{0};
inline std::atomic<uint64_t> g_orig_ticks_didx{0};
inline std::atomic<uint64_t> g_orig_cnt_didx{0};
inline std::atomic<uint64_t> g_orig_ticks_up{0};
inline std::atomic<uint64_t> g_orig_cnt_up{0};
inline std::atomic<uint64_t> g_orig_ticks_clr{0};
inline std::atomic<uint64_t> g_orig_cnt_clr{0};
inline std::atomic<uint64_t> g_orig_ticks_res{0};
inline std::atomic<uint64_t> g_orig_cnt_res{0};
inline std::atomic<uint32_t> g_game_core_mask{0};
inline std::atomic<uint32_t> g_allowed_core_mask{0};
// Thread ON-CPU hardware ticks by role: 0=sim(TID04), 1=draw(TID09), 2=wrk(Saber3D), 3=ren(RenderThread), 4=aud(XAudio/FMOD/XMA), 5=strm(Streaming/Other)
inline std::atomic<uint64_t> g_role_cpu_ticks[6]{};
inline std::atomic<uint64_t> g_handover_counts[6]{};
inline thread_local int t_thread_role = 5;
void AccountThreadCpuTicks(int role);
inline std::atomic<uint32_t> g_hud_fps_x10{0};
inline std::atomic<uint32_t> g_hud_min_fps_x10{0};
inline std::atomic<uint32_t> g_hud_frame_ms_x10{0};
inline std::atomic<uint32_t> g_hud_gpu_ms_x10{0};
inline std::atomic<uint32_t> g_hud_draws{0};
inline std::atomic<bool> g_menu_open{false};
inline std::atomic<int> g_menu_cursor{0};
inline std::atomic<uint64_t> g_menu_saved_until_ms{0};

struct CampaignLevelEntry {
  const char* pak;
  const char* label;
};

inline constexpr CampaignLevelEntry kCampaignLevels[] = {
    {"",                         "OFF"},
    {"ts2_city_part01",          "01 CITY 1"},
    {"ts2_city_part02",          "02 CITY 2"},
    {"ts2_city_part03",          "03 CITY 3"},
    {"ts2_guardfactory_part01",  "04 GUARD 1"},
    {"ts2_guardfactory_part02",  "05 GUARD 2"},
    {"ts2_guardfactory_part03",  "06 GUARD 3"},
    {"ts2_guardfactory_part04",  "07 GUARD 4"},
    {"ts2_guardfactory_part05",  "08 GUARD 5"},
    {"ts2_skyscraper_part1",     "09 TOWER 1"},
    {"ts2_skyscraper_part2",     "10 TOWER 2"},
    {"ts2_skyscraper_part3",     "11 TOWER 3"},
    {"ts2_skyscraper_part4",     "12 TOWER 4"},
    {"ts2_summeroutdoor_part01", "13 OUTDOOR 1"},
    {"ts2_summeroutdoor_part02", "14 OUTDOOR 2"},
    {"ts2_zep_factory_part1",    "15 ZEPFAC 1"},
    {"ts2_zep_factory_part2",    "16 ZEPFAC 2"},
    {"ts2_zep_factory_part3",    "17 ZEPFAC 3"},
    {"ts2_zep_factory_part4",    "18 ZEPFAC 4"},
    {"ts2_zepride1_part01",      "19 ZEPRIDE 1"},
    {"ts2_zepcrashsite_part01",  "20 CRASH 1"},
    {"ts2_zepcrashsite_part02",  "21 CRASH 2"},
    {"ts2_zepcrashsite_part03",  "22 CRASH 3"},
    {"ts2_zepride2_part01",      "23 ZEPRIDE 2"},
    {"ts2_cityfinal_part01",     "24 FINAL"},
};
inline constexpr int kCampaignLevelCount =
    static_cast<int>(sizeof(kCampaignLevels) / sizeof(kCampaignLevels[0]));

inline thread_local int t_fence_wait_depth = 0;

struct FenceWaitScope {
  bool outer = false;
  uint64_t start = 0;

  FenceWaitScope() {
    g_count_fence_wait.fetch_add(1, std::memory_order_relaxed);
    if (t_fence_wait_depth++ == 0) {
      outer = true;
      start = NowTicks();
    }
  }

  ~FenceWaitScope() {
    --t_fence_wait_depth;
    if (outer) {
      const uint64_t end = NowTicks();
      if (end > start) {
        g_ticks_fence_wait.fetch_add(end - start, std::memory_order_relaxed);
      }
    }
  }
};

#ifndef TIMESHIFT_DETAILED_TELEMETRY
#define TIMESHIFT_DETAILED_TELEMETRY 0
#endif

struct ScopedTick {
#if TIMESHIFT_DETAILED_TELEMETRY
  std::atomic<uint64_t>& acc;
  uint64_t start;
  bool active = true;

  explicit ScopedTick(std::atomic<uint64_t>& a) : acc(a), start(NowTicks()) {}

  void Stop() {
    if (!active) return;
    active = false;
    const uint64_t end = NowTicks();
    if (end > start) {
      acc.fetch_add(end - start, std::memory_order_relaxed);
    }
  }

  ~ScopedTick() { Stop(); }
#else
  explicit ScopedTick(std::atomic<uint64_t>&) noexcept {}
  void Stop() noexcept {}
#endif
};

}  // namespace timeshift::telemetry
