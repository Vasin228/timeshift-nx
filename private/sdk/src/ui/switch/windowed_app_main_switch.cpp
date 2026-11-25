/**
 * @file        ui/switch/windowed_app_main_switch.cpp
 * @brief       Horizon OS entry point for Nintendo Switch (libnx)
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include <switch.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context.h>

namespace {
std::atomic<uint64_t> g_wd_swaps{0};
std::atomic<uint64_t> g_wd_renders{0};
std::atomic<uint64_t> g_wd_audout{0};
std::atomic<uint32_t> g_wd_heap_owner_role{0};
std::atomic<uint32_t> g_wd_heap_op{0};
std::atomic<uint32_t> g_wd_draw_stage{0};
std::atomic<uint32_t> g_wd_render_stage{0};
std::atomic<bool> g_wd_stop{false};

struct RexWatchdogThreadSnap {
  uint32_t tid;
  uint32_t start;
  uint32_t lr;
  uint32_t ctr;
  uint32_t r1;
  uint32_t r3;
  uint8_t is_guest;
  uint8_t is_running;
  uint16_t pad;
};
}  // namespace

extern "C" {
int rex_kernel_snapshot_threads_trylock(RexWatchdogThreadSnap* out, int max_count);
void rex_watchdog_note_swap(uint64_t swaps) {
  g_wd_swaps.store(swaps, std::memory_order_relaxed);
}
void rex_watchdog_note_render(uint64_t frames) {
  g_wd_renders.store(frames, std::memory_order_relaxed);
}
void rex_watchdog_note_audout(uint64_t buffers) {
  g_wd_audout.store(buffers, std::memory_order_relaxed);
}
void rex_watchdog_set_heap_owner(uint32_t role_plus_one) {
  g_wd_heap_owner_role.store(role_plus_one, std::memory_order_relaxed);
}
void rex_watchdog_set_heap_op(uint32_t op) {
  g_wd_heap_op.store(op, std::memory_order_relaxed);
}
void rex_watchdog_set_draw_stage(uint32_t stage) {
  g_wd_draw_stage.store(stage, std::memory_order_relaxed);
}
void rex_watchdog_set_render_stage(uint32_t stage) {
  g_wd_render_stage.store(stage, std::memory_order_relaxed);
}
}

extern "C" {
  // Title override application budget (gives ~3.1 GB usable process RAM)
  u32 __nx_applet_type = AppletType_Application;

  // Fallback newlib heap size limit if svcGetInfo fails
  size_t __nx_heap_size = 2432ull * 1024ull * 1024ull;

  // Claim all available process memory for the newlib heap when launched directly
  // (e.g. in Ryujinx without hbloader's envHasHeapOverride).
  void __libnx_initheap(void) {
    void* addr = nullptr;
    size_t size = 0;
    if (envHasHeapOverride()) {
      addr = envGetHeapOverrideAddr();
      size = envGetHeapOverrideSize();
    } else {
      u64 available = 0, used = 0;
      svcGetInfo(&available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
      svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
      if (available > used + 0x200000) {
        size = (available - used - 0x200000) & ~size_t(0x1FFFFF);
      }
      if (size < __nx_heap_size) {
        size = __nx_heap_size;
      }
      Result rc = svcSetHeapSize(&addr, size);
      if (R_FAILED(rc) && size != __nx_heap_size) {
        size = __nx_heap_size;
        rc = svcSetHeapSize(&addr, size);
      }
      if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
      }
    }
    __nx_heap_size = size;
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = static_cast<char*>(addr);
    fake_heap_end = static_cast<char*>(addr) + size;
  }
}

namespace rex::ui {

class SwitchWindowedAppContext : public WindowedAppContext {
 public:
  SwitchWindowedAppContext() = default;
  ~SwitchWindowedAppContext() override = default;

  bool Initialize() {
    return true;
  }

  int RunMainMessageLoop() {
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, (1u << 1) | (1u << 2));
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x22);

    using Clock = std::chrono::steady_clock;
    const auto start_tp = Clock::now();
    auto last_hb_tp = start_tp;
    auto last_dump_tp = start_tp;

    while (appletMainLoop() && !HasQuitFromUIThread()) {
      ExecutePendingFunctionsFromUIThread();
      svcSleepThread(10000000ULL);  // 10 ms tick (<0.01% CPU)

      const auto now = Clock::now();
      if ((now - last_hb_tp) >= std::chrono::seconds(10)) {
        last_hb_tp = now;
        const auto elapsed_s =
            std::chrono::duration_cast<std::chrono::seconds>(now - start_tp).count();
        REXLOG_INFO("[heartbeat] UI loop running, time={}s swaps={} renders={} audout={}",
                    elapsed_s, g_wd_swaps.load(std::memory_order_relaxed),
                    g_wd_renders.load(std::memory_order_relaxed),
                    g_wd_audout.load(std::memory_order_relaxed));
      }

      if ((now - last_dump_tp) >= std::chrono::seconds(30)) {
        last_dump_tp = now;
        if (auto* ks = rex::system::kernel_state()) {
          ks->DumpThreads();
        }
      }
    }
    return EXIT_SUCCESS;
  }

 protected:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

}  // namespace rex::ui

namespace {

void WriteWatchdogDump(const char* buf, size_t len, bool truncate, bool mirror_stderr) {
  const int flags = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND);
  const int fd = open("sdmc:/switch/timeshift/freeze_dump.txt", flags, 0644);
  if (fd >= 0) {
    size_t off = 0;
    while (off < len) {
      const ssize_t w = write(fd, buf + off, len - off);
      if (w <= 0) break;
      off += size_t(w);
    }
    close(fd);
  }
  if (mirror_stderr) {
    size_t off = 0;
    while (off < len) {
      const ssize_t w = write(STDERR_FILENO, buf + off, len - off);
      if (w <= 0) break;
      off += size_t(w);
    }
  }
}

void WatchdogThreadMain() {
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 0x7u);
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);

  static constexpr char kArmBanner[] = "=== TimeShift Black-Box Watchdog armed ===\n";
  WriteWatchdogDump(kArmBanner, sizeof(kArmBanner) - 1, true, false);

  using Clock = std::chrono::steady_clock;
  const auto start_tp = Clock::now();
  auto last_swap_change_tp = start_tp;
  uint64_t last_swaps = 0;
  int dump_stage = 0;
  static constexpr long long kStageMs[3] = {2500, 6000, 15000};

  while (!g_wd_stop.load(std::memory_order_relaxed)) {
    svcSleepThread(50000000ULL);  // 50 ms tick
    if (g_wd_stop.load(std::memory_order_relaxed)) break;

    const auto now = Clock::now();
    const uint64_t cur_swaps = g_wd_swaps.load(std::memory_order_relaxed);
    if (cur_swaps != last_swaps) {
      last_swaps = cur_swaps;
      last_swap_change_tp = now;
      dump_stage = 0;
      continue;
    }
    if (cur_swaps < 10 || dump_stage >= 3) {
      continue;
    }
    const long long stall_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_swap_change_tp).count();
    if (stall_ms < kStageMs[dump_stage]) {
      continue;
    }

    const uint64_t r0 = g_wd_renders.load(std::memory_order_relaxed);
    const uint64_t a0 = g_wd_audout.load(std::memory_order_relaxed);
    const uint32_t ho0 = g_wd_heap_owner_role.load(std::memory_order_relaxed);
    const uint32_t hop0 = g_wd_heap_op.load(std::memory_order_relaxed);
    const uint32_t ds0 = g_wd_draw_stage.load(std::memory_order_relaxed);
    const uint32_t rs0 = g_wd_render_stage.load(std::memory_order_relaxed);

    RexWatchdogThreadSnap snap0[64]{};
    RexWatchdogThreadSnap snap1[64]{};
    int n0 = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
      n0 = rex_kernel_snapshot_threads_trylock(snap0, 64);
      if (n0 >= 0) break;
      svcSleepThread(10000000ULL);
    }

    svcSleepThread(250000000ULL);
    if (g_wd_stop.load(std::memory_order_relaxed)) break;

    const uint64_t r1 = g_wd_renders.load(std::memory_order_relaxed);
    const uint64_t a1 = g_wd_audout.load(std::memory_order_relaxed);
    const uint32_t ho1 = g_wd_heap_owner_role.load(std::memory_order_relaxed);
    const uint32_t hop1 = g_wd_heap_op.load(std::memory_order_relaxed);
    const uint32_t ds1 = g_wd_draw_stage.load(std::memory_order_relaxed);
    const uint32_t rs1 = g_wd_render_stage.load(std::memory_order_relaxed);
    const int n1 = (n0 >= 0) ? rex_kernel_snapshot_threads_trylock(snap1, 64) : -1;

    const long long uptime_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_tp).count();

    static char buf[16384];
    size_t pos = 0;
    auto append = [&](int n) {
      if (n > 0 && pos < sizeof(buf) - 1) {
        const size_t avail = sizeof(buf) - 1 - pos;
        pos += size_t(n) < avail ? size_t(n) : avail;
      }
    };

    append(std::snprintf(
        buf + pos, sizeof(buf) - pos,
        "\n[BLACKBOX stage=%d uptime=%lldms stall=%lldms swaps=%llu renders=%llu->%llu "
        "audout=%llu->%llu heap_owner=%u->%u heap_op=%u->%u draw_stage=%u->%u "
        "render_stage=%u->%u gcr_locked=%d]\n",
        dump_stage, uptime_ms, stall_ms, static_cast<unsigned long long>(cur_swaps),
        static_cast<unsigned long long>(r0), static_cast<unsigned long long>(r1),
        static_cast<unsigned long long>(a0), static_cast<unsigned long long>(a1),
        ho0, ho1, hop0, hop1, ds0, ds1, rs0, rs1, (n0 < 0) ? 1 : 0));

    if (n0 > 0) {
      for (int i = 0; i < n0 && pos + 160 < sizeof(buf); ++i) {
        const auto& s0 = snap0[i];
        const RexWatchdogThreadSnap* s1 = nullptr;
        if (n1 > 0) {
          for (int j = 0; j < n1; ++j) {
            if (snap1[j].tid == s0.tid) {
              s1 = &snap1[j];
              break;
            }
          }
        }
        const char* tag = "WAIT";
        if (s0.is_running != 0) {
          if (!s1) {
            tag = "UNK";
          } else if (s0.lr != s1->lr || s0.ctr != s1->ctr || s0.r1 != s1->r1 || s0.r3 != s1->r3) {
            tag = "MOVING";
          } else {
            tag = "STUCK";
          }
        }
        append(std::snprintf(
            buf + pos, sizeof(buf) - pos,
            "TID=%08X start=%08X guest=%u run=%u lr=%08X->%08X ctr=%08X->%08X "
            "r1=%08X->%08X r3=%08X->%08X %s\n",
            s0.tid, s0.start, unsigned(s0.is_guest), unsigned(s0.is_running),
            s0.lr, s1 ? s1->lr : s0.lr, s0.ctr, s1 ? s1->ctr : s0.ctr,
            s0.r1, s1 ? s1->r1 : s0.r1, s0.r3, s1 ? s1->r3 : s0.r3, tag));
      }
    }

    WriteWatchdogDump(buf, pos, false, true);
    ++dump_stage;
  }
}

int RunWindowedApp(int argc, char** argv) {
  // NVK keeps a pipeline cache on the SD card and writes it whenever a new
  // pipeline is compiled - that first write is where the S4 runs died. Mesa
  // caches are not worth an SD card round trip on a console, so keep them in
  // RAM (and disabled) instead.
  setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
  setenv("MESA_GLSL_CACHE_DISABLE", "true", 1);
  setenv("MESA_SHADER_CACHE_DIR", "/tmp", 1);

  mkdir("sdmc:/switch", 0755);
  mkdir("sdmc:/switch/timeshift", 0755);
  mkdir("sdmc:/switch/timeshift/user", 0755);
  FILE* log_fp = freopen("sdmc:/switch/timeshift/timeshift.log", "w", stdout);
  if (log_fp) {
    dup2(fileno(stdout), fileno(stderr));
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
  }

  printf("=== TimeShift Switch bootstrap starting ===\n");
  fflush(stdout);

  static char default_arg0[] = "timeshift";
  static char* default_argv[] = {default_arg0, nullptr};
  if (argc <= 0 || !argv || !argv[0]) {
    argc = 1;
    argv = default_argv;
  }

  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  REXLOG_INFO("ReXGlue Switch bootstrap starting...");

  g_wd_stop.store(false, std::memory_order_release);
  std::thread watchdog_thread(WatchdogThreadMain);

  int result;
  {
    rex::ui::SwitchWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      g_wd_stop.store(true, std::memory_order_release);
      if (watchdog_thread.joinable()) watchdog_thread.join();
      return EXIT_FAILURE;
    }

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }

  g_wd_stop.store(true, std::memory_order_release);
  if (watchdog_thread.joinable()) watchdog_thread.join();

  rex::FlushLogging();
  std::fflush(stdout);
  std::fflush(stderr);
  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}
