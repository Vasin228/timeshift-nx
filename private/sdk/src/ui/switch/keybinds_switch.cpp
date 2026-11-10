#include <rex/ui/keybinds.h>

namespace rex::ui {

VirtualKey ParseVirtualKey(std::string_view name) {
  (void)name;
  return VirtualKey::kNone;
}

std::string VirtualKeyToString(VirtualKey vk) {
  (void)vk;
  return "";
}

void RegisterBind(std::string_view name, std::string_view default_key, std::string_view description,
                  std::function<void()> callback) {
  (void)name;
  (void)default_key;
  (void)description;
  (void)callback;
}

void UnregisterBind(std::string_view name) {
  (void)name;
}

bool ProcessKeyEvent(KeyEvent& e) {
  (void)e;
  return false;
}

}  // namespace rex::ui
