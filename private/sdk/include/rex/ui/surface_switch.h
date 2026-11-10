#pragma once
/**
 * @file        ui/surface_switch.h
 * @brief       Nintendo Switch VI surface implementation for Vulkan
 */

#include <rex/ui/surface.h>

namespace rex::ui {

class SwitchViSurface final : public Surface {
 public:
  explicit SwitchViSurface(void* window) : window_(window) {}
  ~SwitchViSurface() override = default;

  TypeIndex GetType() const override { return kTypeIndex_SwitchViWindow; }
  void* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    width_out = 1280;
    height_out = 720;
    return true;
  }

 private:
  void* window_;
};

}  // namespace rex::ui
