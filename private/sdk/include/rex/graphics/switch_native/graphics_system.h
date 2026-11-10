/**
 * Part 111: native Switch renderer - graphics system entry point.
 *
 * This is the third backend of the rexgpu-xenos plugin (the other two are
 * vulkan/ and d3d12/). It keeps the SDK's packet decoding, shader translation
 * and primitive assembly, and replaces the Xenos GPU emulation itself: no
 * EDRAM buffer, no per-draw render passes, no resolve copies.
 */
#pragma once

#include <memory>
#include <string>

#include <rex/graphics/graphics_system.h>

namespace rex::graphics::switch_native {

class SwitchNativeGraphicsSystem final : public GraphicsSystem {
 public:
  SwitchNativeGraphicsSystem();
  ~SwitchNativeGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::string name() const override;

 protected:
  void CreateProvider(bool with_presentation) override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace rex::graphics::switch_native