// Nintendo Switch input driver.
//
// The guest (Xbox 360 title) only ever sees a plain Xbox 360 pad: 16 button bits,
// two 8-bit triggers and four int16 axes. Everything Switch-specific lives here.

#pragma once

#include <rex/input/input_driver.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace rex::input::nx {

class SwitchInputDriver final : public InputDriver {
 public:
  explicit SwitchInputDriver(rex::ui::Window* window, size_t window_z_order)
      : InputDriver(window, window_z_order) {}

  // Public: the deadzone helper in the .cpp is a free function.
  static constexpr uint64_t kDeviceId = 0x4E58000000000001ull;
  static constexpr int32_t kStickDeadzone = 5000;  // out of 32767, applied radially

  X_STATUS Setup() override;
  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
#if defined(__SWITCH__)
  PadState pad_handheld_{};
  PadState pad_pro_{};
#endif
  X_INPUT_GAMEPAD last_gamepad_{};
  uint32_t packet_number_ = 0;
  bool ready_ = false;
};

}  // namespace rex::input::nx
