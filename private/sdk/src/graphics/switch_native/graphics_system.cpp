/**
 * Part 111: native Switch renderer - graphics system.
 */
#include <rex/graphics/switch_native/command_processor.h>
#include <rex/graphics/switch_native/graphics_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/vulkan/provider.h>

namespace rex::graphics::switch_native {

SwitchNativeGraphicsSystem::SwitchNativeGraphicsSystem() {}

SwitchNativeGraphicsSystem::~SwitchNativeGraphicsSystem() {}

std::string SwitchNativeGraphicsSystem::name() const {
  return "SwitchNative";
}

void SwitchNativeGraphicsSystem::CreateProvider(bool with_presentation) {
  // The presentation stack (swapchain, window, ImGui) is not Xenos specific, so
  // it is borrowed from the Vulkan backend unchanged.
  provider_ = rex::ui::vulkan::VulkanProvider::Create(true, with_presentation);
}

std::unique_ptr<CommandProcessor> SwitchNativeGraphicsSystem::CreateCommandProcessor() {
  REXLOG_INFO("[native] creating the native Switch command processor (M0 skeleton)");
  return std::make_unique<SwitchNativeCommandProcessor>(this, kernel_state_);
}

}  // namespace rex::graphics::switch_native