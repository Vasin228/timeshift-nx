/**
 * @file        ui/switch/windowed_app_main_switch.cpp
 * @brief       Horizon OS entry point for Nintendo Switch (libnx)
 */

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

extern "C" {
  // Title override application budget (gives ~3.1 GB usable process RAM)
  u32 __nx_applet_type = AppletType_Application;

  // Newlib heap size limit: 1024 MB
  u64 __nx_heap_size = 1792ull * 1024ull * 1024ull;
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
    uint64_t tick = 0;
    while (appletMainLoop() && !HasQuitFromUIThread()) {
      ExecutePendingFunctionsFromUIThread();
      svcSleepThread(1000000ULL); // yield 1 ms
      if (++tick % 3000 == 0) {
        REXLOG_INFO("[heartbeat] UI loop running, time={}s", tick / 1000);
        if (tick % 15000 == 0) {
          auto* ks = rex::system::kernel_state();
          if (ks) {
            ks->DumpThreads();
          }
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
  spdlog::flush_on(spdlog::level::trace);
  spdlog::flush_every(std::chrono::seconds(1));

  REXLOG_INFO("ReXGlue Switch bootstrap starting...");

  int result;
  {
    rex::ui::SwitchWindowedAppContext app_context;
    if (!app_context.Initialize()) {
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

  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}
