/**
 * @file        ui/switch/rex_app_switch.cpp
 * @brief       ReXApp Switch platform implementation
 */

#include <rex/rex_app.h>

#include <rex/ui/imgui_drawer.h>
#include <rex/ui/overlay/console_overlay.h>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/ui/overlay/achievements_overlay.h>
#include <rex/ui/overlay/achievement_toast.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <algorithm>

#include <fmt/format.h>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/audio/audio_system.h>
#include <rex/audio/nop/nop_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/kernel/crt/heap.h>
#include <rex/kernel/init.h>
#include <rex/system.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/ui/window.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app.h>
#include <rex/version.h>

#if defined(__SWITCH__)
#include <switch.h>

#include <sys/stat.h>
#endif

namespace rex {

REXCVAR_DEFINE_STRING(gpu_backend, "vulkan", "GPU",
                      "rexgpu-xenos backend: vulkan (Xenos emulation) or native (Part 111)");

namespace {

#if defined(__SWITCH__)
// Part 109/158: log AND set CPU/GPU/EMC clocks to max safe rates so the frame
// budget is not wasted on stock throttled clocks.  If sys-clk is already
// managing frequencies this block is a no-op (we detect its presence and skip
// the set calls so sys-clk's profile wins).
//
// Safe max rates for all retail Switch variants:
//   CPU 1785 MHz  (Erista+Mariko; Mariko can do 1963 but 1785 is CFW-safe)
//   GPU  768 MHz  (Erista-safe; Mariko can do 921 but we stay conservative)
//   EMC 1600 MHz  (Erista bus cap; Mariko selects its own optimum)
//
// These are the values used by nfsmw-nx, sa2-nx, and project8-sw.
static void ApplySwitchPerformanceConfig() {
  u32 perf_normal = 0;
  u32 perf_boost = 0;
  if (R_SUCCEEDED(apmInitialize())) {
    apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &perf_normal);
    apmGetPerformanceConfiguration(ApmPerformanceMode_Boost, &perf_boost);
    apmExit();
  }
  u32 cpu_hz = 0;
  u32 gpu_hz = 0;
  bool clocks_read = false;
  bool clocks_set = false;
  struct stat st {};
  const bool sysclk_present = stat("sdmc:/atmosphere/contents/00FF0000636C6BFF", &st) == 0;

  if (R_SUCCEEDED(clkrstInitialize())) {
    // CPU (1785 MHz: safe high-performance clock for Switch)
    ClkrstSession cpu_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&cpu_session, PcvModuleId_CpuBus, 3))) {
      clocks_read = R_SUCCEEDED(clkrstGetClockRate(&cpu_session, &cpu_hz)) || clocks_read;
      const Result set_rc = clkrstSetClockRate(&cpu_session, 1785000000u);
      clocks_set = R_SUCCEEDED(set_rc) || clocks_set;
      clkrstGetClockRate(&cpu_session, &cpu_hz);
      clkrstCloseSession(&cpu_session);
    }
    // GPU (768 MHz: safe high-performance clock for GM20B)
    ClkrstSession gpu_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&gpu_session, PcvModuleId_GPU, 3))) {
      clocks_read = R_SUCCEEDED(clkrstGetClockRate(&gpu_session, &gpu_hz)) || clocks_read;
      const Result set_rc = clkrstSetClockRate(&gpu_session, 768000000u);
      clocks_set = R_SUCCEEDED(set_rc) || clocks_set;
      clkrstGetClockRate(&gpu_session, &gpu_hz);
      clkrstCloseSession(&gpu_session);
    }
    // EMC (1600 MHz memory bus: full memory bandwidth)
    ClkrstSession emc_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&emc_session, PcvModuleId_EMC, 3))) {
      clkrstSetClockRate(&emc_session, 1600000000u);
      clkrstCloseSession(&emc_session);
    }
    clkrstExit();
  }
  if (!clocks_set && R_SUCCEEDED(pcvInitialize())) {
    pcvSetClockRate(PcvModule_CpuBus, 1785000000u);
    pcvSetClockRate(PcvModule_GPU, 768000000u);
    pcvSetClockRate(PcvModule_EMC, 1600000000u);
    pcvGetClockRate(PcvModule_CpuBus, &cpu_hz);
    pcvGetClockRate(PcvModule_GPU, &gpu_hz);
    clocks_set = (cpu_hz > 1020000000u);
    pcvExit();
  }
  REXLOG_INFO(
      "[perfcfg] apm_config_normal={} apm_config_boost={} cpu_mhz={} gpu_mhz={} "
      "clocks_read={} clocks_set={} sysclk_present={}",
      perf_normal, perf_boost, cpu_hz / 1000000u, gpu_hz / 1000000u,
      clocks_read, clocks_set, sysclk_present);
}
// Keep the original name used at the one call site below.
static void LogSwitchPerformanceConfig() { ApplySwitchPerformanceConfig(); }
#endif

}  // namespace

ReXApp::~ReXApp() = default;

ReXApp::ReXApp(ui::WindowedAppContext& ctx, std::string_view name, PPCImageInfo ppc_info,
               std::string_view usage)
    : WindowedApp(ctx, name, usage), ppc_info_(ppc_info) {}

std::unique_ptr<ui::ImGuiDialog> ReXApp::CreateAchievementsOverlay() {
  return nullptr;
}

std::unique_ptr<ui::AchievementNotificationDialog> ReXApp::CreateAchievementNotificationDialog() {
  return nullptr;
}

system::AchievementManager& ReXApp::achievements() const {
  assert_not_null(runtime_);
  assert_not_null(runtime_->kernel_state());
  return runtime_->kernel_state()->achievements();
}

bool ReXApp::SetupEnvironment() {
  std::filesystem::path game_dir = "sdmc:/switch/timeshift/game";
  std::string game_data_cvar = REXCVAR_GET(game_data_root);
  if (!game_data_cvar.empty()) {
    game_dir = game_data_cvar;
  }

  std::filesystem::path user_dir = "sdmc:/switch/timeshift/user";
  std::string user_data_cvar = REXCVAR_GET(user_data_root);
  if (!user_data_cvar.empty()) {
    user_dir = user_data_cvar;
  }

  std::filesystem::path update_dir = "sdmc:/switch/timeshift/update";
  std::string update_data_cvar = REXCVAR_GET(update_data_root);
  if (!update_data_cvar.empty()) {
    update_dir = update_data_cvar;
  }

  std::filesystem::path cache_dir = "sdmc:/switch/timeshift/cache";
  std::string cache_root_cvar = REXCVAR_GET(cache_root);
  if (!cache_root_cvar.empty()) {
    cache_dir = cache_root_cvar;
  }

  std::filesystem::path metadata_dir = "sdmc:/switch/timeshift/metadata";
  std::string metadata_root_cvar = REXCVAR_GET(metadata_root);
  if (!metadata_root_cvar.empty()) {
    metadata_dir = metadata_root_cvar;
  }

  resolved_defaults_ = {
      .game_data_root = std::move(game_dir),
      .user_data_root = std::move(user_dir),
      .update_data_root = std::move(update_dir),
      .cache_root = std::move(cache_dir),
      .metadata_root = std::move(metadata_dir),
      .config_path = "sdmc:/switch/timeshift/timeshift.toml",
  };

  OnConfigurePaths(resolved_defaults_);
  if (std::filesystem::exists(resolved_defaults_.config_path)) {
    rex::cvar::LoadConfig(resolved_defaults_.config_path);
    REXLOG_INFO("[config] Loaded configuration from '{}'", resolved_defaults_.config_path.string());
  } else {
    REXLOG_WARN("[config] Configuration file not found at '{}'", resolved_defaults_.config_path.string());
  }
  return true;
}

bool ReXApp::SetupPresentation() {
  setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);

#if defined(__SWITCH__)
  LogSwitchPerformanceConfig();
#endif

  config_.gpu_plugin = "xenos";
  config_.audio_factory = REX_AUDIO_BACKEND(rex::audio::nop::NopAudioSystem);
  config_.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  config_.kernel_init = rex::kernel::InitializeKernel;

  OnPreSetup(config_);

  if (!config_.graphics && !config_.gpu_plugin.empty()) {
    std::string backend_name = REXCVAR_GET(gpu_backend);
    // Temporary (Part 112 debugging): the toml value did not reach the cvar on
    // the console, so an explicit one-word file wins when present.
    if (FILE* backend_file = fopen("sdmc:/switch/timeshift/backend.txt", "r")) {
      char backend_buffer[32] = {};
      if (fgets(backend_buffer, sizeof(backend_buffer) - 1, backend_file)) {
        std::string file_backend(backend_buffer);
        file_backend.erase(0, file_backend.find_first_not_of(" \t\r\n"));
        file_backend.erase(file_backend.find_last_not_of(" \t\r\n") + 1);
        if (!file_backend.empty()) {
          backend_name = file_backend;
        }
      }
      fclose(backend_file);
    }
    REXLOG_INFO("[gpu] selecting backend '{}' (cvar '{}')", backend_name,
                std::string(REXCVAR_GET(gpu_backend)));
    config_.graphics = rex::system::LoadGpuPlugin(config_.gpu_plugin, backend_name.c_str());
    if (!config_.graphics) {
      REXLOG_ERROR("Failed to load GPU plugin '{}'", config_.gpu_plugin);
      return false;
    }
  }

  if (config_.graphics) {
    X_STATUS status = config_.graphics->SetupPresentation(&app_context());
    if (XFAILED(status)) {
      REXLOG_ERROR("Graphics presentation setup failed: {:08X}", status);
      return false;
    }
  }

  // Create Window for Switch
  window_ = rex::ui::Window::Create(app_context(), GetName(), 1280, 720);
  if (!window_) {
    REXLOG_ERROR("Failed to create Switch window");
    return false;
  }

  window_->AddListener(this);
  window_->AddInputListener(this, 0);
  window_->Open();

  auto* graphics_system = config_.graphics.get();
  if (graphics_system && graphics_system->presenter()) {
    auto* presenter = graphics_system->presenter();
    window_->SetPresenter(presenter);
  }

  return true;
}

bool ReXApp::ConstructRuntime(const PathConfig& paths) {
  std::error_code ec;
  std::filesystem::create_directories(paths.user_data_root, ec);
  std::filesystem::create_directories(paths.cache_root, ec);

  game_data_root_ = paths.game_data_root;
  user_data_root_ = paths.user_data_root;
  update_data_root_ = paths.update_data_root;
  cache_root_ = paths.cache_root;
  metadata_root_ = paths.metadata_root;

  runtime_ = std::make_unique<rex::Runtime>(paths.game_data_root, paths.user_data_root,
                                            paths.update_data_root, paths.cache_root, paths.metadata_root);
  runtime_->set_app_context(&app_context());
  if (window_) {
    runtime_->set_display_window(window_.get());
  }

  auto status = runtime_->Setup(ppc_info_, std::move(config_));
  if (XFAILED(status)) {
    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    return false;
  }

  if (window_ && runtime_->input_system()) {
    static_cast<rex::input::InputSystem*>(runtime_->input_system())->AttachWindow(window_.get());
  }

  if (ppc_info_.register_modules) {
    ppc_info_.register_modules(runtime_->kernel_state());
  }

  std::string xex_image = "game:\\default.xex";
  OnLoadXexImage(xex_image);

  // Mirrors the game:\ / d:\ -> game_data_root mapping in Runtime::SetupVfs.
  {
    constexpr std::string_view kGameDevice = "game:\\";
    constexpr std::string_view kDDevice = "d:\\";
    std::string_view tail = xex_image;
    if (tail.starts_with(kGameDevice)) {
      tail.remove_prefix(kGameDevice.size());
    } else if (tail.starts_with(kDDevice)) {
      tail.remove_prefix(kDDevice.size());
    }
    std::string host_tail{tail};
    std::replace(host_tail.begin(), host_tail.end(), '\\', '/');
    auto xex_host = paths.game_data_root / host_tail;
    if (!std::filesystem::is_regular_file(xex_host)) {
      auto msg = fmt::format("Entrypoint XEX not found: {}", xex_host.string());
      REXLOG_ERROR("{}", msg);
      return false;
    }
  }

  status = runtime_->LoadXexImage(xex_image);
  if (XFAILED(status)) {
    auto msg = fmt::format("Failed to load XEX ({}): {:08X}", xex_image, status);
    REXLOG_ERROR("{}", msg);
    return false;
  }

  OnPostLoadXexImage();

  if (ppc_info_.rexcrt_heap) {
    if (!rex::kernel::crt::InitHeap(REXCVAR_GET(rexcrt_heap_size_mb), runtime_->memory())) {
      REXLOG_ERROR("Failed to initialize rexcrt heap");
      return false;
    }
  }

  OnPostSetup();
  return true;
}

void ReXApp::LaunchModule() {
  app_context().CallInUIThreadDeferred([this]() {
    OnPreLaunchModule();

    auto main_thread = runtime_->PrepareModuleLaunch();
    if (!main_thread) {
      REXLOG_ERROR("Failed to launch module");
      app_context().QuitFromUIThread();
      return;
    }

    // Persistent shader and pipeline storage. The shared ReXApp path only sets
    // it up when the title has a guest XEX id, and a recompiled title has none,
    // so every run used to re-translate and re-create every pipeline (the menu
    // alone was 166 s of it). A fixed id gives the cache a file name.
    auto* graphics_system = runtime_->graphics_system();
    if (graphics_system && !runtime_->cache_root().empty()) {
      uint32_t title_id = runtime_->kernel_state()->title_id();
      if (title_id == 0) {
        title_id = 0x54534C54;  // 'TSLT'
      }
      REXLOG_INFO("Initializing shader storage for title {:08X}...", title_id);
      graphics_system->InitializeShaderStorage(runtime_->cache_root(), title_id, true);
    }

    OnPostLaunchModule(main_thread.get());
    main_thread->Resume();

    module_thread_ = std::thread([this, main_thread = std::move(main_thread)]() mutable {
      main_thread->Wait(0, 0, 0, nullptr);
      OnGuestThreadExit(main_thread.get());
      REXLOG_INFO("Guest execution complete");
      if (!shutting_down_.load(std::memory_order_acquire)) {
        app_context().CallInUIThread([this]() { app_context().QuitFromUIThread(); });
      }
    });
  });
}

std::function<void(PathConfig)> ReXApp::MakeResumeCallback() {
  return [this](PathConfig paths) {
    if (shutting_down_.load(std::memory_order_acquire))
      return;
    if (!ConstructRuntime(std::move(paths))) {
      app_context().QuitFromUIThread();
      return;
    }
    LaunchModule();
  };
}

bool ReXApp::OnInitialize() {
  if (!SetupEnvironment())
    return false;
  if (!SetupPresentation())
    return false;

  auto paths = OnFinalizePaths(resolved_defaults_, MakeResumeCallback());
  if (!paths) {
    return true;
  }

  if (!ConstructRuntime(*paths))
    return false;
  LaunchModule();
  return true;
}

void ReXApp::OnKeyDown(ui::KeyEvent& e) {
  (void)e;
}

void ReXApp::OnClosing(ui::UIEvent& e) {
  (void)e;
  shutting_down_.store(true, std::memory_order_release);
  if (runtime_ && runtime_->kernel_state()) {
    runtime_->kernel_state()->TerminateTitle();
  }
  rex::FlushLogging();
}

bool ReXApp::OnCloseRequested(ui::UIEvent& e) {
  (void)e;
  return OnWindowCloseRequested();
}

void ReXApp::OnResize(ui::UISetupEvent& e) {
  (void)e;
}

void ReXApp::OnDpiChanged(ui::UISetupEvent& e) {
  (void)e;
}

void ReXApp::OnGotFocus(ui::UISetupEvent& e) {
  (void)e;
  OnWindowFocusChanged(true);
}

void ReXApp::OnLostFocus(ui::UISetupEvent& e) {
  (void)e;
  OnWindowFocusChanged(false);
}

void ReXApp::OnMinimized(ui::UIEvent& e) {
  (void)e;
  OnWindowMinimized();
}

void ReXApp::OnRestored(ui::UIEvent& e) {
  (void)e;
  OnWindowRestored();
}

void ReXApp::OnDestroy() {
  OnShutdown();
  shutting_down_.store(true, std::memory_order_release);
  if (module_thread_.joinable()) {
    module_thread_.join();
  }
  if (window_) {
    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
  }
  window_.reset();
  runtime_.reset();
}

void ReXApp::SetGuestFrameStats(ui::DebugOverlayDialog::FrameStatsProvider provider) {
  (void)provider;
}

}  // namespace rex
