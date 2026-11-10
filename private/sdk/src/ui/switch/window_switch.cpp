#include <rex/ui/window.h>
#include <rex/ui/surface.h>
#include <rex/ui/surface_switch.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/logging.h>
#include <switch.h>
#include <atomic>

namespace rex::ui {

class WindowSwitch : public Window {
 public:
  WindowSwitch(WindowedAppContext& app_context, std::string_view title,
               uint32_t desired_logical_width, uint32_t desired_logical_height)
      : Window(app_context, title, desired_logical_width, desired_logical_height) {}

  ~WindowSwitch() override {
    EnterDestructor();
  }

 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver destruction_receiver(this);
    OnActualSizeUpdate(1280, 720, destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return true;
    }
    OnFocusUpdate(true, destruction_receiver);
    return true;
  }

  void RequestCloseImpl() override {
    WindowDestructionReceiver destruction_receiver(this);
    OnBeforeClose(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return;
    }
    OnAfterClose();
  }

  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override {
    if (allowed_types & Surface::kTypeFlag_SwitchViWindow) {
      return std::make_unique<SwitchViSurface>(static_cast<void*>(nwindowGetDefault()));
    }
    return nullptr;
  }

  void RequestPaintImpl() override {
    static uint64_t req_count = 0;
    if (++req_count <= 5 || req_count % 300 == 0) {
      REXLOG_INFO("[window] RequestPaint called (total={})", req_count);
    }
    if (paint_pending_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    app_context().CallInUIThreadDeferred([this]() {
      paint_pending_.store(false, std::memory_order_release);
      static uint64_t paint_count = 0;
      if (++paint_count <= 5 || paint_count % 300 == 0) {
        REXLOG_INFO("[window] OnPaint executed on UI thread (total={})", paint_count);
      }
      OnPaint();
    });
  }

 private:
  std::atomic<bool> paint_pending_{false};
};

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<WindowSwitch>(app_context, title, desired_logical_width,
                                        desired_logical_height);
}

}  // namespace rex::ui
