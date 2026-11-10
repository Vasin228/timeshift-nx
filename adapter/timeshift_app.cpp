// timeshift_app.cpp - TimeShift ReNut/deko3d Application & Lifecycle
#include <rex/rex_app.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <memory>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <filesystem>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "timeshift_pch.h"
#include "renut_engine/gpu/overlay/app_integration.h"
#include "renut_engine/gpu/backend/video.h"
#include "shader_runtime_bridge.h"

namespace rex {
REXCVAR_DECLARE(std::string, gpu_backend);
}

// Failing ICD entry points when NVK is not linked (PLUME_SWITCH_NO_NVK)
extern "C" {
void* vk_icdGetInstanceProcAddr(void* instance, const char* name) {
  (void)instance;
  (void)name;
  return nullptr;
}

int32_t vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* version) {
  (void)version;
  return -9; // VK_ERROR_INCOMPATIBLE_DRIVER
}
}

namespace {
void InstallCrashHandlers() {
  static bool s_installed = false;
  if (s_installed) return;
  s_installed = true;

  std::set_terminate([]() {
    std::fprintf(stderr, "\n[FATAL] std::terminate() called!\n");
    try {
      auto ex = std::current_exception();
      if (ex) {
        std::rethrow_exception(ex);
      } else {
        std::fprintf(stderr, "[FATAL] No active exception.\n");
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::fprintf(stderr, "[FATAL] std::filesystem::filesystem_error: what='%s', p1='%s', p2='%s', code=%d\n",
                   e.what(), e.path1().string().c_str(), e.path2().string().c_str(), e.code().value());
    } catch (const std::system_error& e) {
      std::fprintf(stderr, "[FATAL] std::system_error: what='%s', code=%d, cat='%s'\n",
                   e.what(), e.code().value(), e.code().category().name());
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[FATAL] Uncaught std::exception: type='%s', what='%s'\n",
                   typeid(e).name(), e.what());
    } catch (...) {
      std::fprintf(stderr, "[FATAL] Uncaught non-standard exception!\n");
    }
    std::fflush(stderr);
#if defined(__SWITCH__)
    svcSleepThread(500000000ULL);
#endif
  });

  std::atexit([]() {
    std::fprintf(stderr, "[app] atexit() hook called: process terminating normally.\n");
    std::fflush(stderr);
  });

  auto sig_handler = [](int sig) {
    std::fprintf(stderr, "\n[FATAL] Caught signal: %d!\n", sig);
    std::fflush(stderr);
#if defined(__SWITCH__)
    svcSleepThread(500000000ULL);
    svcExitProcess();
#else
    std::abort();
#endif
  };

  signal(SIGSEGV, sig_handler);
  signal(SIGABRT, sig_handler);
  signal(SIGFPE, sig_handler);
  signal(SIGILL, sig_handler);
  signal(SIGBUS, sig_handler);
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);
}
}  // namespace

namespace timeshift::app {

class TimeshiftRenutApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    InstallCrashHandlers();
    std::fprintf(stderr, "[app] TimeshiftRenutApp::Create - crash boundaries & signal handlers active\n");
    std::fflush(stderr);
    return std::unique_ptr<TimeshiftRenutApp>(new TimeshiftRenutApp(ctx, "timeshift", PPCImageConfig));
  }

  static bool IsRenutBackendSelected() {
    // 1. Explicit backend selection file takes precedence if present
    if (FILE* f = std::fopen("sdmc:/switch/timeshift/backend.txt", "r")) {
      char buf[64] = {};
      if (std::fgets(buf, sizeof(buf) - 1, f)) {
        std::string s(buf);
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
        std::fclose(f);
        if (s == "vulkan") {
          return false;
        }
        if (s == "renut" || s == "deko3d") {
          return true;
        }
      } else {
        std::fclose(f);
      }
    }

    // 2. CVar gpu_backend check
    std::string cvar_val = std::string(rex::FLAGS_gpu_backend_storage_());
    if (cvar_val == "renut" || cvar_val == "deko3d") {
      return true;
    }
    if (cvar_val == "vulkan") {
      // In this private ReNut executable target, the default backend is ReNut (dormant mode),
      // unless backend.txt explicitly specified "vulkan".
      return true;
    }
    return true;
  }

  protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (IsRenutBackendSelected()) {
      renut::gpu::ConfigureRuntime(config);
      timeshift::renderer::SetCoordinatedBackendActive(true);
      REXLOG_INFO("[renut] Backend: deko3d / UAM (Switch native Tegra X1)");
      REXLOG_INFO("[renut] Upstream revision: 32a81a83aafd83e25d3a3cccae34f567766402c6, XEX: 0eb9b13de4b44b14f89907b91973209112bca8f229cf7b55e26cc5daa0841050");
      REXLOG_INFO("[renut] Coordinated activation: ACTIVE (ResourceHooksEnabled == true)");
    } else {
      timeshift::renderer::SetCoordinatedBackendActive(false);
      REXLOG_INFO("[gpu] Fallback Vulkan reference backend selected");
    }
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (IsRenutBackendSelected()) {
      // Start renderer after the window has been created by SetupPresentation
      if (!renut::gpu::StartRenderer(app_context(), [] { return nullptr; })) {
        REXLOG_ERROR("[gpu] ReNut Video / renderer initialization failed: device creation or swapchain error");
        return std::nullopt;
      }
      REXLOG_INFO("[gpu] ReNut Video / renderer initialized successfully; presentation queue ready");
    }
    return rex::ReXApp::OnFinalizePaths(defaults, std::move(resume));
  }

  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
    rex::ReXApp::OnWindowPixelSizeChanged(pixel_width, pixel_height);
    if (IsRenutBackendSelected()) {
      renut::gpu::OnWindowResized();
    }
  }

  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    // Optional immediate drawer absent on Switch platform
    return nullptr;
  }

  void OnShutdown() override {
    if (IsRenutBackendSelected()) {
      renut::gpu::ShutdownRenderer();
      REXLOG_INFO("[gpu] ReNut Video / renderer shut down cleanly");
    }
    rex::ReXApp::OnShutdown();
  }
};

} // namespace timeshift::app

REX_DEFINE_APP(timeshift, timeshift::app::TimeshiftRenutApp::Create)
