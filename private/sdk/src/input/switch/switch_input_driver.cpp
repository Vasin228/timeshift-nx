#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(switch_nintendo_layout, true, "Input/Switch",
                    "Use native Nintendo layout (A right=Confirm, B bottom=Cancel)");

#include "switch_input_driver.h"

#if defined(__SWITCH__)

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace rex::input::nx {

namespace {

inline int16_t ApplyDeadzone(int32_t x, int32_t y, int16_t* out_y) {
  // Radial deadzone with rescaling, so the first millimetre of travel is not lost
  // and the character does not drift while the stick is untouched.
  const int32_t len_sq = x * x + y * y;
  if (len_sq <= SwitchInputDriver::kStickDeadzone * SwitchInputDriver::kStickDeadzone) {
    *out_y = 0;
    return 0;
  }
  const float len = std::sqrt(static_cast<float>(len_sq));
  const float scale = (len - SwitchInputDriver::kStickDeadzone) /
                      (32767.0f - SwitchInputDriver::kStickDeadzone);
  const float nx = (static_cast<float>(x) / len) * scale * 32767.0f;
  const float ny = (static_cast<float>(y) / len) * scale * 32767.0f;
  *out_y = static_cast<int16_t>(ny);
  return static_cast<int16_t>(nx);
}

}  // namespace

X_STATUS SwitchInputDriver::Setup() {
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad_);
  padUpdate(&pad_);
  std::memset(&last_gamepad_, 0, sizeof(last_gamepad_));
  packet_number_ = 1;
  ready_ = true;
  return X_STATUS_SUCCESS;
}

void SwitchInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  padUpdate(&pad_);
  DeviceInfo info;
  info.id = DeviceId{kDeviceId};
  info.name = "Nintendo Switch controller";
  info.guid = "nx-hid";
  info.synthetic = false;
  out.push_back(info);
}

X_RESULT SwitchInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!out_state || id != DeviceId{kDeviceId}) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  padUpdate(&pad_);
  const u64 held = padGetButtons(&pad_);

  uint16_t buttons = 0;
  // Default to native Nintendo Switch layout:
  // A (Right) = Action/Confirm (Xbox A)
  // B (Bottom) = Cancel/Back (Xbox B)
  // X (Top) = Xbox X
  // Y (Left) = Xbox Y
  const bool nintendo_layout = REXCVAR_GET(switch_nintendo_layout);
  if (nintendo_layout) {
    if (held & HidNpadButton_A) buttons |= X_INPUT_GAMEPAD_A;
    if (held & HidNpadButton_B) buttons |= X_INPUT_GAMEPAD_B;
    if (held & HidNpadButton_X) buttons |= X_INPUT_GAMEPAD_X;
    if (held & HidNpadButton_Y) buttons |= X_INPUT_GAMEPAD_Y;
  } else {
    // Physical Xbox button positions: bottom button is A
    if (held & HidNpadButton_B) buttons |= X_INPUT_GAMEPAD_A;
    if (held & HidNpadButton_A) buttons |= X_INPUT_GAMEPAD_B;
    if (held & HidNpadButton_Y) buttons |= X_INPUT_GAMEPAD_X;
    if (held & HidNpadButton_X) buttons |= X_INPUT_GAMEPAD_Y;
  }
  if (held & HidNpadButton_L) buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
  if (held & HidNpadButton_R) buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  if (held & HidNpadButton_Plus) buttons |= X_INPUT_GAMEPAD_START;
  if (held & HidNpadButton_Minus) buttons |= X_INPUT_GAMEPAD_BACK;
  if (held & HidNpadButton_StickL) buttons |= X_INPUT_GAMEPAD_LEFT_THUMB;
  if (held & HidNpadButton_StickR) buttons |= X_INPUT_GAMEPAD_RIGHT_THUMB;
  if (held & HidNpadButton_Up) buttons |= X_INPUT_GAMEPAD_DPAD_UP;
  if (held & HidNpadButton_Down) buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
  if (held & HidNpadButton_Left) buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
  if (held & HidNpadButton_Right) buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;

  const HidAnalogStickState left = padGetStickPos(&pad_, 0);
  const HidAnalogStickState right = padGetStickPos(&pad_, 1);

  int16_t ly = 0, ry = 0;
  const int16_t lx = ApplyDeadzone(left.x, left.y, &ly);
  const int16_t rx = ApplyDeadzone(right.x, right.y, &ry);

  const uint8_t lt = (held & HidNpadButton_ZL) ? 0xFF : 0x00;
  const uint8_t rt = (held & HidNpadButton_ZR) ? 0xFF : 0x00;

  bool state_changed = false;
  if (uint16_t(last_gamepad_.buttons) != buttons ||
      last_gamepad_.left_trigger != lt ||
      last_gamepad_.right_trigger != rt ||
      std::abs(int32_t(int16_t(last_gamepad_.thumb_lx)) - lx) > 1200 ||
      std::abs(int32_t(int16_t(last_gamepad_.thumb_ly)) - ly) > 1200 ||
      std::abs(int32_t(int16_t(last_gamepad_.thumb_rx)) - rx) > 1200 ||
      std::abs(int32_t(int16_t(last_gamepad_.thumb_ry)) - ry) > 1200) {
    state_changed = true;
  }

  if (state_changed) {
    ++packet_number_;
    last_gamepad_.buttons = buttons;
    last_gamepad_.left_trigger = lt;
    last_gamepad_.right_trigger = rt;
    last_gamepad_.thumb_lx = lx;
    last_gamepad_.thumb_ly = ly;
    last_gamepad_.thumb_rx = rx;
    last_gamepad_.thumb_ry = ry;
  }

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = lt;
  out_state->gamepad.right_trigger = rt;
  out_state->gamepad.thumb_lx = lx;
  out_state->gamepad.thumb_ly = ly;
  out_state->gamepad.thumb_rx = rx;
  out_state->gamepad.thumb_ry = ry;
  return X_ERROR_SUCCESS;
}

X_RESULT SwitchInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                                  X_INPUT_CAPABILITIES* out_caps) {
  if (!out_caps || id != DeviceId{kDeviceId}) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  out_caps->type = 1;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
  out_caps->flags = 4;     // wireless
  out_caps->gamepad.buttons = 0xF3FF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = static_cast<int16_t>(-64);
  out_caps->gamepad.thumb_ly = static_cast<int16_t>(-64);
  out_caps->gamepad.thumb_rx = static_cast<int16_t>(-64);
  out_caps->gamepad.thumb_ry = static_cast<int16_t>(-64);
  out_caps->vibration.left_motor_speed = 0xFF;
  out_caps->vibration.right_motor_speed = 0xFF;
  return X_ERROR_SUCCESS;
}

X_RESULT SwitchInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  if (id != DeviceId{kDeviceId}) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  (void)vibration;
  return X_ERROR_SUCCESS;
}

X_RESULT SwitchInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                               X_INPUT_KEYSTROKE* out_keystroke) {
  (void)id;
  (void)flags;
  (void)out_keystroke;
  return X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace rex::input::nx

#endif  // __SWITCH__
