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
  padConfigureInput(4, HidNpadStyleSet_NpadStandard);
  padInitialize(&pad_handheld_, HidNpadIdType_Handheld);
  padInitialize(&pad_pro_, HidNpadIdType_No1, HidNpadIdType_No2);
  padUpdate(&pad_handheld_);
  padUpdate(&pad_pro_);
  std::memset(&last_gamepad_, 0, sizeof(last_gamepad_));
  packet_number_ = 1;
  ready_ = true;
  return X_STATUS_SUCCESS;
}

void SwitchInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  padUpdate(&pad_handheld_);
  padUpdate(&pad_pro_);
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
  padUpdate(&pad_handheld_);
  padUpdate(&pad_pro_);
  const u64 held = padGetButtons(&pad_handheld_) | padGetButtons(&pad_pro_);

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

  const HidAnalogStickState left_h = padGetStickPos(&pad_handheld_, 0);
  const HidAnalogStickState right_h = padGetStickPos(&pad_handheld_, 1);
  const HidAnalogStickState left_p = padGetStickPos(&pad_pro_, 0);
  const HidAnalogStickState right_p = padGetStickPos(&pad_pro_, 1);

  int16_t ly_h = 0, ry_h = 0;
  const int16_t lx_h = ApplyDeadzone(left_h.x, left_h.y, &ly_h);
  const int16_t rx_h = ApplyDeadzone(right_h.x, right_h.y, &ry_h);

  int16_t ly_p = 0, ry_p = 0;
  const int16_t lx_p = ApplyDeadzone(left_p.x, left_p.y, &ly_p);
  const int16_t rx_p = ApplyDeadzone(right_p.x, right_p.y, &ry_p);

  const int32_t mag_l_h = int32_t(lx_h) * lx_h + int32_t(ly_h) * ly_h;
  const int32_t mag_l_p = int32_t(lx_p) * lx_p + int32_t(ly_p) * ly_p;
  const int16_t lx = (mag_l_h >= mag_l_p) ? lx_h : lx_p;
  const int16_t ly = (mag_l_h >= mag_l_p) ? ly_h : ly_p;

  const int32_t mag_r_h = int32_t(rx_h) * rx_h + int32_t(ry_h) * ry_h;
  const int32_t mag_r_p = int32_t(rx_p) * rx_p + int32_t(ry_p) * ry_p;
  const int16_t rx = (mag_r_h >= mag_r_p) ? rx_h : rx_p;
  const int16_t ry = (mag_r_h >= mag_r_p) ? ry_h : ry_p;

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
