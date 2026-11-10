/**
 * Part 111/112: native Switch renderer - command processor.
 *
 * M0: backend exists, counts swaps.
 * M1: on every swap, clear the presenter's guest output image to a colour that
 *     changes once per second and present it, proving the presentation path is
 *     ours and is not the bottleneck.
 */
#include <rex/graphics/switch_native/command_processor.h>
#include "native_renderer.h"

#include <rex/graphics/switch_native/self_test_shaders.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include <rex/cvar.h>
#include <rex/graphics/perf_counters.h>
#include <rex/memory/utils.h>
#include <rex/math.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/graphics/vulkan/shader.h>
#include <rex/hash.h>
#include <rex/string.h>
#include <rex/graphics/switch_native/graphics_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/presenter.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>

namespace rex::graphics::switch_native {

// S8 diagnostic: keep the guest vertex shader, replace the guest pixel shader
// with a flat colour. Solid magenta on screen proves the vertex path (fetch
// constants, shared memory window, NDC transform, translated VS) produces
// geometry, so whatever is missing lives on the pixel shader side.

namespace {

// Runtime diagnostics driven by sdmc:/switch/timeshift/debug.txt, for example:
//   flat=1     draw guest geometry with the flat magenta pixel shader
//   marker=1   replace the guest vertex shader with the viewport marker triangle
//   probe=1    colour probe pixel shader
//   shot=7     take one screenshot right now (any new value triggers one)
// The file is re-read about once a second, so modes can be switched while the
// title is running - no rebuild, no relaunch.
struct RuntimeToggles {
  bool flat_pixel_shader = false;
  bool marker_vertex_shader = false;
  bool probe_pixel_shader = false;
  int shot_request = -1;
  int last_shot_request = -1;
  bool shot_once = false;
  // S20: replay only a slice of the queued draws, so one run can show what each
  // group of draws paints. -1 = no limit. Driven from debug.txt while the title
  // runs (a file, not a cvar, because cvars are registered too late to be read
  // from timeshift.toml).
  int draw_skip = 0;
  int draw_limit = -1;
  bool sweep = false;
  int sweep_step = 100;
  // Seconds between sweep steps. The default 3 s spent all twenty screenshots
  // inside the first minute, which is where the boot logos live, so the timing
  // has to be controllable without a rebuild.
  double sweep_seconds = 0.0;
  // A/B diagnostics: the pipeline state includes these, so flipping them builds
  // separate pipelines and the picture can be compared with and without the
  // feature inside one run.
  bool blend_enabled = true;
  bool depth_enabled = true;
  // 0 = off, 1 = alternate blending per screenshot, 2 = alternate the guest
  // window scissor per screenshot.
  int ab_mode = 0;
  bool scissor_enabled = true;
  // Perf bisection variants, driven by ab=5 which cycles them every ten frames:
  // clamp every draw to three vertices (vertex work removed) and replace every
  // sampled image with a 1x1 dummy (texture work removed).
  bool clamp_vertices = false;
  bool no_textures = false;
  // Replaces every sampler with nearest/no-mip variants: separates texture
  // filtering and level-of-detail cost from raw texture bandwidth.
  bool nearest_samplers = false;
  // Keeps bilinear filtering but pins the level of detail to zero, so the cost
  // of mip selection can be told apart from the cost of filtering.
  bool lod_zero = false;
  int list_request = -1;
  int last_list_request = -1;
  bool list_once = false;
  // S21: autonomous benchmark loop. `exit=N` writes perf.txt and terminates the
  // title N seconds after the first swap, so a run never has to be closed by a
  // physical HOME press. `perf=N` rewrites perf.txt every N seconds.
  int exit_after_seconds = 0;
  int perf_every_seconds = 5;
  uint64_t last_poll_frame = ~uint64_t(0);
};

RuntimeToggles g_toggles;

void RefreshRuntimeToggles(uint64_t frame) {
  if (g_toggles.last_poll_frame != ~uint64_t(0) &&
      frame - g_toggles.last_poll_frame < 30) {
    return;
  }
  g_toggles.last_poll_frame = frame;
  std::FILE* file = std::fopen("sdmc:/switch/timeshift/debug.txt", "r");
  if (!file) {
    return;
  }
  char line[160];
  while (std::fgets(line, sizeof(line), file)) {
    char* equals = std::strchr(line, '=');
    if (!equals) {
      continue;
    }
    *equals = '\0';
    const int value = std::atoi(equals + 1);
    if (!std::strncmp(line, "flat", 4)) {
      g_toggles.flat_pixel_shader = value != 0;
    } else if (!std::strncmp(line, "marker", 6)) {
      g_toggles.marker_vertex_shader = value != 0;
    } else if (!std::strncmp(line, "probe", 5)) {
      g_toggles.probe_pixel_shader = value != 0;
    } else if (!std::strncmp(line, "skip", 4)) {
      g_toggles.draw_skip = value > 0 ? value : 0;
    } else if (!std::strncmp(line, "limit", 5)) {
      g_toggles.draw_limit = value;
    } else if (!std::strncmp(line, "sweep", 5)) {
      g_toggles.sweep = value != 0;
    } else if (!std::strncmp(line, "step", 4)) {
      g_toggles.sweep_step = value > 0 ? value : 100;
    } else if (!std::strncmp(line, "blend", 5)) {
      g_toggles.blend_enabled = value != 0;
    } else if (!std::strncmp(line, "depth", 5)) {
      g_toggles.depth_enabled = value != 0;
    } else if (!std::strncmp(line, "ab", 2)) {
      g_toggles.ab_mode = value;
    } else if (!std::strncmp(line, "tstep", 5)) {
      const double seconds = std::atof(equals + 1);
      g_toggles.sweep_seconds = seconds > 0.0 ? seconds : 0.0;
    } else if (!std::strncmp(line, "list", 4)) {
      g_toggles.list_request = value;
      if (value != g_toggles.last_list_request) {
        g_toggles.last_list_request = value;
        g_toggles.list_once = true;
      }
    } else if (!std::strncmp(line, "shot", 4)) {
      g_toggles.shot_request = value;
      if (value != g_toggles.last_shot_request) {
        g_toggles.last_shot_request = value;
        g_toggles.shot_once = true;
      }
    } else if (!std::strncmp(line, "exit", 4)) {
      g_toggles.exit_after_seconds = value > 0 ? value : 0;
    } else if (!std::strncmp(line, "perf", 4)) {
      g_toggles.perf_every_seconds = value > 0 ? value : 0;
    }
  }
  std::fclose(file);
}

}  // namespace

REXCVAR_DEFINE_BOOL(native_draw_marker, false, "Graphics",
                    "Draw the red diagnostic marker quads over the guest frame");
REXCVAR_DEFINE_BOOL(native_flat_ps, false, "Graphics",
                    "Draw guest geometry with a flat colour pixel shader (diagnostic)");
// S14 bisect: keep every piece of the guest pipeline but replace its vertex
// shader with the viewport-filling marker triangle. A magenta screen then means
// "the pipeline, descriptors, viewport and pixel path are fine, only the
// translated vertex shader produces no visible position".
REXCVAR_DEFINE_BOOL(native_marker_vs, false, "Graphics",
                    "Draw guest geometry with a viewport-filling vertex shader (diagnostic)");
// S15: full-screen colour probe. Together with native_marker_vs it answers "did
// the per-draw constants, fetch descriptions and vertex data reach the GPU"
// from a screenshot, with no readback and no log.
REXCVAR_DEFINE_BOOL(native_probe_ps, false, "Graphics",
                    "Report the guest draw data as a colour (diagnostic)");

namespace {

// Some slowly rotating colours, one per second, so the screen is obviously
// alive and not a frozen frame.
constexpr VkClearColorValue kCycleColors[] = {
    {{1.0f, 0.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f, 1.0f}},
    {{1.0f, 1.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f, 1.0f, 1.0f}},
};

}  // namespace

SwitchNativeCommandProcessor::SwitchNativeCommandProcessor(GraphicsSystem* graphics_system,
                                                          system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

SwitchNativeCommandProcessor::~SwitchNativeCommandProcessor() {
  ShutdownContext();
}

bool SwitchNativeCommandProcessor::SetupContext() {
  auto* provider = static_cast<ui::vulkan::VulkanProvider*>(graphics_system_->provider());
  if (!provider || !provider->vulkan_device()) {
    REXLOG_ERROR("[native] no Vulkan device");
    return false;
  }
  device_ = provider->vulkan_device();
  native_renderer_ = std::make_unique<thps::native::Renderer>();
  if (!native_renderer_->Initialize(provider, memory_, register_file_)) return false;
  native_renderer_->InitializeDriverCache("sdmc:/switch/timeshift/cache", 0x565507DA);
  // Native owns its stream, descriptor sets and pipelines. Creating the old
  // replay backend as well allocated an unused guest-memory window and bound
  // its incomplete constants buffer (invalid descriptors on desktop Vulkan).
  LogDeviceLimits();
  last_report_ns_ = rex::graphics::perf::NowNs();
  REXLOG_INFO("[native] N163 context ready (legacy replay resources disabled)");
  return true;
}

void SwitchNativeCommandProcessor::ShutdownContext() {
  if (!device_) {
    return;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  if (descriptor_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    descriptor_pool_ = VK_NULL_HANDLE;
    shared_memory_set_ = VK_NULL_HANDLE;
    constants_set_ = VK_NULL_HANDLE;
  }
  if (constants_placeholder_buffer_ != VK_NULL_HANDLE) {
    dfn.vkDestroyBuffer(device, constants_placeholder_buffer_, nullptr);
    constants_placeholder_buffer_ = VK_NULL_HANDLE;
    if (constants_placeholder_memory_ != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, constants_placeholder_memory_, nullptr);
      constants_placeholder_memory_ = VK_NULL_HANDLE;
    }
  }
  if (shared_memory_buffer_ != VK_NULL_HANDLE) {
    if (shared_memory_mapped_) {
      dfn.vkUnmapMemory(device, shared_memory_memory_);
      shared_memory_mapped_ = nullptr;
    }
    dfn.vkDestroyBuffer(device, shared_memory_buffer_, nullptr);
    shared_memory_buffer_ = VK_NULL_HANDLE;
    if (shared_memory_memory_ != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, shared_memory_memory_, nullptr);
      shared_memory_memory_ = VK_NULL_HANDLE;
    }
    shared_memory_ready_ = false;
  }
  if (pipeline_cache_ != VK_NULL_HANDLE) {
    dfn.vkDestroyPipelineCache(device, pipeline_cache_, nullptr);
    pipeline_cache_ = VK_NULL_HANDLE;
  }
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    dfn.vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
  if (descriptor_set_layout_shared_memory_ != VK_NULL_HANDLE) {
    dfn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_shared_memory_, nullptr);
    descriptor_set_layout_shared_memory_ = VK_NULL_HANDLE;
  }
  if (descriptor_set_layout_constants_ != VK_NULL_HANDLE) {
    dfn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_constants_, nullptr);
    descriptor_set_layout_constants_ = VK_NULL_HANDLE;
  }
  if (descriptor_set_layout_empty_ != VK_NULL_HANDLE) {
    dfn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_empty_, nullptr);
    descriptor_set_layout_empty_ = VK_NULL_HANDLE;
  }
  if (clear_fence_ != VK_NULL_HANDLE) {
    dfn.vkDestroyFence(device, clear_fence_, nullptr);
    clear_fence_ = VK_NULL_HANDLE;
  }
  if (command_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyCommandPool(device, command_pool_, nullptr);
    command_pool_ = VK_NULL_HANDLE;
    command_buffer_ = VK_NULL_HANDLE;
  }
  if (native_renderer_) {
    native_renderer_->Shutdown();
    native_renderer_.reset();
  }
  REXLOG_INFO("[native] ShutdownContext after {} swaps", swaps_);
  device_ = nullptr;
}


void SwitchNativeCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  uint32_t old_value = index < RegisterFile::kRegisterCount ? register_file_->values[index] : 0;
  CommandProcessor::WriteRegister(index, value);
  if (native_renderer_) {
    native_renderer_->RegisterWritten(index, old_value, value);
  }
}

Shader* SwitchNativeCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                                 uint32_t guest_address,
                                                 const uint32_t* host_address,
                                                 uint32_t dword_count) {
  if (!device_ || !host_address || !dword_count) {
    return nullptr;
  }
  (void)guest_address;
  if (native_renderer_ && host_address && dword_count) {
    native_renderer_->LoadShader(shader_type, host_address, dword_count, guest_address);
  }
  const uint64_t ucode_hash = XXH3_64bits(host_address, dword_count * sizeof(uint32_t));
  auto existing = shaders_.find(ucode_hash);
  if (existing != shaders_.end()) {
    return existing->second;
  }
  // The shared Vulkan shader already wraps the Xenos ucode -> SPIR-V translator,
  // so the native backend reuses it instead of owning a second translator.
  auto* shader = new vulkan::VulkanShader(device_, shader_type, ucode_hash, host_address,
                                          dword_count);
  shaders_.emplace(ucode_hash, shader);
  if ((shaders_.size() % 32) == 1) {
    REXLOG_INFO("[native] shaders loaded so far: {} (latest {:016X}, {} dwords)", shaders_.size(),
                ucode_hash, dword_count);
  }
  return shader;
}

bool SwitchNativeCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                                             IndexBufferInfo* index_buffer_info,
                                             bool major_mode_explicit) {
  // M2 draws geometry here. Until then the draw is accepted and dropped: the
  // base command processor logs an error for every draw a backend rejects, and
  // that logging alone (tens of thousands of lines written to the SD card per
  // second) starved the guest down to ~3.5 swaps/s in the M1 run.
  (void)major_mode_explicit;
  std::lock_guard<std::mutex> state_lock(state_mutex_);
  draws_ += 1;
  last_draw_vertex_count_ = index_count;
  if (index_count > max_draw_vertices_) {
    max_draw_vertices_ = index_count;
  }
  if (index_count >= 100) {
    ++big_draws_;
  } else if (index_count < 4) {
    ++tiny_draws_;
  }
  if (!native_renderer_) TrackPipelineState(prim_type);
  const reg::RB_MODECONTROL mode_ctrl = register_file_->Get<reg::RB_MODECONTROL>();
  if (native_renderer_) {
    if (mode_ctrl.edram_mode == xenos::EdramMode::kCopy) {
      native_renderer_->Resolve();
    } else {
      native_renderer_->Draw(nullptr, 0);
    }
    return true;
  }

  // EDRAM mode and colour surface of this draw, counted for the frame report.
  {
    const RegisterFile& regs = *register_file_;
    const reg::RB_MODECONTROL mode_control = regs.Get<reg::RB_MODECONTROL>();
    switch (mode_control.edram_mode) {
      case xenos::EdramMode::kColorDepth:
        ++frame_edram_color_;
        break;
      case xenos::EdramMode::kCopy:
        ++frame_edram_copy_;
        break;
      case xenos::EdramMode::kDepthOnly:
        ++frame_edram_depth_;
        break;
      default:
        ++frame_edram_other_;
        break;
    }
    const reg::RB_DEPTHCONTROL depth_control = regs.Get<reg::RB_DEPTHCONTROL>();
    if (depth_control.z_enable) {
      ++frame_depth_test_;
      if (depth_control.z_write_enable) {
        ++frame_depth_write_;
      }
      if (depth_control.zfunc == xenos::CompareFunction::kLess ||
          depth_control.zfunc == xenos::CompareFunction::kLessEqual) {
        ++frame_depth_less_;
      }
    }
    const reg::RB_SURFACE_INFO surface_info = regs.Get<reg::RB_SURFACE_INFO>();
    const reg::RB_COLOR_INFO color_info = regs.Get<reg::RB_COLOR_INFO>();
    const uint32_t surface = (uint32_t(color_info.color_base) << 16) |
                             (uint32_t(color_info.color_base_bit_11) << 15) |
                             (uint32_t(surface_info.surface_pitch) << 4) |
                             uint32_t(color_info.color_format);
    bool known = false;
    for (uint32_t i = 0; i < frame_surface_count_; ++i) {
      if (frame_surface_[i] == surface) {
        known = true;
        break;
      }
    }
    if (!known && frame_surface_count_ < 8) {
      frame_surface_[frame_surface_count_++] = surface;
    }
  }

  // S4: queue the draw. Pipelines are created lazily here so a state that never
  // reaches the screen costs nothing.
  FrameDraw draw;
  if (index_buffer_info && index_buffer_info->count) {
    draw.indexed = true;
    draw.index_count = index_buffer_info->count;
    draw.index_base_bytes = index_buffer_info->guest_base;
    draw.index_type = index_buffer_info->format == xenos::IndexFormat::kInt32
                          ? VK_INDEX_TYPE_UINT32
                          : VK_INDEX_TYPE_UINT16;
  } else {
    draw.index_count = index_count;
  }
  // S20 listing: the primitive and the state hash make the draw order readable
  // without any GPU readback.
  draw.prim_type = uint32_t(prim_type);
  draw.state_key = PipelineStateKeyHash()(last_state_key_);
  const PipelineEntry* pipeline_entry = GetOrCreatePipeline(last_state_key_);
  if (!pipeline_entry || pipeline_entry->pipeline == VK_NULL_HANDLE) {
    ++pipeline_failures_;
    return true;
  }
  const bool use_flat_pixel_shader =
      REXCVAR_QUERY(bool, native_flat_ps) || g_toggles.flat_pixel_shader;
  draw.pipeline = use_flat_pixel_shader && pipeline_entry->pipeline_flat != VK_NULL_HANDLE
                      ? pipeline_entry->pipeline_flat
                      : pipeline_entry->pipeline;
  draw.layout = pipeline_entry->layout;
  draw.textures_layout_vertex = pipeline_entry->textures_layout_vertex;
  draw.textures_layout_pixel = pipeline_entry->textures_layout_pixel;
  // Constants, textures and the viewport are captured now, while the register
  // file still belongs to this draw: the frame is replayed later, at swap time.
  PrepareDraw(draw, index_count);
  if (frame_draws_.size() < 200000) {
    frame_draws_.push_back(draw);
  }
  return true;
  indices_ += index_count;

  // S1: prove the shared translator can handle this title's ucode, before any
  // pipeline object exists. Nothing is drawn yet.
  if (translator_) {
    static string::StringBuffer s_disasm_buffer;
    const RegisterFile& regs = *register_file_;
    Shader* shaders_to_translate[2] = {active_vertex_shader(), active_pixel_shader()};
    for (int i = 0; i < 2; ++i) {
      Shader* shader = shaders_to_translate[i];
      if (!shader) {
        continue;
      }
      if (!shader->is_ucode_analyzed()) {
        shader->AnalyzeUcode(s_disasm_buffer);
      }
      const uint64_t default_modification =
          i == 0
              ? translator_->GetDefaultVertexShaderModification(
                    shader->GetDynamicAddressableRegisterCount(
                        regs.Get<reg::SQ_PROGRAM_CNTL>().vs_num_reg),
                    Shader::HostVertexShaderType::kVertex)
              : translator_->GetDefaultPixelShaderModification(
                    shader->GetDynamicAddressableRegisterCount(
                        regs.Get<reg::SQ_PROGRAM_CNTL>().ps_num_reg));
      SpirvShaderTranslator::Modification modification(default_modification);
      if (i == 0) {
        modification.vertex.interpolator_mask = 0xFFFFu;
        modification.vertex.tessellation_mode = 0;
      } else {
        modification.pixel.interpolator_mask = 0xFFFFu;
        modification.pixel.param_gen_enable = 0;
      }
      bool is_new_translation = false;
      auto* translation = static_cast<vulkan::VulkanShader::VulkanTranslation*>(
          shader->GetOrCreateTranslation(modification.value, &is_new_translation));
      if (!translation || !is_new_translation) {
        if (!translation) {
          ++failed_shaders_;
          REXLOG_ERROR("[native] shader {:016X} has no translation object",
                       static_cast<vulkan::VulkanShader*>(shader)->ucode_data_hash());
        }
        continue;
      }
      if (!translator_->TranslateAnalyzedShader(*translation)) {
        ++failed_shaders_;
        REXLOG_ERROR("[native] translation FAILED for shader {:016X} ({})",
                     static_cast<vulkan::VulkanShader*>(shader)->ucode_data_hash(),
                     i == 0 ? "vertex" : "pixel");
        continue;
      }
      if (translation->GetOrCreateShaderModule() == VK_NULL_HANDLE) {
        ++failed_shaders_;
        REXLOG_ERROR("[native] shader module creation failed for {:016X}",
                     static_cast<vulkan::VulkanShader*>(shader)->ucode_data_hash());
        continue;
      }
      ++translated_shaders_;
      if ((translated_shaders_ % 32) == 1) {
        REXLOG_INFO("[native] translated {} shaders, {} failed (latest {:016X})", translated_shaders_,
                    failed_shaders_, static_cast<vulkan::VulkanShader*>(shader)->ucode_data_hash());
      }
    }
  }
  return true;
}

bool SwitchNativeCommandProcessor::IssueCopy() {
  // Nothing to copy: this backend never emulates EDRAM.
  return false;
}

bool SwitchNativeCommandProcessor::ClearGuestOutput(VkImage image, bool image_ever_written) {
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  const VkDevice device = device_->device();

  if (fence_submitted_) {
    if (dfn.vkWaitForFences(device, 1, &clear_fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
      REXLOG_ERROR("[native] waiting for the previous clear failed");
      return false;
    }
    fence_submitted_ = false;
  }
  dfn.vkResetFences(device, 1, &clear_fence_);


  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (dfn.vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
    return false;
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  if (image_ever_written) {
    barrier.srcAccessMask = ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dfn.vkCmdPipelineBarrier(command_buffer_,
                             ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
  } else {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
  }

  // The M1 colour cycling proved the presentation path; now a frame without
  // guest draws must look like a real blank frame, not like a test pattern.
  const VkClearColorValue color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkImageSubresourceRange range{};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = 1;
  range.baseArrayLayer = 0;
  range.layerCount = 1;
  dfn.vkCmdClearColorImage(command_buffer_, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                           &range);

  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
  dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask, 0, 0, nullptr,
                           0, nullptr, 1, &barrier);

  if (dfn.vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
    return false;
  }

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer_;
  const ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
      device_->AcquireQueue(device_->queue_family_graphics_compute(), 0);
  if (dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, clear_fence_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] clear submit failed");
    return false;
  }
  fence_submitted_ = true;
  return true;
}

void SwitchNativeCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                             uint32_t frontbuffer_height) {
  RefreshRuntimeToggles(swaps_);
  // S21: autonomous benchmark loop - no physical HOME press needed.
  {
    static uint64_t s21_start_ns = 0;
    static uint64_t s21_perf_ns = 0;
    const uint64_t s21_now = rex::graphics::perf::NowNs();
    if (!s21_start_ns) {
      s21_start_ns = s21_now;
      s21_perf_ns = s21_now;
    }
    const uint64_t s21_uptime_ns = s21_now - s21_start_ns;
    const auto s21_write = [this, s21_uptime_ns]() {
      std::FILE* file = std::fopen("sdmc:/switch/timeshift/perf.txt", "w");
      if (!file) {
        return;
      }
      std::fprintf(file,
                   "uptime_s=%.1f swaps=%llu draws=%llu draws_per_swap=%.1f textures=%zu\n",
                   double(s21_uptime_ns) / 1e9, (unsigned long long)swaps_,
                   (unsigned long long)draws_, swaps_ ? double(draws_) / double(swaps_) : 0.0,
                   textures_.size());
      std::fclose(file);
    };
    if (g_toggles.perf_every_seconds > 0 &&
        s21_now - s21_perf_ns >= uint64_t(g_toggles.perf_every_seconds) * 1000000000ull) {
      s21_perf_ns = s21_now;
      s21_write();
    }
    if (g_toggles.exit_after_seconds > 0 &&
        s21_uptime_ns >= uint64_t(g_toggles.exit_after_seconds) * 1000000000ull) {
      s21_write();
      REXLOG_INFO("[native] S21: exit={} s reached, terminating the title",
                  g_toggles.exit_after_seconds);
      std::fflush(stdout);
      std::exit(0);
    }
  }
  ++swaps_;
  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter || !device_ || frontbuffer_width == 0 || frontbuffer_height == 0) {
    return;
  }
  if (native_renderer_) {
    native_renderer_->SetGammaRamp256(
        reinterpret_cast<const uint32_t*>(gamma_ramp_256_entry_table()));
    native_renderer_->Swap(presenter, frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  } else {
    presenter->RefreshGuestOutput(
        frontbuffer_width, frontbuffer_height, frontbuffer_width, frontbuffer_height,
        [this, frontbuffer_width, frontbuffer_height](
            ui::Presenter::GuestOutputRefreshContext& context) -> bool {
          auto& vk_context =
              static_cast<ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext&>(context);
          if (!frame_draws_.empty()) {
            return RenderFrame(vk_context.image(), vk_context.image_view(),
                               vk_context.image_ever_written_previously(), frontbuffer_width,
                               frontbuffer_height);
          }
          return ClearGuestOutput(vk_context.image(), vk_context.image_ever_written_previously());
        });
  }

  const uint64_t now = rex::graphics::perf::NowNs();
  if (!last_report_ns_ || now - last_report_ns_ < 2000000000ull) {
    return;
  }
  const double seconds = double(now - last_report_ns_) / 1e9;
  const uint64_t delta = swaps_ - last_report_swaps_;
  const uint64_t draw_delta = draws_ - last_report_draws_;
  size_t state_key_count = 0;
  size_t pipeline_count = 0;
  {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    state_key_count = pipeline_state_keys_.size();
    pipeline_count = pipelines_.size();
  }
  REXLOG_INFO(
      "[native] swap {} {}x{} rate={:.1f}/s draws={} draws_per_swap={:.0f} indices={} "
      "state_keys={} pipelines={} rendered={} textures={} shared={} MiB maxverts={} big={} "
      "tiny={}",
      swaps_, frontbuffer_width, frontbuffer_height,
      seconds > 0.0 ? double(delta) / seconds : 0.0, draws_,
      delta ? double(draw_delta) / double(delta) : 0.0, indices_, state_key_count,
      pipeline_count, draws_rendered_, textures_.size(),
      uint64_t(shared_memory_upload_offset_ >> 20), max_draw_vertices_, big_draws_, tiny_draws_);
  last_report_draws_ = draws_;
  big_draws_ = 0;
  tiny_draws_ = 0;
  last_report_swaps_ = swaps_;
  last_report_draws_ = draws_;
  if (copy_budget_hits_ && !copy_budget_logged_) {
    copy_budget_logged_ = true;
    REXLOG_INFO("[native] S10: vertex upload hit the {} MiB per-frame ceiling {} times",
                (uint64_t(32768) * 4096) >> 20, copy_budget_hits_);
  }
  if (uncommitted_page_logs_ < 4 && uncommitted_pages_skipped_ >= 4096) {
    ++uncommitted_page_logs_;
    REXLOG_INFO("[native] S10: {} guest pages skipped as not committed", uncommitted_pages_skipped_);
    uncommitted_pages_skipped_ = 0;
  }
  last_report_ns_ = now;
}

void SwitchNativeCommandProcessor::LogDeviceLimits() {
  const ui::vulkan::VulkanDevice::Properties& props = device_->properties();
  // The limits the renderer design depends on, so every hardware log answers
  // "is this device capable of the layout we are about to build?".
  uint32_t max_push_constants = 0;
  if (const ui::vulkan::VulkanInstance* instance = device_->vulkan_instance()) {
    VkPhysicalDeviceProperties raw{};
    instance->functions().vkGetPhysicalDeviceProperties(device_->physical_device(), &raw);
    max_push_constants = raw.limits.maxPushConstantsSize;
    REXLOG_INFO(
        "[native] raw limits: maxBoundDescriptorSets={} maxPerStageDescriptorUniformBuffers={} "
        "maxDescriptorSetUniformBuffers={} maxUniformBufferRange={}",
        raw.limits.maxBoundDescriptorSets, raw.limits.maxPerStageDescriptorUniformBuffers,
        raw.limits.maxDescriptorSetUniformBuffers, raw.limits.maxUniformBufferRange);
  }
  REXLOG_INFO(
      "[native] device '{}': maxPushConstants={} maxPerStageSampledImages={} "
      "maxPerStageResources={} maxVertexOutputComponents={} maxImageDimension2D={} "
      "tessellation={} geometry={} colorSamples=0x{:X}",
      props.deviceName, max_push_constants, props.maxPerStageDescriptorSampledImages,
      props.maxPerStageResources, props.maxVertexOutputComponents, props.maxImageDimension2D,
      props.tessellationShader ? 1 : 0, props.geometryShader ? 1 : 0,
      uint32_t(props.framebufferColorSampleCounts));
  REXLOG_INFO("[native] maxStorageBufferRange={} maxStorageBufferRange/512MB -> shared memory "
              "buffers={}",
              props.maxStorageBufferRange,
              uint32_t(1) << SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2(
                  props.maxStorageBufferRange));
}

bool SwitchNativeCommandProcessor::CreatePipelineInfrastructure() {
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

  // Set 0 of SpirvShaderTranslator::DescriptorSet - shared memory and EDRAM.
  // The vertex shader fetches attributes straight out of it, so this set is what
  // makes real geometry possible; binding 1 (EDRAM) is declared even though the
  // native backend does not emulate EDRAM yet, so the set layout matches the
  // shaders that do declare it.
  shared_memory_buffer_count_ = uint32_t(1)
                                << SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2(
                                       device_->properties().maxStorageBufferRange);
  VkDescriptorSetLayoutBinding shared_memory_bindings[2] = {};
  // Binding 0: guest memory. When one storage buffer cannot address the whole
  // 512 MB window the translator declares the buffer as an array, so the
  // descriptor count must match, otherwise the pipeline layout is unusable.
  shared_memory_bindings[0].binding = 0;
  shared_memory_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  shared_memory_bindings[0].descriptorCount = shared_memory_buffer_count_;
  shared_memory_bindings[0].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  // Binding 1: EDRAM.
  shared_memory_bindings[1].binding = 1;
  shared_memory_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  shared_memory_bindings[1].descriptorCount = 1;
  shared_memory_bindings[1].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  layout_info.bindingCount = uint32_t(std::size(shared_memory_bindings));
  layout_info.pBindings = shared_memory_bindings;
  VkResult step_result = dfn.vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                                         &descriptor_set_layout_shared_memory_);
  REXLOG_INFO("[native] S4 step: descriptor set layout (shared memory x{}) -> {} handle={}",
              shared_memory_buffer_count_, int32_t(step_result),
              static_cast<const void*>(descriptor_set_layout_shared_memory_));
  if (step_result != VK_SUCCESS) {
    return false;
  }

  // Set 1 - the five guest draw constants expected by the translator.
  constexpr uint32_t kConstantCount = SpirvShaderTranslator::kConstantBufferCount;
  VkDescriptorSetLayoutBinding constant_bindings[kConstantCount] = {};
  for (uint32_t i = 0; i < kConstantCount; ++i) {
    constant_bindings[i].binding = i;
    // Dynamic so one ring buffer serves every draw of the frame.
    constant_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    constant_bindings[i].descriptorCount = 1;
    constant_bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  layout_info.bindingCount = kConstantCount;
  layout_info.pBindings = constant_bindings;
  step_result = dfn.vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                                &descriptor_set_layout_constants_);
  REXLOG_INFO("[native] S4 step: descriptor set layout ({} constants) -> {}", kConstantCount,
              int32_t(step_result));
  if (step_result != VK_SUCCESS) {
    return false;
  }

  // Sets 2 and 3 (textures) - placeholder empty layouts until S6 so the pipeline
  // layout already has the final shape.
  layout_info.bindingCount = 0;
  layout_info.pBindings = nullptr;
  step_result = dfn.vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                                &descriptor_set_layout_empty_);
  REXLOG_INFO("[native] S4 step: descriptor set layout (empty placeholder) -> {}",
              int32_t(step_result));
  if (step_result != VK_SUCCESS) {
    return false;
  }

  VkDescriptorSetLayout pipeline_set_layouts[4] = {
      descriptor_set_layout_shared_memory_, descriptor_set_layout_constants_,
      descriptor_set_layout_empty_, descriptor_set_layout_empty_};
  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = uint32_t(std::size(pipeline_set_layouts));
  pipeline_layout_info.pSetLayouts = pipeline_set_layouts;
  step_result =
      dfn.vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_);
  REXLOG_INFO("[native] S4 step: pipeline layout with 4 sets -> {} handle={}",
              int32_t(step_result), static_cast<const void*>(pipeline_layout_));
  if (step_result != VK_SUCCESS) {
    REXLOG_ERROR("[native] S2: vkCreatePipelineLayout failed (result={})", int32_t(step_result));
    return false;
  }

  VkPipelineCacheCreateInfo pipeline_cache_info{};
  pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  step_result = dfn.vkCreatePipelineCache(device, &pipeline_cache_info, nullptr, &pipeline_cache_);
  REXLOG_INFO("[native] S2 step: pipeline cache -> {} handle={}", int32_t(step_result),
              static_cast<const void*>(pipeline_cache_));
  if (step_result != VK_SUCCESS) {
    return false;
  }

  REXLOG_INFO("[native] S4: {} constant buffer bindings, empty + constants sets, pipeline cache",
              kConstantCount);

  // The guest memory window and the descriptor sets that reference it have to
  // exist before the first draw, so failures are visible in the startup log
  // instead of silently losing the first frame.
  if (EnsureSharedMemoryBuffer()) {
    const bool sets_ok = CreateDescriptorSets();
    if (!sets_ok) {
      REXLOG_ERROR("[native] S2: descriptor pool / sets failed");
    }
  }
  return true;
}

bool SwitchNativeCommandProcessor::RunPipelineSelfTest() {
  if (pipeline_layout_ == VK_NULL_HANDLE || pipeline_cache_ == VK_NULL_HANDLE) {
    return false;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  auto create_module = [&](const uint32_t* code, size_t dword_count) -> VkShaderModule {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = dword_count * sizeof(uint32_t);
    create_info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (dfn.vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    return module;
  };
  VkShaderModule vertex_module =
      create_module(kSelfTestVertexSpirv, sizeof(kSelfTestVertexSpirv) / sizeof(uint32_t));
  VkShaderModule pixel_module =
      create_module(kSelfTestPixelSpirv, sizeof(kSelfTestPixelSpirv) / sizeof(uint32_t));
  if (vertex_module == VK_NULL_HANDLE || pixel_module == VK_NULL_HANDLE) {
    REXLOG_ERROR("[native] S2: self-test shader module creation failed");
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_module;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = pixel_module;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterization{};
  rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_NONE;
  rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.blendEnable = VK_FALSE;
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  // The guest output image is the target, so the pipeline is created for that
  // format through dynamic rendering - the native backend has no render passes.
  const VkFormat color_format = ui::vulkan::VulkanPresenter::kGuestOutputFormat;
  const VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &color_format;
  rendering_info.depthAttachmentFormat = depth_format;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &rendering_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterization;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pColorBlendState = &color_blend;
  VkPipelineDepthStencilStateCreateInfo marker_depth{};
  marker_depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  marker_depth.depthTestEnable = VK_FALSE;
  marker_depth.depthWriteEnable = VK_FALSE;
  marker_depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  marker_depth.stencilTestEnable = VK_FALSE;
  pipeline_info.pDepthStencilState = &marker_depth;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = VK_NULL_HANDLE;

  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult create_result =
      dfn.vkCreateGraphicsPipelines(device, pipeline_cache_, 1, &pipeline_info, nullptr, &pipeline);
  dfn.vkDestroyShaderModule(device, vertex_module, nullptr);
  dfn.vkDestroyShaderModule(device, pixel_module, nullptr);
  if (create_result != VK_SUCCESS) {
    REXLOG_ERROR("[native] S2: vkCreateGraphicsPipelines failed (result={})",
                 int32_t(create_result));
    return false;
  }

  // Created, so the layout/cache/format combination is valid on this device. It is
  // kept as a diagnostic marker drawn over the guest frame.
  debug_pipeline_ = pipeline;
  return true;
}

void SwitchNativeCommandProcessor::TrackPipelineState(xenos::PrimitiveType prim_type) {
  const RegisterFile& regs = *register_file_;
  PipelineStateKey key{};

  // Shaders: the pipeline is keyed by what was actually translated, not by the
  // guest code address, so identical ucode shared by many draw calls collapses.
  if (Shader* vertex_shader = active_vertex_shader()) {
    key.vertex_shader_hash = static_cast<vulkan::VulkanShader*>(vertex_shader)->ucode_data_hash();
  }
  if (Shader* pixel_shader = active_pixel_shader()) {
    key.pixel_shader_hash = static_cast<vulkan::VulkanShader*>(pixel_shader)->ucode_data_hash();
  }

  key.primitive_type = uint32_t(prim_type);
  // Depth and stencil: the shared helper applies the Xbox 360 defaults for the
  // "depth control is the same as the previous one" encoding.
  key.depth_control = draw_util::GetNormalizedDepthControl(regs).value;
  // Colour write mask, one nibble per render target.
  key.color_mask = regs.Get<reg::RB_COLOR_MASK>().value;
  // Culling, front face and polygon mode.
  key.pa_su_sc_mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>().value;
  key.debug_bits = (g_toggles.flat_pixel_shader ? 1u : 0u) |
                   (g_toggles.marker_vertex_shader ? 2u : 0u) |
                   (g_toggles.probe_pixel_shader ? 4u : 0u) |
                   (g_toggles.blend_enabled ? 0u : 8u) |
                   (g_toggles.depth_enabled ? 0u : 16u);
  // Alpha-to-mask and per-target blend enable bits.
  key.color_control = regs.Get<reg::RB_COLORCONTROL>().value;
  // Blend factors and operations, four render targets. The upper bits of the
  // register are not part of the blend state.
  for (uint32_t i = 0; i < 4; ++i) {
    key.blend_control[i] =
        regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFFu;
  }

  auto inserted = pipeline_state_keys_.try_emplace(key, 0);
  inserted.first->second += 1;
  ++state_key_draws_;
  last_state_key_ = key;

  // Print the first handful of keys with their contents, so the fields that
  // actually vary can be seen without a debugger.
  if (inserted.second && logged_state_keys_ < 8) {
    ++logged_state_keys_;
    REXLOG_INFO(
        "[native] S3 state key #{}: vs={:016X} ps={:016X} prim={} depth={:08X} "
        "color_mask={:08X} sc_mode={:08X} color_control={:08X} blend=[{:08X} {:08X} {:08X} {:08X}]",
        pipeline_state_keys_.size(), key.vertex_shader_hash, key.pixel_shader_hash,
        key.primitive_type, key.depth_control, key.color_mask, key.pa_su_sc_mode,
        key.color_control, key.blend_control[0], key.blend_control[1], key.blend_control[2],
        key.blend_control[3]);
  }
}

bool SwitchNativeCommandProcessor::EnsureSharedMemoryBuffer() {
  if (shared_memory_ready_) {
    return true;
  }
  if (!memory_) {
    REXLOG_ERROR("[native] S4: no guest memory object, cannot expose vertex data");
    return false;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  // The guest physical heap, which is what the shaders address. Reading past
  // its end takes the console down, so the window is sized from the heap.
  VkDeviceSize heap_size = 0;
  rex::memory::VirtualHeap* physical_heap = memory_->GetPhysicalHeap();
  if (physical_heap) {
    heap_size = VkDeviceSize(physical_heap->heap_size());
  }
  const VkDeviceSize kSharedMemoryWindowSize =
      heap_size != 0 ? heap_size : (VkDeviceSize(512) << 20);
  REXLOG_INFO("[native] S4: guest physical heap {} MiB, window {} MiB",
              uint64_t(heap_size >> 20), uint64_t(kSharedMemoryWindowSize >> 20));

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = kSharedMemoryWindowSize;
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (dfn.vkCreateBuffer(device, &buffer_info, nullptr, &shared_memory_buffer_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateBuffer for guest memory failed");
    return false;
  }

  REXLOG_INFO("[native] S4: guest window buffer created ({} MiB)",
              uint64_t(kSharedMemoryWindowSize >> 20));
  VkMemoryRequirements requirements{};
  dfn.vkGetBufferMemoryRequirements(device, shared_memory_buffer_, &requirements);
  REXLOG_INFO("[native] S4: guest window memory requirements {} bytes, type bits 0x{:X}",
              uint64_t(requirements.size), requirements.memoryTypeBits);

  // Host visible and coherent: the CPU writes guest pages into it directly, and
  // on a unified-memory device this costs no copy.
  const uint32_t candidates = device_->memory_types().host_coherent &
                              device_->memory_types().host_visible & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S4: no host coherent memory type for the guest memory buffer");
    dfn.vkDestroyBuffer(device, shared_memory_buffer_, nullptr);
    shared_memory_buffer_ = VK_NULL_HANDLE;
    return false;
  }

  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &shared_memory_memory_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateMemory for {} MiB failed",
                 uint64_t(kSharedMemoryWindowSize >> 20));
    dfn.vkDestroyBuffer(device, shared_memory_buffer_, nullptr);
    shared_memory_buffer_ = VK_NULL_HANDLE;
    return false;
  }
  if (dfn.vkBindBufferMemory(device, shared_memory_buffer_, shared_memory_memory_, 0) !=
      VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkBindBufferMemory failed");
    return false;
  }
  REXLOG_INFO("[native] S4: guest window memory bound, mapping");
  if (dfn.vkMapMemory(device, shared_memory_memory_, 0, VK_WHOLE_SIZE, 0, &shared_memory_mapped_) !=
      VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkMapMemory failed");
    return false;
  }

  shared_memory_size_ = kSharedMemoryWindowSize;
  shared_memory_upload_offset_ = 0;
  REXLOG_INFO("[native] S4: guest window allocated and mapped, uploading in chunks");

  // The guest heap is not fully resident at boot, so it is uploaded lazily: a
  // bounded number of pages per frame, logged as it advances. Geometry that the
  // first frames reference lives low in the heap and is therefore available
  // almost immediately.
  UploadGuestMemoryChunk(VkDeviceSize(4) << 20);

  shared_memory_ready_ = true;
  REXLOG_INFO("[native] S4: guest memory window {} MiB mapped and ready",
              uint64_t(kSharedMemoryWindowSize >> 20));
  return true;
}

void SwitchNativeCommandProcessor::UploadGuestMemoryChunk(VkDeviceSize budget_bytes) {
  if (!shared_memory_mapped_ || !memory_ || shared_memory_size_ == 0 ||
      shared_memory_upload_offset_ >= shared_memory_size_) {
    return;
  }
  constexpr VkDeviceSize kPageSize = 64 * 1024;
  rex::memory::BaseHeap* heap = memory_->GetPhysicalHeap();
  uint8_t* destination = static_cast<uint8_t*>(shared_memory_mapped_);

  if (shared_memory_upload_offset_ == 0) {
    // Cheap probes before the bulk copy: the previous attempt died here with no
    // indication of which side was at fault.
    REXLOG_INFO("[native] S4 probe: writing mapped window head and tail");
    destination[0] = 0x5A;
    destination[kPageSize - 1] = 0x5A;
    destination[shared_memory_size_ - 1] = 0x5A;
    REXLOG_INFO("[native] S4 probe: window writable ({} MiB)", uint64_t(shared_memory_size_ >> 20));
    if (heap) {
      REXLOG_INFO("[native] S4 probe: physical heap page 0 access {}",
                  int(heap->QueryRangeAccess(0, uint32_t(kPageSize) - 1)));
    } else {
      REXLOG_INFO("[native] S4 probe: no physical heap");
    }
    REXLOG_INFO("[native] S4 probe: reading guest page 0");
    const volatile uint8_t* first = memory_->TranslatePhysical<const uint8_t*>(0);
    const uint8_t sample = first[0];
    REXLOG_INFO("[native] S4 probe: guest page 0 readable (0x{:02X})", sample);
  }

  VkDeviceSize end = shared_memory_upload_offset_ + budget_bytes;
  if (end > shared_memory_size_) {
    end = shared_memory_size_;
  }
  const VkDeviceSize begin = shared_memory_upload_offset_;
  uint32_t copied = 0;
  uint32_t unmapped = 0;
  for (VkDeviceSize offset = begin; offset < end; offset += kPageSize) {
    if (heap &&
        heap->QueryRangeAccess(uint32_t(offset), uint32_t(offset + kPageSize - 1)) ==
            rex::memory::PageAccess::kNoAccess) {
      ++unmapped;
      continue;
    }
    const uint8_t* source = memory_->TranslatePhysical<const uint8_t*>(uint32_t(offset));
    std::memcpy(destination + offset, source, kPageSize);
    ++copied;
    if (end <= (VkDeviceSize(16) << 20) && (offset & ((VkDeviceSize(1) << 20) - 1)) == 0) {
      REXLOG_INFO("[native] S4 probe: uploaded through {} KiB ({} pages)", uint64_t(offset >> 10),
                  copied);
    }
  }
  shared_memory_upload_offset_ = end;
  REXLOG_INFO("[native] S4: guest window upload {}-{} MiB: {} pages copied, {} unmapped", 
              uint64_t(begin >> 20), uint64_t(end >> 20), copied, unmapped);
}

bool SwitchNativeCommandProcessor::CreateDescriptorSets() {
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  constexpr uint32_t kConstantCount = SpirvShaderTranslator::kConstantBufferCount;

  // Pool sized for the shared sets, the empty set and the per-shader texture
  // sets (each translated shader pair may need its own).
  VkDescriptorPoolSize pool_sizes[4] = {};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[0].descriptorCount = 4 * shared_memory_buffer_count_ + 4;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[1].descriptorCount = 4 * kConstantCount;
  pool_sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  pool_sizes[2].descriptorCount = 128;
  pool_sizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  pool_sizes[3].descriptorCount = 64;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 64;
  pool_info.poolSizeCount = uint32_t(std::size(pool_sizes));
  pool_info.pPoolSizes = pool_sizes;
  if (dfn.vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateDescriptorPool failed");
    return false;
  }

  VkDescriptorSetLayout set_layouts[2] = {descriptor_set_layout_shared_memory_,
                                          descriptor_set_layout_constants_};
  VkDescriptorSetAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate_info.descriptorPool = descriptor_pool_;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &set_layouts[0];
  if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &shared_memory_set_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateDescriptorSets(shared memory) failed");
    return false;
  }
  allocate_info.pSetLayouts = &set_layouts[1];
  if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &constants_set_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateDescriptorSets(constants) failed");
    return false;
  }

  // Separate pool for the per-draw texture sets: it is reset every frame, while
  // the shared sets above live for the whole session.
  VkDescriptorPoolSize texture_pool_sizes[2] = {};
  texture_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  texture_pool_sizes[0].descriptorCount = 16384;
  texture_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  texture_pool_sizes[1].descriptorCount = 8192;
  VkDescriptorPoolCreateInfo texture_pool_info{};
  texture_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  texture_pool_info.maxSets = 2048;
  texture_pool_info.poolSizeCount = uint32_t(std::size(texture_pool_sizes));
  texture_pool_info.pPoolSizes = texture_pool_sizes;
  if (dfn.vkCreateDescriptorPool(device, &texture_pool_info, nullptr, &texture_pool_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S6: vkCreateDescriptorPool(textures) failed");
  }

  // Stand-in resources for the texture sets that are still placeholders.
  if (!InitializeDummyTextureResources()) {
    REXLOG_ERROR("[native] S4: dummy texture resources failed");
  }
  const VkDescriptorSetLayout empty_layout = descriptor_set_layout_empty_;
  allocate_info.pSetLayouts = &empty_layout;
  if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &empty_texture_set_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateDescriptorSets(empty) failed");
    return false;
  }

  // Set 0 binding 0: the guest memory window. The translator declares it as an
  // array when one buffer cannot cover the whole address space, so array
  // descriptors are written for every slot.
  VkDescriptorBufferInfo shared_info{};
  shared_info.buffer = shared_memory_buffer_;
  shared_info.offset = 0;
  shared_info.range = VK_WHOLE_SIZE;
  VkWriteDescriptorSet writes[1 + SpirvShaderTranslator::kConstantBufferCount] = {};
  uint32_t write_count = 0;
  writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[write_count].dstSet = shared_memory_set_;
  writes[write_count].dstBinding = 0;
  writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[write_count].descriptorCount = shared_memory_buffer_count_;
  writes[write_count].pBufferInfo = &shared_info;
  ++write_count;

  // Set 1: the five constant buffers. Until the per-draw buffers exist, all five
  // point at one small placeholder so the layout is exercised end to end.
  constants_placeholder_size_ = 4096;
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = constants_placeholder_size_;
  buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (dfn.vkCreateBuffer(device, &buffer_info, nullptr, &constants_placeholder_buffer_) !=
      VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateBuffer(constants placeholder) failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetBufferMemoryRequirements(device, constants_placeholder_buffer_, &requirements);
  const uint32_t candidates = device_->memory_types().host_coherent &
                              device_->memory_types().host_visible & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S4: no host coherent memory type for the constants buffer");
    return false;
  }
  VkMemoryAllocateInfo allocate_memory_info{};
  allocate_memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_memory_info.allocationSize = requirements.size;
  allocate_memory_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_memory_info, nullptr,
                           &constants_placeholder_memory_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateMemory(constants placeholder) failed");
    return false;
  }
  if (dfn.vkBindBufferMemory(device, constants_placeholder_buffer_, constants_placeholder_memory_,
                             0) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkBindBufferMemory(constants placeholder) failed");
    return false;
  }
  if (!CreateConstantsRing()) {
    return false;
  }
  // Block sizes have to match what WriteDrawConstants packs, because the dynamic
  // offset is applied on top of these ranges.
  const VkDeviceSize constant_ranges[kConstantCount] = {
      sizeof(SpirvShaderTranslator::SystemConstants), VkDeviceSize(16) * 256,
      VkDeviceSize(16) * 256, sizeof(uint32_t) * (8 + 32), sizeof(uint32_t) * 6 * 32};
  VkDescriptorBufferInfo constant_infos[kConstantCount] = {};
  for (uint32_t i = 0; i < kConstantCount; ++i) {
    constant_infos[i].buffer = constants_ring_buffer_;
    constant_infos[i].offset = 0;
    constant_infos[i].range = constant_ranges[i];
    writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[write_count].dstSet = constants_set_;
    writes[write_count].dstBinding = i;
    writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[write_count].descriptorCount = 1;
    writes[write_count].pBufferInfo = &constant_infos[i];
    ++write_count;
  }
  dfn.vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
  REXLOG_INFO("[native] S4: descriptor sets written (shared memory x{}, {} constants)",
              shared_memory_buffer_count_, kConstantCount);
  return true;
}

uint64_t SwitchNativeCommandProcessor::TextureLayoutKey(bool is_vertex, uint32_t texture_count,
                                                        uint32_t sampler_count) {
  return (uint64_t(is_vertex ? 1 : 0) << 63) | (uint64_t(texture_count) << 16) | sampler_count;
}

SwitchNativeCommandProcessor::TextureLayoutEntry&
SwitchNativeCommandProcessor::GetOrCreateTextureLayout(bool is_vertex, uint32_t texture_count,
                                                       uint32_t sampler_count) {
  const uint64_t key = TextureLayoutKey(is_vertex, texture_count, sampler_count);
  auto existing = texture_layouts_.find(key);
  if (existing != texture_layouts_.end()) {
    return existing->second;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  TextureLayoutEntry entry;
  if (texture_count == 0 && sampler_count == 0) {
    entry.layout = descriptor_set_layout_empty_;
    entry.set = empty_texture_set_;
    texture_layouts_.emplace(key, entry);
    return texture_layouts_.find(key)->second;
  }

  const VkShaderStageFlags stage_flags =
      is_vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutBinding bindings[64] = {};
  uint32_t binding_count = 0;
  for (uint32_t i = 0; i < texture_count && binding_count < 64; ++i) {
    VkDescriptorSetLayoutBinding& binding = bindings[binding_count++];
    binding.binding = i;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = stage_flags;
  }
  for (uint32_t i = 0; i < sampler_count && binding_count < 64; ++i) {
    VkDescriptorSetLayoutBinding& binding = bindings[binding_count++];
    binding.binding = texture_count + i;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = stage_flags;
  }
  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = binding_count;
  layout_info.pBindings = bindings;
  if (dfn.vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &entry.layout) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateDescriptorSetLayout(textures {}/{}) failed", texture_count,
                 sampler_count);
    entry.layout = descriptor_set_layout_empty_;
    entry.set = empty_texture_set_;
  } else {
    VkDescriptorSetAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool = descriptor_pool_;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &entry.layout;
    if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &entry.set) != VK_SUCCESS) {
      REXLOG_ERROR("[native] S4: vkAllocateDescriptorSets(textures) failed");
      entry.set = empty_texture_set_;
    } else {
      // Point every binding at the dummy image or the default sampler so no
      // descriptor the shader reads is undefined.
      VkDescriptorImageInfo image_infos[64] = {};
      VkWriteDescriptorSet writes[64] = {};
      uint32_t write_count = 0;
      for (uint32_t i = 0; i < texture_count && write_count < 64; ++i) {
        image_infos[write_count].imageView = dummy_image_view_;
        image_infos[write_count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet& write = writes[write_count];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = entry.set;
        write.dstBinding = i;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &image_infos[write_count];
        ++write_count;
      }
      for (uint32_t i = 0; i < sampler_count && write_count < 64; ++i) {
        image_infos[write_count].sampler = dummy_sampler_;
        VkWriteDescriptorSet& write = writes[write_count];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = entry.set;
        write.dstBinding = texture_count + i;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_infos[write_count];
        ++write_count;
      }
      dfn.vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
    }
  }
  if (texture_layouts_.size() < 8) {
    REXLOG_INFO("[native] S4: texture set layout {} (textures={} samplers={}) as {}", 
                static_cast<const void*>(entry.layout), texture_count, sampler_count,
                is_vertex ? "vertex" : "pixel");
  }
  texture_layouts_.emplace(key, entry);
  return texture_layouts_.find(key)->second;
}

SwitchNativeCommandProcessor::PipelineLayoutEntry&
SwitchNativeCommandProcessor::GetOrCreatePipelineLayout(uint32_t texture_count_vertex,
                                                        uint32_t sampler_count_vertex,
                                                        uint32_t texture_count_pixel,
                                                        uint32_t sampler_count_pixel) {
  const uint64_t key = (uint64_t(texture_count_vertex) << 48) |
                       (uint64_t(sampler_count_vertex) << 32) |
                       (uint64_t(texture_count_pixel) << 16) | uint64_t(sampler_count_pixel);
  auto existing = pipeline_layouts_.find(key);
  if (existing != pipeline_layouts_.end()) {
    return existing->second;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  TextureLayoutEntry& vertex_textures =
      GetOrCreateTextureLayout(true, texture_count_vertex, sampler_count_vertex);
  TextureLayoutEntry& pixel_textures =
      GetOrCreateTextureLayout(false, texture_count_pixel, sampler_count_pixel);
  PipelineLayoutEntry entry;
  entry.textures_vertex = vertex_textures.set;
  entry.textures_pixel = pixel_textures.set;
  VkDescriptorSetLayout set_layouts[4] = {descriptor_set_layout_shared_memory_,
                                          descriptor_set_layout_constants_,
                                          vertex_textures.layout, pixel_textures.layout};
  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = uint32_t(std::size(set_layouts));
  layout_info.pSetLayouts = set_layouts;
  if (dfn.vkCreatePipelineLayout(device, &layout_info, nullptr, &entry.layout) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreatePipelineLayout(textures {}/{}, {}/{}) failed",
                 texture_count_vertex, sampler_count_vertex, texture_count_pixel,
                 sampler_count_pixel);
    entry.layout = pipeline_layout_;
  }
  pipeline_layouts_.emplace(key, entry);
  return pipeline_layouts_.find(key)->second;
}

bool SwitchNativeCommandProcessor::InitializeDummyTextureResources() {
  if (dummy_image_view_ != VK_NULL_HANDLE) {
    return true;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  // Which compressed and small-channel formats this device can sample. The
  // guest stores most textures compressed, so keeping them compressed instead of
  // decoding to RGBA8 is an eightfold cut in texture bandwidth - but only if the
  // target really supports BC blocks in optimal tiling.
  if (const ui::vulkan::VulkanInstance* format_instance = device_->vulkan_instance()) {
    const auto sampled = [&](VkFormat format) {
      VkFormatProperties properties{};
      format_instance->functions().vkGetPhysicalDeviceFormatProperties(
          device_->physical_device(), format, &properties);
      return (properties.optimalTilingFeatures &
              (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) ==
             (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    };
    REXLOG_INFO("[native] S26: format support bc1={} bc3={} r8={} r8g8={} b5g6r5={}",
                sampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) ? 1 : 0,
                sampled(VK_FORMAT_BC3_UNORM_BLOCK) ? 1 : 0, sampled(VK_FORMAT_R8_UNORM) ? 1 : 0,
                sampled(VK_FORMAT_R8G8_UNORM) ? 1 : 0,
                sampled(VK_FORMAT_B5G6R5_UNORM_PACK16) ? 1 : 0);
  }

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent.width = 1;
  image_info.extent.height = 1;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dfn.vkCreateImage(device, &image_info, nullptr, &dummy_image_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateImage(dummy) failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetImageMemoryRequirements(device, dummy_image_, &requirements);
  const uint32_t candidates =
      device_->memory_types().device_local & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S4: no device local memory type for the dummy image");
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &dummy_image_memory_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkAllocateMemory(dummy image) failed");
    return false;
  }
  if (dfn.vkBindImageMemory(device, dummy_image_, dummy_image_memory_, 0) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkBindImageMemory(dummy) failed");
    return false;
  }
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = dummy_image_;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &view_info, nullptr, &dummy_image_view_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateImageView(dummy) failed");
    return false;
  }
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 1.0f;
  if (dfn.vkCreateSampler(device, &sampler_info, nullptr, &dummy_sampler_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: vkCreateSampler(dummy) failed");
    return false;
  }

  // Move the image to the layout the shaders sample from. Done once, before any
  // frame is recorded, using the frame command buffer and fence.
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (dfn.vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
    return false;
  }
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dummy_image_;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  // White, not black: a stand-in that renders as a solid surface proves geometry
  // placement while the real texture cache does not exist yet.
  const VkClearColorValue dummy_clear = {{1.0f, 1.0f, 1.0f, 1.0f}};
  VkImageSubresourceRange dummy_range{};
  dummy_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  dummy_range.levelCount = 1;
  dummy_range.layerCount = 1;
  dfn.vkCmdClearColorImage(command_buffer_, dummy_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           &dummy_clear, 1, &dummy_range);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &barrier);
  if (dfn.vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
    return false;
  }
  dfn.vkResetFences(device, 1, &clear_fence_);
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer_;
  const ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
      device_->AcquireQueue(device_->queue_family_graphics_compute(), 0);
  if (dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, clear_fence_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: dummy image submit failed");
    return false;
  }
  if (dfn.vkWaitForFences(device, 1, &clear_fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: dummy image fence failed");
    return false;
  }
  fence_submitted_ = false;
  REXLOG_INFO("[native] S4: dummy texture and sampler ready");
  return true;
}

bool SwitchNativeCommandProcessor::CreateConstantsRing() {
  if (constants_ring_buffer_ != VK_NULL_HANDLE) {
    return true;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  constants_ring_size_ = VkDeviceSize(32) << 20;
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = constants_ring_size_;
  buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (dfn.vkCreateBuffer(device, &buffer_info, nullptr, &constants_ring_buffer_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S5: vkCreateBuffer(constants ring) failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetBufferMemoryRequirements(device, constants_ring_buffer_, &requirements);
  const uint32_t candidates = device_->memory_types().host_coherent &
                              device_->memory_types().host_visible & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S5: no host coherent memory type for the constants ring");
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &constants_ring_memory_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S5: vkAllocateMemory(constants ring) failed");
    return false;
  }
  if (dfn.vkBindBufferMemory(device, constants_ring_buffer_, constants_ring_memory_, 0) !=
      VK_SUCCESS) {
    REXLOG_ERROR("[native] S5: vkBindBufferMemory(constants ring) failed");
    return false;
  }
  if (dfn.vkMapMemory(device, constants_ring_memory_, 0, VK_WHOLE_SIZE, 0,
                      &constants_ring_mapped_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S5: vkMapMemory(constants ring) failed");
    return false;
  }
  constants_ring_offset_ = 0;
  REXLOG_INFO("[native] S5: constants ring {} MiB mapped", uint64_t(constants_ring_size_ >> 20));
  return true;
}

bool SwitchNativeCommandProcessor::ReadGuestForDiagnostics(uint64_t address, void* out,
                                                           size_t size) const {
  if (!out || size == 0) {
    return true;
  }
  if (!memory_ || address >= shared_memory_size_ || size > shared_memory_size_ - address) {
    return false;
  }
  rex::memory::BaseHeap* const heap = memory_->GetPhysicalHeap();
  constexpr uint64_t kPageSize = 4096;
  auto* destination = static_cast<uint8_t*>(out);
  size_t done = 0;
  while (done < size) {
    const uint64_t at = address + done;
    const uint64_t page = at & ~(kPageSize - 1);
    if (heap && heap->QueryRangeAccess(uint32_t(page), uint32_t(page + kPageSize - 1)) ==
                    rex::memory::PageAccess::kNoAccess) {
      return false;
    }
    uint64_t chunk = kPageSize - (at & (kPageSize - 1));
    if (chunk > uint64_t(size - done)) {
      chunk = uint64_t(size - done);
    }
    const uint8_t* source = memory_->TranslatePhysical<const uint8_t*>(uint32_t(at));
    std::memcpy(destination + done, source, size_t(chunk));
    done += size_t(chunk);
  }
  return true;
}

void SwitchNativeCommandProcessor::PrepareDraw(FrameDraw& draw, uint32_t index_count) {
  uint32_t* offsets_out = draw.constant_offsets;
  float* viewport_out = draw.viewport;
  const auto align_up = [](VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  };
  constexpr VkDeviceSize kAlignment = 256;
  const auto allocate = [&](VkDeviceSize size, uint8_t** out_pointer) -> uint32_t {
    VkDeviceSize offset = align_up(constants_ring_offset_, kAlignment);
    if (offset + size > constants_ring_size_) {
      offset = 0;
    }
    constants_ring_offset_ = offset + size;
    *out_pointer = static_cast<uint8_t*>(constants_ring_mapped_) + offset;
    return uint32_t(offset);
  };
  if (!constants_ring_mapped_) {
    return;
  }
  const RegisterFile& regs = *register_file_;

  // --- System constants: what the translator's vertex shader epilogue needs to
  // place vertices on screen (without the NDC scale and offset every vertex ends
  // up at the same point, which is why the first S4 runs rendered nothing).
  static SpirvShaderTranslator::SystemConstants system_constants;
  std::memset(&system_constants, 0, sizeof(system_constants));
  uint32_t flags = 0;
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  }
  const auto primitive = xenos::PrimitiveType(register_file_->Get<reg::VGT_DRAW_INITIATOR>()
                                                  .prim_type);
  if (primitive == xenos::PrimitiveType::kTriangleList ||
      primitive == xenos::PrimitiveType::kTriangleFan ||
      primitive == xenos::PrimitiveType::kTriangleStrip ||
      primitive == xenos::PrimitiveType::kTriangleWithWFlags) {
    flags |= SpirvShaderTranslator::kSysFlag_PrimitivePolygonal;
  }
  if (draw_util::IsPrimitiveLine(regs)) {
    flags |= SpirvShaderTranslator::kSysFlag_PrimitiveLine;
  }
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  flags |= uint32_t(rb_surface_info.msaa_samples) << SpirvShaderTranslator::kSysFlag_MsaaSamples_Shift;
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= SpirvShaderTranslator::kSysFlag_DepthFloat24;
  }
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  // Alpha testing is off until the textures are proven correct: with a wrong
  // alpha channel the translated shader discards every fragment and the screen
  // stays black while everything else works.
  const xenos::CompareFunction alpha_test_function = xenos::CompareFunction::kAlways;
  (void)rb_colorcontrol;
  flags |= uint32_t(alpha_test_function) << SpirvShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  system_constants.flags = flags;
  // The translated vertex shader remaps the host vertex index with
  // UClamp(index, vertex_index_min, vertex_index_max). Left at zero (the memset
  // default) that collapses every vertex to fetch index 0, so all triangles
  // became degenerate and nothing was visible even with correct pipelines and
  // data. 0..0xFFFFFF is the identity for a 24-bit guest vertex index.
  system_constants.vertex_index_min = 0;
  system_constants.vertex_index_max = xenos::kVertexIndexMask;
  // gl_VertexIndex never equals this, so the line-loop closing remap is a no-op.
  system_constants.line_loop_closing_index = xenos::kVertexIndexMask;
  // The pixel shader multiplies its output by this; zeros render black.
  system_constants.color_exp_bias[0] = 1.0f;
  system_constants.color_exp_bias[1] = 1.0f;
  system_constants.color_exp_bias[2] = 1.0f;
  system_constants.color_exp_bias[3] = 1.0f;

  const reg::RB_DEPTHCONTROL normalized_depth_control =
      draw_util::GetNormalizedDepthControl(regs);
  draw_util::ViewportInfo viewport_info{};
  draw_util::GetHostViewportInfo(regs, 1, 1, false, 1280, 720, true, normalized_depth_control,
                                 false, false, false, viewport_info);
  for (uint32_t i = 0; i < 3; ++i) {
    system_constants.ndc_scale[i] = viewport_info.ndc_scale[i];
    system_constants.ndc_offset[i] = viewport_info.ndc_offset[i];
  }
  if (primitive == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto pa_su_point_size = regs.Get<reg::PA_SU_POINT_SIZE>();
    system_constants.point_vertex_diameter_min = float(pa_su_point_minmax.min_size) * (2.0f / 16.0f);
    system_constants.point_vertex_diameter_max = float(pa_su_point_minmax.max_size) * (2.0f / 16.0f);
    system_constants.point_constant_diameter[0] = float(pa_su_point_size.width) * (2.0f / 16.0f);
    system_constants.point_constant_diameter[1] = float(pa_su_point_size.height) * (2.0f / 16.0f);
    system_constants.point_screen_diameter_to_ndc_radius[0] =
        1.0f / float(std::max(viewport_info.xy_extent[0], uint32_t(1)));
    system_constants.point_screen_diameter_to_ndc_radius[1] =
        1.0f / float(std::max(viewport_info.xy_extent[1], uint32_t(1)));
  }
  static uint32_t logged_draws = 0;
  if (logged_draws < 8) {
    ++logged_draws;
    const auto draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
    // First vertex fetch constant in use: the shader reads its data from there,
    // so the address tells whether the uploaded window can cover it at all.
    uint32_t fetch_type = 0;
    uint32_t fetch_address = 0;
    const Shader::ConstantRegisterMap* map =
        active_vertex_shader() ? &active_vertex_shader()->constant_register_map() : nullptr;
    if (map) {
      for (uint32_t i = 0; i < 96; ++i) {
        if (map->vertex_fetch_bitmap[i >> 5] & (uint32_t(1) << (i & 31))) {
          xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i);
          fetch_type = uint32_t(fetch.type);
          fetch_address = fetch.address << 2;
          break;
        }
      }
    }
    REXLOG_INFO("[native] S5 draw: prim={} vgt_prim={} vp={},{} {}x{}", uint32_t(primitive),
                uint32_t(draw_initiator.prim_type), viewport_info.xy_offset[0],
                viewport_info.xy_offset[1], viewport_info.xy_extent[0],
                viewport_info.xy_extent[1]);
    REXLOG_INFO("[native] S5 draw: ndc_scale={},{},{} ndc_offset={},{},{} flags={:08X}",
                viewport_info.ndc_scale[0], viewport_info.ndc_scale[1], viewport_info.ndc_scale[2],
                viewport_info.ndc_offset[0], viewport_info.ndc_offset[1],
                viewport_info.ndc_offset[2], flags);
    uint32_t vertex_preview[4] = {};
    if (fetch_address &&
        !ReadGuestForDiagnostics(fetch_address, vertex_preview, sizeof(vertex_preview))) {
      vertex_preview[0] = 0xDEADBEEF;
    }
    REXLOG_INFO(
        "[native] S5 draw: count={} fetch_type={} fetch_addr={:08X} first={:08X},{:08X},"
        "{:08X},{:08X}",
        index_count, fetch_type, fetch_address, vertex_preview[0], vertex_preview[1],
        vertex_preview[2], vertex_preview[3]);
  }
  // S8: sample real content, not the loading screen. Every fetch constant of
  // the draw plus the first raw dwords answer "is there geometry at all" without
  // any GPU readback. Raw words, not floats: most fetch constants use a packed
  // endian mode, so the shader unpacks them later.
  static uint32_t logged_geometry_draws = 0;
  if (logged_geometry_draws < 6 && draws_ > 20000 && index_count >= 3) {
    ++logged_geometry_draws;
    REXLOG_INFO("[native] S7 geom: prim={} count={} indexed={} viewport={}x{}+{},{} vs={:016X} "
                "ps={:016X} flat={}",
                uint32_t(primitive), index_count, draw.indexed ? 1 : 0,
                viewport_info.xy_extent[0], viewport_info.xy_extent[1],
                viewport_info.xy_offset[0], viewport_info.xy_offset[1],
                active_vertex_shader()
                    ? static_cast<vulkan::VulkanShader*>(active_vertex_shader())->ucode_data_hash()
                    : 0ull,
                active_pixel_shader()
                    ? static_cast<vulkan::VulkanShader*>(active_pixel_shader())->ucode_data_hash()
                    : 0ull,
                REXCVAR_QUERY(bool, native_flat_ps) ? 1 : 0);
    const Shader* vertex_program = active_vertex_shader();
    uint32_t first_address = 0;
    if (vertex_program) {
      for (uint32_t i = 0; i < xenos::kVertexFetchConstantCount; ++i) {
        const xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i);
        if (fetch.type != xenos::FetchConstantType::kVertex) {
          continue;
        }
        REXLOG_INFO("[native] S7 geom: fetch[{}] type={} addr={:08X} size={} endian={}", i,
                    uint32_t(fetch.type), fetch.address << 2, fetch.size,
                    uint32_t(fetch.endian));
        if (!first_address && fetch.type == xenos::FetchConstantType::kVertex) {
          first_address = fetch.address << 2;
        }
      }
    }
    if (first_address) {
      uint32_t words[16] = {};
      if (ReadGuestForDiagnostics(first_address, words, sizeof(words))) {
        REXLOG_INFO("[native] S7 geom: words {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} "
                    "{:08X}",
                    words[0], words[1], words[2], words[3], words[4], words[5], words[6],
                    words[7]);
        REXLOG_INFO("[native] S7 geom: words+ {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} "
                    "{:08X}",
                    words[8], words[9], words[10], words[11], words[12], words[13], words[14],
                    words[15]);
      } else {
        REXLOG_INFO("[native] S7 geom: first fetch address {:08X} is not readable", first_address);
      }
    }
    // The vertex float constants usually hold the world/view/projection matrices,
    // so they say whether the transform can ever land inside the viewport.
    float vs_constants[16] = {};
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t raw = regs[XE_GPU_REG_SHADER_CONSTANT_000_X + i];
      std::memcpy(&vs_constants[i], &raw, sizeof(float));
    }
    REXLOG_INFO("[native] S16 xform: viewport={}x{}+{},{} ndc_scale={},{},{} ndc_offset={},{},{} "
                "flags={:08X}",
                viewport_info.xy_extent[0], viewport_info.xy_extent[1],
                viewport_info.xy_offset[0], viewport_info.xy_offset[1],
                viewport_info.ndc_scale[0], viewport_info.ndc_scale[1], viewport_info.ndc_scale[2],
                viewport_info.ndc_offset[0], viewport_info.ndc_offset[1],
                viewport_info.ndc_offset[2], flags);
    // Which branch of GetHostViewportInfo produced the transform above, and what
    // the raw viewport registers contain. A pixel-space scale (2/extent) with
    // clip_disable = 0 would mean the register file lost the viewport scale, and
    // the shader would be multiplying real clip-space positions by ~1/640.
    auto pa_cl_clip_cntl_dbg = regs.Get<reg::PA_CL_CLIP_CNTL>();
    auto pa_cl_vte_cntl_dbg = regs.Get<reg::PA_CL_VTE_CNTL>();
    REXLOG_INFO("[native] S18 clip: clip_disable={} vte_xy_fmt={} vte_z_fmt={} vte_w0_fmt={} "
                "x_ena={} y_ena={} z_ena={} xoff_ena={} yoff_ena={} zoff_ena={} "
                "xscale={} yscale={} zscale={} xoffset={} yoffset={} zoffset={}",
                pa_cl_clip_cntl_dbg.clip_disable ? 1 : 0, pa_cl_vte_cntl_dbg.vtx_xy_fmt ? 1 : 0,
                pa_cl_vte_cntl_dbg.vtx_z_fmt ? 1 : 0, pa_cl_vte_cntl_dbg.vtx_w0_fmt ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_x_scale_ena ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_y_scale_ena ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_z_scale_ena ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_x_offset_ena ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_y_offset_ena ? 1 : 0,
                pa_cl_vte_cntl_dbg.vport_z_offset_ena ? 1 : 0,
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE),
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE),
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_ZSCALE),
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET),
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET),
                regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_ZOFFSET));
    REXLOG_INFO("[native] S9 c0={},{},{},{} c1={},{},{},{}", vs_constants[0], vs_constants[1],
                vs_constants[2], vs_constants[3], vs_constants[4], vs_constants[5],
                vs_constants[6], vs_constants[7]);
    REXLOG_INFO("[native] S9 c2={},{},{},{} c3={},{},{},{}", vs_constants[8], vs_constants[9],
                vs_constants[10], vs_constants[11], vs_constants[12], vs_constants[13],
                vs_constants[14], vs_constants[15]);
  }
  // One-shot dump of the raw vertex data behind every active vertex fetch
  // constant. The translated shader reads these bytes as floats directly, so the
  // numbers say whether the geometry is garbage or only mis-transformed: the first
  // run with correct vertex indices filled the whole screen with triangles at
  // 3.8 fps, which is what wrong vertex data looks like from the outside.
  {
    static bool fetch_data_dumped[xenos::kVertexFetchConstantCount] = {};
    if (active_vertex_shader() && index_count >= 2000) {
      uint32_t first_guest_index = 0;
      bool have_index = false;
      if (draw.indexed && draw.index_count) {
        if (draw.index_type == VK_INDEX_TYPE_UINT32) {
          have_index = ReadGuestForDiagnostics(draw.index_base_bytes, &first_guest_index,
                                               sizeof(first_guest_index));
        } else {
          uint16_t index16 = 0;
          have_index = ReadGuestForDiagnostics(draw.index_base_bytes, &index16, sizeof(index16));
          first_guest_index = index16;
        }
      }
      for (uint32_t i = 0; i < xenos::kVertexFetchConstantCount; ++i) {
        if (fetch_data_dumped[i]) {
          continue;
        }
        const xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i);
        if (fetch.type != xenos::FetchConstantType::kVertex || fetch.address == 0 ||
            fetch.size == 0) {
          continue;
        }
        uint32_t raw[16] = {};
        if (!ReadGuestForDiagnostics(fetch.address << 2, raw, sizeof(raw))) {
          continue;
        }
        fetch_data_dumped[i] = true;
        float f[16];
        std::memcpy(f, raw, sizeof(f));
        REXLOG_INFO("[native] S17 vfetch[{}] addr={:08X} size={}dw word4={:08X} word5={:08X}", i,
                    fetch.address << 2, fetch.size, regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                                           i * 6 + 4],
                    regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + i * 6 + 5]);
        REXLOG_INFO("[native] S17 vfetch[{}] base i0={:.3f},{:.3f},{:.3f} i1={:.3f},{:.3f},{:.3f}",
                    i, f[0], f[1], f[2], f[3], f[4], f[5]);
        // The guest constant says endian 2, and the translator compensates by
        // swapping 8-bit units inside each 32-bit word, so the raw dwords above
        // are NOT what the shader sees. Print the corrected values, and the 16-bit
        // halves as halves, so a compressed format is recognisable too.
        auto Swap8In32 = [](uint32_t v) {
          return ((v >> 8) & UINT32_C(0x00FF00FF)) | ((v & UINT32_C(0x00FF00FF)) << 8);
        };
        float s0, s1, s2;
        const uint32_t swapped0 = Swap8In32(raw[0]), swapped1 = Swap8In32(raw[1]),
                       swapped2 = Swap8In32(raw[2]);
        std::memcpy(&s0, &swapped0, sizeof(float));
        std::memcpy(&s1, &swapped1, sizeof(float));
        std::memcpy(&s2, &swapped2, sizeof(float));
        REXLOG_INFO("[native] S17 vfetch[{}] swapped i0={:.3f},{:.3f},{:.3f} raw={:08X},{:08X},"
                    "{:08X}",
                    i, s0, s1, s2, raw[0], raw[1], raw[2]);
        REXLOG_INFO("[native] S17 vfetch[{}] halves i0.lo={:.4f} i0.hi={:.4f} i1.lo={:.4f} "
                    "i1.hi={:.4f}",
                    i, rex::xenos_half_to_float(uint16_t(raw[0] & 0xFFFF)),
                    rex::xenos_half_to_float(uint16_t(raw[0] >> 16)),
                    rex::xenos_half_to_float(uint16_t(raw[1] & 0xFFFF)),
                    rex::xenos_half_to_float(uint16_t(raw[1] >> 16)));
        // The shader fetches 3 dwords per vertex from base + index * 3, so the
        // drawn vertices live at first_index * 12 bytes, not at the buffer start.
        if (have_index) {
          uint32_t indexed_raw[12] = {};
          const uint32_t byte_address =
              (fetch.address << 2) + first_guest_index * 3 * uint32_t(sizeof(uint32_t));
          if (ReadGuestForDiagnostics(byte_address, indexed_raw, sizeof(indexed_raw))) {
            float fi[12];
            std::memcpy(fi, indexed_raw, sizeof(fi));
            REXLOG_INFO("[native] S17 vfetch[{}] idx={} at={:08X} v0={:.3f},{:.3f},{:.3f} "
                        "v1={:.3f},{:.3f},{:.3f}",
                        i, first_guest_index, byte_address, fi[0], fi[1], fi[2], fi[3], fi[4],
                        fi[5]);
          } else {
            REXLOG_INFO("[native] S17 vfetch[{}] idx={} at={:08X} unreadable", i, first_guest_index,
                        byte_address);
          }
        }
      }
    }
  }
  static uint32_t trace_draws = 0;
  const bool trace_this_draw = trace_draws < 3 && index_count >= 100;
  if (trace_this_draw) {
    ++trace_draws;
    REXLOG_INFO("[native] S11 trace: draw {} begin (count={} indexed={})", trace_draws, index_count,
                draw.indexed ? 1 : 0);
  }

  // --- Textures of this draw: decoded from guest memory into cached images.
  const auto gather_textures = [&](Shader* shader, std::vector<VkImageView>& views,
                                   std::vector<VkSampler>& samplers) {
    auto* spirv_shader = static_cast<SpirvShader*>(shader);
    if (!spirv_shader) {
      return;
    }
    const auto& texture_bindings = spirv_shader->GetTextureBindingsAfterTranslation();
    const auto& sampler_bindings = spirv_shader->GetSamplerBindingsAfterTranslation();
    views.reserve(texture_bindings.size());
    samplers.reserve(sampler_bindings.size());
    for (const SpirvShader::TextureBinding& binding : texture_bindings) {
      const xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(binding.fetch_constant);
      views.push_back(GetOrCreateTexture(fetch));
    }
    for (const SpirvShader::SamplerBinding& binding : sampler_bindings) {
      const xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(binding.fetch_constant);
      samplers.push_back(GetOrCreateSampler(fetch));
    }
  };
  gather_textures(active_vertex_shader(), draw.texture_views_vertex, draw.samplers_vertex);
  gather_textures(active_pixel_shader(), draw.texture_views_pixel, draw.samplers_pixel);

  viewport_out[0] = float(viewport_info.xy_offset[0]);
  viewport_out[1] = float(viewport_info.xy_offset[1]);
  viewport_out[2] = float(viewport_info.xy_extent[0]);
  viewport_out[3] = float(viewport_info.xy_extent[1]);
  // Guest window scissor, captured for the same reason as the viewport: the
  // frame is replayed at swap time, when the register file has moved on.
  {
    draw_util::Scissor guest_scissor{};
    draw_util::GetScissor(regs, guest_scissor, true);
    draw.scissor[0] = guest_scissor.offset[0];
    draw.scissor[1] = guest_scissor.offset[1];
    draw.scissor[2] = guest_scissor.extent[0];
    draw.scissor[3] = guest_scissor.extent[1];
  }

  uint8_t* write_pointer = nullptr;
  offsets_out[SpirvShaderTranslator::kConstantBufferSystem] =
      allocate(sizeof(system_constants), &write_pointer);
  std::memcpy(write_pointer, &system_constants, sizeof(system_constants));

  // --- Float constants: the translator packs only the registers the shader
  // reads, in bitmap order, so the same order has to be reproduced here.
  const auto write_float_constants = [&](Shader* shader, uint32_t base_register, uint32_t index) {
    offsets_out[index] = allocate(VkDeviceSize(16) * 256, &write_pointer);
    if (!shader || !write_pointer) {
      return;
    }
    const Shader::ConstantRegisterMap& map = shader->constant_register_map();
    uint32_t packed_index = 0;
    for (uint32_t i = 0; i < 256; ++i) {
      if (!(map.float_bitmap[i >> 6] & (uint64_t(1) << (i & 63)))) {
        continue;
      }
      std::memcpy(write_pointer + packed_index * 16, &regs[base_register + i * 4], 16);
      ++packed_index;
    }
  };
  write_float_constants(active_vertex_shader(), XE_GPU_REG_SHADER_CONSTANT_000_X,
                        SpirvShaderTranslator::kConstantBufferFloatVertex);
  write_float_constants(active_pixel_shader(), XE_GPU_REG_SHADER_CONSTANT_256_X,
                        SpirvShaderTranslator::kConstantBufferFloatPixel);

  // --- Bool and loop constants, then the 32 fetch constants.
  // --- Vertex data for this draw, copied on demand. The sequential window
  // upload skips pages the heap reports as unallocated, which is exactly where
  // streamed level and texture data can live, so anything a fetch constant
  // points at is copied here regardless.
  {
    // Upload what this draw can read from guest memory: every vertex fetch
    // constant and, for indexed draws, the index buffer itself.
    //
    // The fetch bitmap is NOT a safe guide: it only lists constants the
    // shader indexes with a compile-time constant, and streamed meshes address
    // vertex buffers through a register. Those draws have an empty bitmap, so
    // trusting it uploaded nothing at all - the shader read zeros, every vertex
    // landed in the same place and the whole scene was invisible while the
    // frame rate, the draw count and every Vulkan call stayed correct.
    uint8_t* window = static_cast<uint8_t*>(shared_memory_mapped_);
    // Reading guest pages that the title has not committed faults on Horizon -
    // the same reason the startup snapshot had to become a residency-checked
    // chunked upload. Sweeping every fetch constant means touching addresses the
    // guest may never have written, so each page is checked before it is read.
    rex::memory::BaseHeap* guest_heap = memory_->GetPhysicalHeap();
    constexpr VkDeviceSize kPageSize = 4096;
    // A single draw never needs more than this much of one buffer; the field is
    // 24 bits wide and garbage constants would otherwise sweep the whole heap.
    constexpr VkDeviceSize kMaxFetchBytes = 8 << 20;
    // The shader's fetch constant block is 32 * 6 dwords and holds 96 vertex
    // fetch constants (three 2-dword pairs per slot). The scene shader really
    // does address 93..95, so all 96 must be swept.
    constexpr uint32_t kVisibleFetchCount = xenos::kVertexFetchConstantCount;
    // Hard ceiling per frame so a garbage fetch constant cannot spend the whole
    // frame copying guest memory.
    constexpr uint32_t kMaxPagesPerFrame = 16384;  // 64 MiB
    const VkDeviceSize page_count = shared_memory_size_ / kPageSize;
    if (copied_pages_.size() != size_t((page_count + 63) / 64)) {
      copied_pages_.assign(size_t((page_count + 63) / 64), 0);
    }
    const auto copy_range = [&](VkDeviceSize address, VkDeviceSize size) {
      if (!window || size == 0 || address >= shared_memory_size_) {
        return;
      }
      if (address + size > shared_memory_size_) {
        size = shared_memory_size_ - address;
      }
      if (size > kMaxFetchBytes) {
        size = kMaxFetchBytes;
      }
      VkDeviceSize first_page = address / kPageSize;
      VkDeviceSize last_page = (address + size + kPageSize - 1) / kPageSize;
      if (last_page > page_count) {
        last_page = page_count;
      }
      for (VkDeviceSize page = first_page; page < last_page; ++page) {
        uint64_t& bits = copied_pages_[size_t(page >> 6)];
        const uint64_t mask = uint64_t(1) << (page & 63);
        if (bits & mask) {
          continue;
        }
        if (pages_copied_this_frame_ >= kMaxPagesPerFrame) {
          ++copy_budget_hits_;
          return;
        }
        bits |= mask;
        ++pages_copied_this_frame_;
        if (guest_heap &&
            guest_heap->QueryRangeAccess(uint32_t(page * kPageSize),
                                         uint32_t(page * kPageSize + kPageSize - 1)) ==
                rex::memory::PageAccess::kNoAccess) {
          ++uncommitted_pages_skipped_;
          continue;
        }
        const uint8_t* source =
            memory_->TranslatePhysical<const uint8_t*>(uint32_t(page * kPageSize));
        std::memcpy(window + page * kPageSize, source, size_t(kPageSize));
        ++vertex_pages_copied_;
      }
    };
    // Indices first: without them the draw is guaranteed to be wrong, while a
    // partial vertex upload still draws most of the mesh.
    if (draw.indexed) {
      copy_range(draw.index_base_bytes,
                 VkDeviceSize(draw.index_count) *
                     (draw.index_type == VK_INDEX_TYPE_UINT32 ? 4 : 2));
    }
    // Descending order on purpose: the guest fetches its vertices through the
    // high constants (the scene shader uses 93, 94 and 95 - the three pairs of
    // register slot 31), and the per-frame ceiling must be spent on those before
    // the unused ones. With an ascending sweep the budget was gone by the time
    // the loop reached them and the real data was never uploaded.
    for (uint32_t i = kVisibleFetchCount; i-- > 0;) {
      const xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(i);
      if (fetch.type != xenos::FetchConstantType::kVertex) {
        continue;
      }
      const VkDeviceSize fetch_size = VkDeviceSize(fetch.size) * 4;
      // Sanity filters: a garbage constant must not eat the budget. Real vertex
      // buffers in this title are a few MiB at most.
      if (fetch_size == 0 || fetch.address == 0 || fetch_size > (16u << 20) ||
          VkDeviceSize(fetch.address) * 4 >= shared_memory_size_) {
        continue;
      }
      copy_range(VkDeviceSize(fetch.address) * 4, fetch_size);
      ++vertex_fetch_copies_;
    }
  }

  if (trace_this_draw) {
    REXLOG_INFO("[native] S11 trace: draw {} constants+vertex data staged ({} pages copied)",
                trace_draws, pages_copied_this_frame_);
    if (draw.indexed) {
      uint16_t indices[8] = {};
      const size_t index_bytes =
          size_t(draw.index_count) * (draw.index_type == VK_INDEX_TYPE_UINT32 ? 4 : 2);
      if (ReadGuestForDiagnostics(draw.index_base_bytes, indices,
                                  index_bytes < sizeof(indices) ? index_bytes : sizeof(indices))) {
        REXLOG_INFO("[native] S14 index: base={:08X} count={} type={} first={},{},{},{},{},{},{},"
                    "{}",
                    draw.index_base_bytes, draw.index_count,
                    draw.index_type == VK_INDEX_TYPE_UINT32 ? 32 : 16, indices[0], indices[1],
                    indices[2], indices[3], indices[4], indices[5], indices[6], indices[7]);
      } else {
        REXLOG_INFO("[native] S14 index: base={:08X} is not readable", draw.index_base_bytes);
      }
    }
  }
  // Constant blend factors, set as dynamic state for this draw.
  draw.blend_constants[0] = regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
  draw.blend_constants[1] = regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
  draw.blend_constants[2] = regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
  draw.blend_constants[3] = regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
  constexpr VkDeviceSize kBoolLoopSize = sizeof(uint32_t) * (8 + 32);
  offsets_out[SpirvShaderTranslator::kConstantBufferBoolLoop] =
      allocate(kBoolLoopSize, &write_pointer);
  std::memcpy(write_pointer, &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], kBoolLoopSize);
  constexpr VkDeviceSize kFetchSize = sizeof(uint32_t) * 6 * 32;
  offsets_out[SpirvShaderTranslator::kConstantBufferFetch] = allocate(kFetchSize, &write_pointer);
  std::memcpy(write_pointer, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchSize);
}

namespace {

// Bytes per texel for the uncompressed formats this backend decodes; 0 for
// compressed or unsupported ones.
uint32_t TextureBytesPerTexel(xenos::TextureFormat format) {
  switch (format) {
    case xenos::TextureFormat::k_8:
    case xenos::TextureFormat::k_8_A:
    case xenos::TextureFormat::k_8_B:
      return 1;
    case xenos::TextureFormat::k_8_8:
    case xenos::TextureFormat::k_5_6_5:
    case xenos::TextureFormat::k_1_5_5_5:
    case xenos::TextureFormat::k_4_4_4_4:
      return 2;
    case xenos::TextureFormat::k_8_8_8_8:
    case xenos::TextureFormat::k_8_8_8_8_A:
    case xenos::TextureFormat::k_2_10_10_10:
      return 4;
    default:
      return 0;
  }
}

uint32_t TextureBlockBytes(xenos::TextureFormat format) {
  switch (format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT1_AS_16_16_16_16:
    case xenos::TextureFormat::k_DXT3A:
    case xenos::TextureFormat::k_DXT5A:
      return 8;
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
    case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return 16;
    default:
      return 0;
  }
}

inline uint8_t Expand4(uint32_t value) { return uint8_t((value << 4) | value); }
inline uint8_t Expand5(uint32_t value) { return uint8_t((value << 3) | (value >> 2)); }
inline uint8_t Expand6(uint32_t value) { return uint8_t((value << 2) | (value >> 4)); }

// k_DXT3A and k_DXT5A are single-channel 8-byte block formats (an alpha or a
// mask), not colour. Decoding them as DXT1 - which is what the generic block
// path did - fills alpha with 255, and every layer that should have been killed
// by the alpha test instead shades every pixel of its rectangle.
void DecodeDXT5AlphaBlock(const uint8_t* block, uint8_t out[16]) {
  const uint32_t alpha_0 = block[0];
  const uint32_t alpha_1 = block[1];
  uint32_t palette[8];
  palette[0] = alpha_0;
  palette[1] = alpha_1;
  if (alpha_0 > alpha_1) {
    for (uint32_t i = 0; i < 6; ++i) {
      palette[2 + i] = ((6 - i) * alpha_0 + (1 + i) * alpha_1) / 7;
    }
  } else {
    for (uint32_t i = 0; i < 4; ++i) {
      palette[2 + i] = ((4 - i) * alpha_0 + (1 + i) * alpha_1) / 5;
    }
    palette[6] = 0;
    palette[7] = 255;
  }
  uint64_t indices = 0;
  for (uint32_t i = 0; i < 6; ++i) {
    indices |= uint64_t(block[2 + i]) << (8 * i);
  }
  for (uint32_t texel = 0; texel < 16; ++texel) {
    out[texel] = uint8_t(palette[(indices >> (texel * 3)) & 7]);
  }
}

void DecodeDXT3AlphaBlock(const uint8_t* block, uint8_t out[16]) {
  uint64_t nibbles = 0;
  for (uint32_t i = 0; i < 8; ++i) {
    nibbles |= uint64_t(block[i]) << (8 * i);
  }
  for (uint32_t texel = 0; texel < 16; ++texel) {
    out[texel] = uint8_t(((nibbles >> (texel * 4)) & 0xF) * 17);
  }
}

void DecodeDXTColor(const uint8_t* block, uint8_t colors[4][4]) {
  const uint16_t color_0 = uint16_t(block[0] | (block[1] << 8));
  const uint16_t color_1 = uint16_t(block[2] | (block[3] << 8));
  for (uint32_t i = 0; i < 2; ++i) {
    const uint16_t color = i == 0 ? color_0 : color_1;
    colors[i][0] = Expand5((color >> 11) & 0x1F);
    colors[i][1] = Expand6((color >> 5) & 0x3F);
    colors[i][2] = Expand5(color & 0x1F);
    colors[i][3] = 255;
  }
  if (color_0 > color_1) {
    for (uint32_t i = 0; i < 3; ++i) {
      colors[2][i] = uint8_t((2 * uint32_t(colors[0][i]) + uint32_t(colors[1][i])) / 3);
      colors[3][i] = uint8_t((uint32_t(colors[0][i]) + 2 * uint32_t(colors[1][i])) / 3);
    }
    colors[2][3] = 255;
    colors[3][3] = 255;
  } else {
    for (uint32_t i = 0; i < 3; ++i) {
      colors[2][i] = uint8_t((uint32_t(colors[0][i]) + uint32_t(colors[1][i])) / 2);
      colors[3][i] = 0;
    }
    colors[2][3] = 255;
    colors[3][3] = 0;
  }
}

void DecodeDXT(const uint8_t* source, uint32_t source_pitch, uint32_t width, uint32_t height,
               uint32_t block_bytes, int alpha_mode, uint8_t* destination) {
  const uint32_t blocks_x = (width + 3) / 4;
  const uint32_t blocks_y = (height + 3) / 4;
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* block = source + by * source_pitch + bx * block_bytes;
      uint8_t colors[4][4] = {};
      uint8_t alpha[16];
      for (uint32_t i = 0; i < 16; ++i) {
        alpha[i] = 255;
      }
      const uint8_t* color_block = block;
      if (alpha_mode == 1) {
        // DXT3 - four bits of explicit alpha per texel.
        for (uint32_t i = 0; i < 16; ++i) {
          alpha[i] = Expand4((block[i >> 1] >> ((i & 1) * 4)) & 0xF);
        }
        color_block = block + 8;
      } else if (alpha_mode == 2) {
        // DXT5 - two endpoints plus 3-bit indices.
        const uint8_t alpha_0 = block[0];
        const uint8_t alpha_1 = block[1];
        uint8_t alpha_table[8] = {alpha_0, alpha_1};
        if (alpha_0 > alpha_1) {
          for (uint32_t i = 0; i < 6; ++i) {
            alpha_table[2 + i] =
                uint8_t(((6 - i) * uint32_t(alpha_0) + (1 + i) * uint32_t(alpha_1)) / 7);
          }
        } else {
          for (uint32_t i = 0; i < 4; ++i) {
            alpha_table[2 + i] =
                uint8_t(((4 - i) * uint32_t(alpha_0) + (1 + i) * uint32_t(alpha_1)) / 5);
          }
          alpha_table[6] = 0;
          alpha_table[7] = 255;
        }
        uint64_t bits = 0;
        for (uint32_t i = 0; i < 6; ++i) {
          bits |= uint64_t(block[2 + i]) << (i * 8);
        }
        for (uint32_t i = 0; i < 16; ++i) {
          alpha[i] = alpha_table[(bits >> (i * 3)) & 7];
        }
        color_block = block + 8;
      }
      DecodeDXTColor(color_block, colors);
      for (uint32_t py = 0; py < 4; ++py) {
        const uint32_t y = by * 4 + py;
        if (y >= height) {
          break;
        }
        for (uint32_t px = 0; px < 4; ++px) {
          const uint32_t x = bx * 4 + px;
          if (x >= width) {
            continue;
          }
          const uint32_t texel = py * 4 + px;
          const uint32_t index = (color_block[4 + (texel >> 2)] >> ((texel & 3) * 2)) & 3;
          uint8_t* out = destination + (y * width + x) * 4;
          out[0] = colors[index][0];
          out[1] = colors[index][1];
          out[2] = colors[index][2];
          out[3] = alpha_mode == 0 ? colors[index][3] : alpha[texel];
        }
      }
    }
  }
}

}  // namespace

bool SwitchNativeCommandProcessor::EnsureTextureStaging(VkDeviceSize additional_size) {
  // Room is needed after the current write position, not in total: the first
  // frames decode every texture in one go, which is far more than 64 MiB.
  const VkDeviceSize required = texture_staging_offset_ + additional_size;
  if (texture_staging_buffer_ != VK_NULL_HANDLE && required <= texture_staging_size_) {
    return true;
  }
  constexpr VkDeviceSize kQuantum = VkDeviceSize(64) << 20;
  constexpr VkDeviceSize kStagingCap = VkDeviceSize(256) << 20;
  VkDeviceSize new_size = ((required + kQuantum - 1) / kQuantum) * kQuantum;
  if (new_size < kQuantum) {
    new_size = kQuantum;
  }
  if (new_size > kStagingCap) {
    // Too much for one frame - the remaining textures are created on later
    // frames instead of overflowing the staging buffer.
    return false;
  }

  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  // Uploads already queued for this frame reference the start of the buffer, so
  // their contents have to survive the move.
  std::vector<uint8_t> preserved(static_cast<size_t>(texture_staging_offset_), 0);
  if (!preserved.empty() && texture_staging_mapped_) {
    std::memcpy(preserved.data(), texture_staging_mapped_, preserved.size());
  }
  if (texture_staging_buffer_ != VK_NULL_HANDLE) {
    dfn.vkUnmapMemory(device, texture_staging_memory_);
    dfn.vkDestroyBuffer(device, texture_staging_buffer_, nullptr);
    dfn.vkFreeMemory(device, texture_staging_memory_, nullptr);
    texture_staging_buffer_ = VK_NULL_HANDLE;
    texture_staging_memory_ = VK_NULL_HANDLE;
    texture_staging_mapped_ = nullptr;
    texture_staging_size_ = 0;
  }

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = new_size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (dfn.vkCreateBuffer(device, &buffer_info, nullptr, &texture_staging_buffer_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S6: vkCreateBuffer(texture staging) failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetBufferMemoryRequirements(device, texture_staging_buffer_, &requirements);
  const uint32_t candidates = device_->memory_types().host_coherent &
                              device_->memory_types().host_visible & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S6: no host coherent memory for the texture staging buffer");
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &texture_staging_memory_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S6: vkAllocateMemory(texture staging) failed ({} MiB)",
                 uint64_t(new_size >> 20));
    return false;
  }
  if (dfn.vkBindBufferMemory(device, texture_staging_buffer_, texture_staging_memory_, 0) !=
          VK_SUCCESS ||
      dfn.vkMapMemory(device, texture_staging_memory_, 0, VK_WHOLE_SIZE, 0,
                      &texture_staging_mapped_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S6: texture staging bind/map failed");
    return false;
  }
  if (!preserved.empty()) {
    std::memcpy(texture_staging_mapped_, preserved.data(), preserved.size());
  }
  texture_staging_size_ = new_size;
  REXLOG_INFO("[native] S6: texture staging grown to {} MiB", uint64_t(new_size >> 20));
  return true;
}

VkSampler SwitchNativeCommandProcessor::GetOrCreateSampler(
    const xenos::xe_gpu_texture_fetch_t& fetch) {
  const uint64_t key = uint64_t(fetch.mag_filter) | (uint64_t(fetch.min_filter) << 2) |
                       (uint64_t(fetch.mip_filter) << 4) | (uint64_t(fetch.clamp_x) << 6) |
                       (uint64_t(fetch.clamp_y) << 9) |
                       (g_toggles.nearest_samplers ? (uint64_t(1) << 12) : 0) |
                       (g_toggles.lod_zero ? (uint64_t(1) << 13) : 0);
  auto existing = samplers_.find(key);
  if (existing != samplers_.end()) {
    return existing->second;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  const auto to_address_mode = [](xenos::ClampMode mode) {
    switch (mode) {
      case xenos::ClampMode::kRepeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      case xenos::ClampMode::kMirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      case xenos::ClampMode::kMirrorClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
      default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
  };
  const auto to_filter = [](xenos::TextureFilter filter) {
    return filter == xenos::TextureFilter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  };
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = to_filter(fetch.mag_filter);
  sampler_info.minFilter = to_filter(fetch.min_filter);
  sampler_info.mipmapMode = fetch.mip_filter == xenos::TextureFilter::kLinear
                                ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = to_address_mode(fetch.clamp_x);
  sampler_info.addressModeV = to_address_mode(fetch.clamp_y);
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = VK_LOD_CLAMP_NONE;
  if (g_toggles.nearest_samplers) {
    // Diagnostic variant: one tap, no mip selection, LOD pinned to zero.
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
  } else if (g_toggles.lod_zero) {
    // Diagnostic variant: bilinear taps stay, only the level choice is removed.
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
  }
  VkSampler sampler = VK_NULL_HANDLE;
  if (dfn.vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
    return dummy_sampler_;
  }
  samplers_.emplace(key, sampler);
  return sampler;
}

VkImageView SwitchNativeCommandProcessor::GetOrCreateTexture(
    const xenos::xe_gpu_texture_fetch_t& fetch) {
  if (fetch.type != xenos::FetchConstantType::kTexture) {
    return dummy_image_view_;
  }
  const uint32_t width = fetch.size_2d.width + 1;
  const uint32_t height = (fetch.dimension == xenos::DataDimension::k1D)
                              ? 1
                              : fetch.size_2d.height + 1;
  if (width == 0 || height == 0 || width > 8192 || height > 8192) {
    return dummy_image_view_;
  }
  const uint32_t bytes_per_texel = TextureBytesPerTexel(fetch.format);
  const uint32_t block_bytes = TextureBlockBytes(fetch.format);
  if (!bytes_per_texel && !block_bytes) {
    return dummy_image_view_;
  }
  // This title stores most textures compressed. Decoding them to RGBA8 costs
  // eight times the memory and eight times the bandwidth at sample time, and the
  // device samples BC blocks in optimal tiling (S26), so the DXT formats are
  // uploaded as they are. Formats the hardware has no equivalent for keep the
  // decode path.
  const bool alpha_only = fetch.format == xenos::TextureFormat::k_DXT3A ||
                          fetch.format == xenos::TextureFormat::k_DXT5A;
  VkFormat image_format = alpha_only ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
  bool keep_compressed = false;
  switch (fetch.format) {
    case xenos::TextureFormat::k_DXT1:
    case xenos::TextureFormat::k_DXT1_AS_16_16_16_16:
      image_format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
      keep_compressed = true;
      break;
    case xenos::TextureFormat::k_DXT2_3:
    case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
      image_format = VK_FORMAT_BC2_UNORM_BLOCK;
      keep_compressed = true;
      break;
    case xenos::TextureFormat::k_DXT4_5:
    case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
      image_format = VK_FORMAT_BC3_UNORM_BLOCK;
      keep_compressed = true;
      break;
    default:
      break;
  }
  const uint32_t bc_blocks_x = (width + 3) / 4;
  const uint32_t bc_blocks_y = (height + 3) / 4;
  const VkDeviceSize staging_bytes =
      alpha_only ? VkDeviceSize(width) * height
                 : (keep_compressed ? VkDeviceSize(bc_blocks_x) * bc_blocks_y * block_bytes
                                    : VkDeviceSize(width) * height * 4);
  uint64_t key = uint64_t(fetch.base_address) * 0x9E3779B97F4A7C15ull;
  key ^= (uint64_t(width) << 20) ^ (uint64_t(height) << 34) ^ (uint64_t(fetch.format) << 48) ^
         (uint64_t(fetch.tiled) << 58) ^ (uint64_t(fetch.pitch) << 60);
  auto existing = textures_.find(key);
  if (existing != textures_.end()) {
    return existing->second.view;
  }
  if (!EnsureTextureStaging(staging_bytes + 4096)) {
    if (texture_failures_ < 8) {
      ++texture_failures_;
      REXLOG_INFO("[native] S6: texture {}x{} deferred (staging cap reached)", width, height);
    }
    return dummy_image_view_;
  }

  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  TextureEntry entry;
  entry.width = width;
  entry.height = height;
  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent.width = width;
  image_info.extent.height = height;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.format = image_format;
  if (dfn.vkCreateImage(device, &image_info, nullptr, &entry.image) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S6: vkCreateImage({}x{}) failed", width, height);
    return dummy_image_view_;
  }
  // A texture that cannot be created is remembered as a failure, otherwise every
  // draw retries it and leaks one VkImage per attempt until the driver runs out.
  const auto fail = [&](const char* stage, int32_t result) -> VkImageView {
    if (entry.memory != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, entry.memory, nullptr);
      entry.memory = VK_NULL_HANDLE;
    }
    dfn.vkDestroyImage(device, entry.image, nullptr);
    TextureEntry failed;
    failed.view = dummy_image_view_;
    textures_.emplace(key, failed);
    if (texture_failures_ < 8) {
      ++texture_failures_;
      REXLOG_ERROR("[native] S6: texture {}x{} format={} failed at {} (result={})", width, height,
                   uint32_t(fetch.format), stage, result);
    }
    return dummy_image_view_;
  };

  VkMemoryRequirements requirements{};
  dfn.vkGetImageMemoryRequirements(device, entry.image, &requirements);
  const uint32_t candidates = device_->memory_types().device_local & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    return fail("memory type", 0);
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  VkResult allocate_result = dfn.vkAllocateMemory(device, &allocate_info, nullptr, &entry.memory);
  if (allocate_result != VK_SUCCESS) {
    return fail("allocate", int32_t(allocate_result));
  }
  VkResult bind_result = dfn.vkBindImageMemory(device, entry.image, entry.memory, 0);
  if (bind_result != VK_SUCCESS) {
    return fail("bind", int32_t(bind_result));
  }
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = entry.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = image_format;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (alpha_only) {
    // One channel in memory, the same value in every component on the way out,
    // so a shader reading .a, .r or any swizzle selection sees the mask.
    view_info.components.r = VK_COMPONENT_SWIZZLE_R;
    view_info.components.g = VK_COMPONENT_SWIZZLE_R;
    view_info.components.b = VK_COMPONENT_SWIZZLE_R;
    view_info.components.a = VK_COMPONENT_SWIZZLE_R;
  }
  VkResult view_result = dfn.vkCreateImageView(device, &view_info, nullptr, &entry.view);
  if (view_result != VK_SUCCESS) {
    entry.view = VK_NULL_HANDLE;
    return fail("image view", int32_t(view_result));
  }

  // Decode into the staging buffer.
  const VkDeviceSize staging_offset = (texture_staging_offset_ + 15) & ~VkDeviceSize(15);
  const VkDeviceSize staging_size = staging_bytes;
  if (staging_offset + staging_size > texture_staging_size_) {
    // Not cached as a failure: the next frame starts with an empty staging
    // buffer and gets another chance.
    dfn.vkDestroyImageView(device, entry.view, nullptr);
    dfn.vkFreeMemory(device, entry.memory, nullptr);
    dfn.vkDestroyImage(device, entry.image, nullptr);
    if (texture_failures_ < 8) {
      ++texture_failures_;
      REXLOG_INFO("[native] S6: texture {}x{} deferred to the next frame (staging full)", width,
                  height);
    }
    return dummy_image_view_;
  }
  texture_staging_offset_ = staging_offset + staging_size;
  uint8_t* destination = static_cast<uint8_t*>(texture_staging_mapped_) + staging_offset;
  std::memset(destination, 0, size_t(staging_size));
  const uint8_t* source = memory_->TranslatePhysical<const uint8_t*>(fetch.base_address << 12);
  const uint32_t pitch_pixels = (fetch.pitch & 0x1FF) << 5;
  const uint32_t pitch_bytes_linear =
      rex::align((pitch_pixels >> (block_bytes ? 2 : 0)) * (block_bytes ? block_bytes : bytes_per_texel),
                 256u);
  if (block_bytes) {
    // Compressed: walk the blocks through the same addressing rules.
    const uint32_t bytes_per_block_log2 = block_bytes == 8 ? 3 : 4;
    const uint32_t blocks_x = (width + 3) / 4;
    const uint32_t blocks_y = (height + 3) / 4;
    std::vector<uint8_t> linear(size_t(blocks_x) * blocks_y * block_bytes);
    for (uint32_t by = 0; by < blocks_y; ++by) {
      for (uint32_t bx = 0; bx < blocks_x; ++bx) {
        size_t offset = fetch.tiled
                            ? size_t(texture_util::GetTiledOffset2D(int32_t(bx), int32_t(by),
                                                                   pitch_pixels >>
                                                                       (block_bytes ? 2 : 0),
                                                                   bytes_per_block_log2))
                            : size_t(by) * pitch_bytes_linear + size_t(bx) * block_bytes;
        if (offset + block_bytes > size_t(width) * height * 4 + (1u << 20)) {
          continue;
        }
        // The fetch constant's endianness applies to the stored bytes, not to
        // the decoded colours: a DXT1 block read without the 16-bit swap gives
        // the right block indices (so the shape survives) and completely wrong
        // colours - exactly the rainbow noise this backend showed.
        texture_conversion::CopySwapBlock(
            fetch.endianness, linear.data() + (size_t(by) * blocks_x + bx) * block_bytes,
            source + offset, block_bytes);
      }
    }
    int alpha_mode = 0;
    switch (fetch.format) {
      case xenos::TextureFormat::k_DXT2_3:
      case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
        alpha_mode = 1;
        break;
      case xenos::TextureFormat::k_DXT4_5:
      case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
        alpha_mode = 2;
        break;
      default:
        alpha_mode = block_bytes == 16 ? 2 : 0;
        break;
    }
    if (alpha_only) {
      // One byte per texel: the mask itself, not a colour decode of it.
      for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
          const uint8_t* block = linear.data() + (size_t(by) * blocks_x + bx) * block_bytes;
          uint8_t alpha[16];
          if (fetch.format == xenos::TextureFormat::k_DXT5A) {
            DecodeDXT5AlphaBlock(block, alpha);
          } else {
            DecodeDXT3AlphaBlock(block, alpha);
          }
          for (uint32_t y = 0; y < 4; ++y) {
            const uint32_t py = by * 4 + y;
            if (py >= height) {
              break;
            }
            for (uint32_t x = 0; x < 4; ++x) {
              const uint32_t px = bx * 4 + x;
              if (px >= width) {
                break;
              }
              destination[size_t(py) * width + px] = alpha[y * 4 + x];
            }
          }
        }
      }
    } else if (keep_compressed) {
      // The blocks are already in the exact layout a BC image wants; only the
      // guest's tiling and byte order had to be undone.
      std::memcpy(destination, linear.data(), size_t(linear.size()));
    } else {
      DecodeDXT(linear.data(), blocks_x * block_bytes, width, height, block_bytes, alpha_mode,
                destination);
    }
  } else {
    const uint32_t bytes_per_texel_log2 =
        bytes_per_texel == 1 ? 0 : (bytes_per_texel == 2 ? 1 : 2);
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const size_t offset =
            fetch.tiled ? size_t(texture_util::GetTiledOffset2D(int32_t(x), int32_t(y),
                                                                pitch_pixels, bytes_per_texel_log2))
                        : size_t(y) * pitch_bytes_linear + size_t(x) * bytes_per_texel;
        uint8_t texel_swapped[4] = {};
        if (fetch.endianness != xenos::Endian::kNone && bytes_per_texel >= 2) {
          texture_conversion::CopySwapBlock(fetch.endianness, texel_swapped, source + offset,
                                            bytes_per_texel);
        } else {
          std::memcpy(texel_swapped, source + offset, bytes_per_texel);
        }
        const uint8_t* texel = texel_swapped;
        uint8_t* out = destination + (size_t(y) * width + x) * 4;
        switch (fetch.format) {
          case xenos::TextureFormat::k_8:
          case xenos::TextureFormat::k_8_A:
          case xenos::TextureFormat::k_8_B:
            out[0] = out[1] = out[2] = out[3] = texel[0];
            break;
          case xenos::TextureFormat::k_8_8:
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = 0;
            out[3] = 255;
            break;
          case xenos::TextureFormat::k_5_6_5: {
            const uint16_t value = uint16_t(texel[0] | (texel[1] << 8));
            out[0] = Expand5((value >> 11) & 0x1F);
            out[1] = Expand6((value >> 5) & 0x3F);
            out[2] = Expand5(value & 0x1F);
            out[3] = 255;
            break;
          }
          case xenos::TextureFormat::k_1_5_5_5: {
            const uint16_t value = uint16_t(texel[0] | (texel[1] << 8));
            out[0] = Expand5((value >> 10) & 0x1F);
            out[1] = Expand5((value >> 5) & 0x1F);
            out[2] = Expand5(value & 0x1F);
            out[3] = (value & 0x8000) ? 255 : 0;
            break;
          }
          case xenos::TextureFormat::k_4_4_4_4: {
            const uint16_t value = uint16_t(texel[0] | (texel[1] << 8));
            out[0] = Expand4((value >> 12) & 0xF);
            out[1] = Expand4((value >> 8) & 0xF);
            out[2] = Expand4((value >> 4) & 0xF);
            out[3] = Expand4(value & 0xF);
            break;
          }
          case xenos::TextureFormat::k_2_10_10_10: {
            const uint32_t value = uint32_t(texel[0]) | (uint32_t(texel[1]) << 8) |
                                   (uint32_t(texel[2]) << 16) | (uint32_t(texel[3]) << 24);
            out[0] = uint8_t((value & 0x3FF) >> 2);
            out[1] = uint8_t(((value >> 10) & 0x3FF) >> 2);
            out[2] = uint8_t(((value >> 20) & 0x3FF) >> 2);
            out[3] = uint8_t(((value >> 30) & 0x3) * 85);
            break;
          }
          default:
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = texel[2];
            out[3] = texel[3];
            break;
        }
      }
    }
  }

  // The fetch constant's component swizzle applies after decoding.
  if (!keep_compressed && !alpha_only && fetch.swizzle != 0x688u && fetch.swizzle != 0) {
    const uint32_t swizzle = fetch.swizzle;
    for (size_t i = 0; i < size_t(width) * height; ++i) {
      uint8_t texel[4];
      std::memcpy(texel, destination + i * 4, 4);
      for (uint32_t c = 0; c < 4; ++c) {
        const uint32_t selection = (swizzle >> (c * 3)) & 7;
        uint8_t value = 0;
        if (selection < 4) {
          value = texel[selection];
        } else if (selection == 5) {
          value = 255;
        }
        destination[i * 4 + c] = value;
      }
    }
  }

  // S21: alpha statistics of the decoded texture. Transparent drawing depends on
  // it and the blend state of this title multiplies the source colour by its
  // alpha, so an all-zero alpha makes every blended draw invisible while the
  // same frame looks correct with blending disabled.
  {
    static uint32_t alpha_logged = 0;
    if (!keep_compressed && !alpha_only && alpha_logged < 12) {
      ++alpha_logged;
      const size_t texels = size_t(width) * height;
      uint32_t alpha_min = 255, alpha_max = 0;
      uint64_t alpha_sum = 0;
      for (size_t i = 0; i < texels; ++i) {
        const uint8_t a = destination[i * 4 + 3];
        alpha_min = std::min<uint32_t>(alpha_min, a);
        alpha_max = std::max<uint32_t>(alpha_max, a);
        alpha_sum += a;
      }
      REXLOG_INFO("[native] S21 alpha: {}x{} format={} endian={} tiled={} min={} max={} mean={:.1f}",
                  width, height, uint32_t(fetch.format), uint32_t(fetch.endianness),
                  uint32_t(fetch.tiled), alpha_min, alpha_max,
                  texels ? double(alpha_sum) / double(texels) : 0.0);
    }
  }
  // S22: dump the first few decoded textures as BMP, so the decode can be judged
  // on a real picture instead of on the composed frame.
  {
    static uint32_t textures_dumped = 0;
    if (!keep_compressed && !alpha_only && textures_dumped < 4) {
      char dump_path[80];
      std::snprintf(dump_path, sizeof(dump_path), "sdmc:/switch/timeshift/tex_%02u.bmp",
                    unsigned(textures_dumped));
      ++textures_dumped;
      std::FILE* dump_file = std::fopen(dump_path, "wb");
      if (dump_file) {
        const uint32_t dump_size = width * height * 4;
        uint8_t dump_header[54] = {};
        dump_header[0] = 'B';
        dump_header[1] = 'M';
        const uint32_t file_size = 54 + dump_size;
        std::memcpy(dump_header + 2, &file_size, 4);
        const uint32_t pixel_offset = 54;
        std::memcpy(dump_header + 10, &pixel_offset, 4);
        const uint32_t dib_size = 40;
        std::memcpy(dump_header + 14, &dib_size, 4);
        const int32_t dump_w = int32_t(width);
        const int32_t dump_h = -int32_t(height);
        std::memcpy(dump_header + 18, &dump_w, 4);
        std::memcpy(dump_header + 22, &dump_h, 4);
        const uint16_t planes = 1, bits = 32;
        std::memcpy(dump_header + 26, &planes, 2);
        std::memcpy(dump_header + 28, &bits, 2);
        std::memcpy(dump_header + 34, &dump_size, 4);
        std::fwrite(dump_header, 1, sizeof(dump_header), dump_file);
        std::vector<uint32_t> dump_row(width);
        for (uint32_t y = 0; y < height; ++y) {
          for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* texel = destination + (size_t(y) * width + x) * 4;
            dump_row[x] = (UINT32_C(0xFF) << 24) | (uint32_t(texel[2]) << 16) |
                          (uint32_t(texel[1]) << 8) | uint32_t(texel[0]);
          }
          std::fwrite(dump_row.data(), 4, width, dump_file);
        }
        std::fclose(dump_file);
        REXLOG_INFO("[native] S22 dump: {} {}x{} format={}", dump_path, width, height,
                    uint32_t(fetch.format));
      }
    }
  }
  TextureUpload upload;
  upload.image = entry.image;
  upload.width = width;
  upload.height = height;
  upload.staging_offset = staging_offset;
  texture_uploads_.push_back(upload);

  textures_.emplace(key, entry);
  ++textures_created_;
  // Every texture created, with its guest address: comparing those addresses
  // against the colour surface bases of the same frame is how "the title renders
  // into a surface and samples it" is proven or refuted from the log alone.
  if (textures_created_ <= 60) {
    REXLOG_INFO(
        "[native] S6: texture {}x{} format={} tiled={} pitch={} addr={:08X} -> cached ({})",
        width, height, uint32_t(fetch.format), uint32_t(fetch.tiled), pitch_pixels,
        uint32_t(fetch.base_address) << 12, keep_compressed ? "bc" : (alpha_only ? "mask" : "rgba"));
  }
  return textures_.find(key)->second.view;
}

VkShaderModule SwitchNativeCommandProcessor::GetOrCreateModule(Shader* shader, bool is_vertex,
                                                            bool rectangle_list) {
  if (!shader || !translator_) {
    return VK_NULL_HANDLE;
  }
  const RegisterFile& regs = *register_file_;
  static string::StringBuffer s_disasm_buffer;
  if (!shader->is_ucode_analyzed()) {
    shader->AnalyzeUcode(s_disasm_buffer);
  }
  const uint64_t default_modification =
      is_vertex ? translator_->GetDefaultVertexShaderModification(
                      shader->GetDynamicAddressableRegisterCount(
                          regs.Get<reg::SQ_PROGRAM_CNTL>().vs_num_reg),
                      rectangle_list
                          ? Shader::HostVertexShaderType::kRectangleListAsTriangleStrip
                          : Shader::HostVertexShaderType::kVertex)
                : translator_->GetDefaultPixelShaderModification(
                      shader->GetDynamicAddressableRegisterCount(
                          regs.Get<reg::SQ_PROGRAM_CNTL>().ps_num_reg));
  SpirvShaderTranslator::Modification modification(default_modification);
  if (is_vertex) {
    modification.vertex.interpolator_mask = 0xFFFFu;
    modification.vertex.tessellation_mode = 0;
  } else {
    modification.pixel.interpolator_mask = 0xFFFFu;
    modification.pixel.param_gen_enable = 0;
  }
  bool is_new = false;
  auto* translation = static_cast<vulkan::VulkanShader::VulkanTranslation*>(
      shader->GetOrCreateTranslation(modification.value, &is_new));
  if (!translation) {
    return VK_NULL_HANDLE;
  }
  if (is_new && !translator_->TranslateAnalyzedShader(*translation)) {
    ++failed_shaders_;
    return VK_NULL_HANDLE;
  }
  VkShaderModule module = translation->GetOrCreateShaderModule();
  // Dump the first translated binaries to the SD card so the SPIR-V can be
  // validated offline with spirv-val when a pipeline creation misbehaves.
  static int dumped_shaders = 0;
  // The first dumps are always the loading-screen shaders. The scene shaders are
  // only created once real geometry appears, so they get their own budget.
  const bool is_scene_shader = last_draw_vertex_count_ >= 2000;
  static int dumped_scene_shaders = 0;
  const bool want_dump =
      module != VK_NULL_HANDLE && translation->is_valid() &&
      (dumped_shaders < 8 || (is_scene_shader && dumped_scene_shaders < 6));
  if (want_dump) {
    const std::vector<uint8_t>& binary = translation->translated_binary();
    mkdir("sdmc:/switch/timeshift/spv", 0755);
    char path[128];
    std::snprintf(path, sizeof(path), "sdmc:/switch/timeshift/spv/%s_%016llX.spv",
                  is_vertex ? "vs" : "ps",
                  static_cast<unsigned long long>(
                      static_cast<vulkan::VulkanShader*>(shader)->ucode_data_hash()));
    if (FILE* dump = std::fopen(path, "wb")) {
      std::fwrite(binary.data(), 1, binary.size(), dump);
      std::fclose(dump);
      if (is_scene_shader) {
        ++dumped_scene_shaders;
      } else {
        ++dumped_shaders;
      }
      REXLOG_INFO("[native] S16: dumped {} bytes of SPIR-V to {} (scene={})", binary.size(), path,
                  is_scene_shader ? 1 : 0);
    }
  }
  return module;
}

namespace {

// Guest xenos::CompareFunction -> Vulkan compare op, in enum order.
VkCompareOp CompareFunctionToVk(xenos::CompareFunction function) {
  switch (function) {
    case xenos::CompareFunction::kNever:
      return VK_COMPARE_OP_NEVER;
    case xenos::CompareFunction::kLess:
      return VK_COMPARE_OP_LESS;
    case xenos::CompareFunction::kEqual:
      return VK_COMPARE_OP_EQUAL;
    case xenos::CompareFunction::kLessEqual:
      return VK_COMPARE_OP_LESS_OR_EQUAL;
    case xenos::CompareFunction::kGreater:
      return VK_COMPARE_OP_GREATER;
    case xenos::CompareFunction::kNotEqual:
      return VK_COMPARE_OP_NOT_EQUAL;
    case xenos::CompareFunction::kGreaterEqual:
      return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case xenos::CompareFunction::kAlways:
    default:
      return VK_COMPARE_OP_ALWAYS;
  }
}

}  // namespace

const SwitchNativeCommandProcessor::PipelineEntry*
SwitchNativeCommandProcessor::GetOrCreatePipeline(const PipelineStateKey& key) {
  auto existing = pipelines_.find(key);
  if (existing != pipelines_.end()) {
    ++existing->second.uses;
    return &existing->second;
  }
  if (pipeline_layout_ == VK_NULL_HANDLE) {
    return nullptr;
  }
  if (marker_vertex_module_ == VK_NULL_HANDLE) {
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = sizeof(kSelfTestVertexSpirv);
    module_info.pCode = kSelfTestVertexSpirv;
    if (device_->functions().vkCreateShaderModule(device_->device(), &module_info, nullptr,
                                                  &marker_vertex_module_) != VK_SUCCESS) {
      marker_vertex_module_ = VK_NULL_HANDLE;
    }
  }
  // A rectangle list is three vertices per rectangle on the guest, expanded to a
  // four-vertex triangle strip by the vertex shader: without it every rectangle
  // loses its second triangle, which is exactly the diagonal cut in the picture.
  const bool rectangle_list =
      key.primitive_type == uint32_t(xenos::PrimitiveType::kRectangleList);
  VkShaderModule vertex_module =
      GetOrCreateModule(active_vertex_shader(), true, rectangle_list);
  if ((REXCVAR_QUERY(bool, native_marker_vs) || g_toggles.marker_vertex_shader) &&
      marker_vertex_module_ != VK_NULL_HANDLE) {
    vertex_module = marker_vertex_module_;
  }
  VkShaderModule pixel_module = GetOrCreateModule(active_pixel_shader(), false);
  if (vertex_module == VK_NULL_HANDLE) {
    REXLOG_ERROR("[native] S4: no vertex shader module for a draw");
    return nullptr;
  }

  // The set 2 / set 3 layouts must match the bindings the translated shaders
  // declare, so they are derived from the shaders themselves here.
  auto* vertex_spirv = static_cast<SpirvShader*>(active_vertex_shader());
  auto* pixel_spirv = static_cast<SpirvShader*>(active_pixel_shader());
  const uint32_t texture_count_vertex =
      vertex_spirv ? uint32_t(vertex_spirv->GetTextureBindingsAfterTranslation().size()) : 0;
  const uint32_t sampler_count_vertex =
      vertex_spirv ? uint32_t(vertex_spirv->GetSamplerBindingsAfterTranslation().size()) : 0;
  const uint32_t texture_count_pixel =
      pixel_spirv ? uint32_t(pixel_spirv->GetTextureBindingsAfterTranslation().size()) : 0;
  const uint32_t sampler_count_pixel =
      pixel_spirv ? uint32_t(pixel_spirv->GetSamplerBindingsAfterTranslation().size()) : 0;
  PipelineLayoutEntry& layout_entry =
      GetOrCreatePipelineLayout(texture_count_vertex, sampler_count_vertex, texture_count_pixel,
                                sampler_count_pixel);

  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();

  VkPipelineShaderStageCreateInfo stages[2] = {};
  uint32_t stage_count = 0;
  stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[stage_count].module = vertex_module;
  stages[stage_count].pName = "main";
  ++stage_count;
  if (pixel_module != VK_NULL_HANDLE) {
    stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[stage_count].module = pixel_module;
    stages[stage_count].pName = "main";
    ++stage_count;
  }

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  switch (xenos::PrimitiveType(key.primitive_type)) {
    case xenos::PrimitiveType::kLineList:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      break;
    case xenos::PrimitiveType::kLineStrip:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      break;
    case xenos::PrimitiveType::kTriangleFan:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
      break;
    case xenos::PrimitiveType::kPointList:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      break;
    case xenos::PrimitiveType::kRectangleList:
      // Four host vertices per guest rectangle, expanded into a strip by the
      // vertex shader; a quad list is not expanded yet.
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      break;
    default:
      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      break;
  }

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // S7: culling is deliberately off. Xenos clip space has +Y up and Vulkan's
  // has +Y down, so with the current NDC transform guest triangles reach the
  // rasteriser with the opposite winding and "cull back faces" discards the
  // whole scene - the frame is "empty", not "mis-drawn". Once geometry is on
  // screen, the winding is fixed together with the Y flip and culling is
  // re-enabled from PA_SU_SC_MODE_CNTL.
  reg::PA_SU_SC_MODE_CNTL sc_mode{};
  sc_mode.value = key.pa_su_sc_mode;
  (void)sc_mode;
  VkPipelineRasterizationStateCreateInfo rasterization{};
  rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_NONE;
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Blending. Xenos has no blend-enable bit: blending is on whenever the factors
  // are not the identity (1 * source + 0 * destination), the same rule the shared
  // Vulkan backend applies. Without this every transparent layer - text,
  // particles, whole UI screens - paints opaque and covers what is under it,
  // which is exactly how a correct frame turns into flat rectangles.
  reg::RB_BLENDCONTROL blend_control{};
  blend_control.value = key.blend_control[0];
  const auto to_vk_blend_factor = [](xenos::BlendFactor factor) -> VkBlendFactor {
    switch (factor) {
      case xenos::BlendFactor::kZero:
        return VK_BLEND_FACTOR_ZERO;
      case xenos::BlendFactor::kOne:
        return VK_BLEND_FACTOR_ONE;
      case xenos::BlendFactor::kSrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
      case xenos::BlendFactor::kOneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
      case xenos::BlendFactor::kSrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
      case xenos::BlendFactor::kOneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      case xenos::BlendFactor::kDstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
      case xenos::BlendFactor::kOneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
      case xenos::BlendFactor::kDstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
      case xenos::BlendFactor::kOneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
      case xenos::BlendFactor::kConstantColor:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
      case xenos::BlendFactor::kOneMinusConstantColor:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
      case xenos::BlendFactor::kConstantAlpha:
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
      case xenos::BlendFactor::kOneMinusConstantAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
      case xenos::BlendFactor::kSrcAlphaSaturate:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
      default:
        return VK_BLEND_FACTOR_ONE;
    }
  };
  const auto to_vk_blend_op = [](xenos::BlendOp op) -> VkBlendOp {
    switch (op) {
      case xenos::BlendOp::kSubtract:
        return VK_BLEND_OP_SUBTRACT;
      case xenos::BlendOp::kMin:
        return VK_BLEND_OP_MIN;
      case xenos::BlendOp::kMax:
        return VK_BLEND_OP_MAX;
      case xenos::BlendOp::kRevSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
      default:
        return VK_BLEND_OP_ADD;
    }
  };
  const bool blend_identity =
      blend_control.color_srcblend == xenos::BlendFactor::kOne &&
      blend_control.color_destblend == xenos::BlendFactor::kZero &&
      blend_control.color_comb_fcn == xenos::BlendOp::kAdd &&
      blend_control.alpha_srcblend == xenos::BlendFactor::kOne &&
      blend_control.alpha_destblend == xenos::BlendFactor::kZero &&
      blend_control.alpha_comb_fcn == xenos::BlendOp::kAdd;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.blendEnable =
      (blend_identity || (key.debug_bits & 8u)) ? VK_FALSE : VK_TRUE;
  blend_attachment.srcColorBlendFactor = to_vk_blend_factor(blend_control.color_srcblend);
  blend_attachment.dstColorBlendFactor = to_vk_blend_factor(blend_control.color_destblend);
  blend_attachment.colorBlendOp = to_vk_blend_op(blend_control.color_comb_fcn);
  blend_attachment.srcAlphaBlendFactor = to_vk_blend_factor(blend_control.alpha_srcblend);
  blend_attachment.dstAlphaBlendFactor = to_vk_blend_factor(blend_control.alpha_destblend);
  blend_attachment.alphaBlendOp = to_vk_blend_op(blend_control.alpha_comb_fcn);
  // The guest's colour write mask for render target 0, so a depth/alpha-only
  // pass cannot paint colour by accident.
  const uint32_t color_write_bits = key.color_mask & 0xFu;
  blend_attachment.colorWriteMask =
      ((color_write_bits & 1u) ? VK_COLOR_COMPONENT_R_BIT : 0) |
      ((color_write_bits & 2u) ? VK_COLOR_COMPONENT_G_BIT : 0) |
      ((color_write_bits & 4u) ? VK_COLOR_COMPONENT_B_BIT : 0) |
      ((color_write_bits & 8u) ? VK_COLOR_COMPONENT_A_BIT : 0);
  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  // Depth: mirror the guest's own RB_DEPTHCONTROL so z-testing and z-writing
  // happen where the game asks for them.
  reg::RB_DEPTHCONTROL depth_control{};
  depth_control.value = key.depth_control;
  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  const bool depth_off = (key.debug_bits & 16u) != 0;
  depth_stencil.depthTestEnable =
      (!depth_off && depth_control.z_enable) ? VK_TRUE : VK_FALSE;
  depth_stencil.depthWriteEnable =
      (!depth_off && depth_control.z_write_enable) ? VK_TRUE : VK_FALSE;
  depth_stencil.depthCompareOp = CompareFunctionToVk(depth_control.zfunc);
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;
  depth_stencil.minDepthBounds = 0.0f;
  depth_stencil.maxDepthBounds = 1.0f;

  VkDynamicState dynamic_states[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_BLEND_CONSTANTS};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = uint32_t(std::size(dynamic_states));
  dynamic_state.pDynamicStates = dynamic_states;

  const VkFormat color_format = ui::vulkan::VulkanPresenter::kGuestOutputFormat;
  const VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &color_format;
  rendering_info.depthAttachmentFormat = depth_format;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &rendering_info;
  pipeline_info.stageCount = stage_count;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterization;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = layout_entry.layout;

  // Second entry in the same call: identical state, flat pixel shader. One extra
  // pipeline object per state key, and the only way to answer "vertex path or
  // pixel path" without a rebuild.
  const bool use_probe = REXCVAR_QUERY(bool, native_probe_ps) || g_toggles.probe_pixel_shader;
  VkShaderModule& diagnostic_pixel_module = use_probe ? probe_pixel_module_ : flat_pixel_module_;
  if (diagnostic_pixel_module == VK_NULL_HANDLE) {
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    if (use_probe) {
      module_info.codeSize = sizeof(kProbePixelSpirv);
      module_info.pCode = kProbePixelSpirv;
    } else {
      module_info.codeSize = sizeof(kFlatPixelSpirv);
      module_info.pCode = kFlatPixelSpirv;
    }
    if (dfn.vkCreateShaderModule(device, &module_info, nullptr, &diagnostic_pixel_module) !=
        VK_SUCCESS) {
      diagnostic_pixel_module = VK_NULL_HANDLE;
    }
  }
  const VkShaderModule flat_pixel_module_ = diagnostic_pixel_module;
  VkGraphicsPipelineCreateInfo pipeline_infos[2] = {pipeline_info, pipeline_info};
  VkPipelineShaderStageCreateInfo flat_stages[2] = {};
  uint32_t create_count = 1;
  if (flat_pixel_module_ != VK_NULL_HANDLE && stage_count == 2) {
    flat_stages[0] = stages[0];
    flat_stages[1] = stages[1];
    flat_stages[1].module = flat_pixel_module_;
    pipeline_infos[1].pStages = flat_stages;
    create_count = 2;
  }
  VkPipeline created[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

  if (pipelines_.size() < 3) {
    REXLOG_INFO("[native] S14: create_count={} flat_module={} marker_vs={} stage_count={} "
                "flat_ps={}",
                create_count, static_cast<const void*>(flat_pixel_module_),
                REXCVAR_QUERY(bool, native_marker_vs) ? 1 : 0, stage_count,
                REXCVAR_QUERY(bool, native_flat_ps) ? 1 : 0);
    REXLOG_INFO("[native] S15: probe_ps={} (R=constants zero G=fetch dword zero B=data zero)",
                REXCVAR_QUERY(bool, native_probe_ps) ? 1 : 0);
  }
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (pipelines_.size() < 8) {
    REXLOG_INFO("[native] S4: creating pipeline for key #{} (vs={:016X} ps={:016X} prim={})",
                pipelines_.size() + 1, key.vertex_shader_hash, key.pixel_shader_hash,
                key.primitive_type);
  }
  const VkResult result = dfn.vkCreateGraphicsPipelines(device, pipeline_cache_, create_count,
                                                        pipeline_infos, nullptr, created);
  pipeline = created[0];
  if (pipelines_.size() < 8) {
    REXLOG_INFO("[native] S4: vkCreateGraphicsPipelines returned {} (handle={})",
                int32_t(result), static_cast<const void*>(pipeline));
  }
  if (result != VK_SUCCESS) {
    ++pipeline_failures_;
    if (pipeline_failures_ < 8) {
      REXLOG_ERROR("[native] S4: vkCreateGraphicsPipelines failed (result={}, vs={:016X})",
                   int32_t(result), key.vertex_shader_hash);
    }
    return nullptr;
  }
  PipelineEntry& entry = pipelines_[key];
  entry.pipeline = pipeline;
  entry.pipeline_flat = created[1];
  entry.layout = layout_entry.layout;
  entry.textures_vertex = layout_entry.textures_vertex;
  entry.textures_pixel = layout_entry.textures_pixel;
  entry.textures_layout_vertex = GetOrCreateTextureLayout(
      true, texture_count_vertex, sampler_count_vertex).layout;
  entry.textures_layout_pixel = GetOrCreateTextureLayout(
      false, texture_count_pixel, sampler_count_pixel).layout;
  entry.uses = 1;
  if (pipelines_.size() < 8) {
    REXLOG_INFO("[native] S4: created pipeline #{} (vs={:016X} ps={:016X} prim={})",
                pipelines_.size(), key.vertex_shader_hash, key.pixel_shader_hash,
                key.primitive_type);
  }
  return &pipelines_.find(key)->second;
}


bool SwitchNativeCommandProcessor::EnsureDepthTarget(uint32_t width, uint32_t height) {
  if (depth_view_ != VK_NULL_HANDLE && depth_width_ == width && depth_height_ == height) {
    return true;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  if (depth_view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, depth_view_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
  }
  if (depth_image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, depth_image_, nullptr);
    depth_image_ = VK_NULL_HANDLE;
  }
  if (depth_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, depth_memory_, nullptr);
    depth_memory_ = VK_NULL_HANDLE;
  }
  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_D32_SFLOAT;
  image_info.extent.width = width;
  image_info.extent.height = height;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dfn.vkCreateImage(device, &image_info, nullptr, &depth_image_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S19 depth: vkCreateImage failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetImageMemoryRequirements(device, depth_image_, &requirements);
  const uint32_t candidates = device_->memory_types().device_local & requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S19 depth: no device local memory type");
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &depth_memory_) != VK_SUCCESS ||
      dfn.vkBindImageMemory(device, depth_image_, depth_memory_, 0) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S19 depth: allocation failed");
    return false;
  }
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = depth_image_;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_D32_SFLOAT;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &view_info, nullptr, &depth_view_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S19 depth: vkCreateImageView failed");
    depth_view_ = VK_NULL_HANDLE;
    return false;
  }
  depth_width_ = width;
  depth_height_ = height;
  REXLOG_INFO("[native] S19 depth: {}x{} D32 target ready", width, height);
  return true;
}

bool SwitchNativeCommandProcessor::EnsureReadback(uint32_t width, uint32_t height) {
  const VkDeviceSize size = VkDeviceSize(width) * VkDeviceSize(height) * 4;
  if (readback_mapped_ && readback_width_ == width && readback_height_ == height) {
    return true;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  if (readback_mapped_) {
    dfn.vkUnmapMemory(device, readback_memory_);
    readback_mapped_ = nullptr;
  }
  if (readback_buffer_ != VK_NULL_HANDLE) {
    dfn.vkDestroyBuffer(device, readback_buffer_, nullptr);
    readback_buffer_ = VK_NULL_HANDLE;
  }
  if (readback_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, readback_memory_, nullptr);
    readback_memory_ = VK_NULL_HANDLE;
  }
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (dfn.vkCreateBuffer(device, &buffer_info, nullptr, &readback_buffer_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S19 shot: vkCreateBuffer failed");
    return false;
  }
  VkMemoryRequirements requirements{};
  dfn.vkGetBufferMemoryRequirements(device, readback_buffer_, &requirements);
  const uint32_t candidates =
      (device_->memory_types().host_visible | device_->memory_types().host_coherent) &
      requirements.memoryTypeBits;
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < 32; ++i) {
    if (candidates & (uint32_t(1) << i)) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    REXLOG_ERROR("[native] S19 shot: no host visible memory type");
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(device, &allocate_info, nullptr, &readback_memory_) != VK_SUCCESS ||
      dfn.vkBindBufferMemory(device, readback_buffer_, readback_memory_, 0) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S19 shot: allocation failed");
    return false;
  }
  if (dfn.vkMapMemory(device, readback_memory_, 0, VK_WHOLE_SIZE, 0, &readback_mapped_) !=
      VK_SUCCESS) {
    readback_mapped_ = nullptr;
    REXLOG_ERROR("[native] S19 shot: vkMapMemory failed");
    return false;
  }
  readback_size_ = size;
  readback_width_ = width;
  readback_height_ = height;
  return true;
}

void SwitchNativeCommandProcessor::WriteScreenshotBmp() {
  if (!readback_mapped_ || !readback_width_ || !readback_height_) {
    return;
  }
  const uint32_t width = readback_width_;
  const uint32_t height = readback_height_;
  char path[80];
  if (next_shot_live_) {
    std::snprintf(path, sizeof(path), "sdmc:/switch/timeshift/shot_live.bmp");
  } else {
    std::snprintf(path, sizeof(path), "sdmc:/switch/timeshift/shot_%02u.bmp",
                  unsigned(screenshots_written_ % 100));
  }
  std::FILE* file = std::fopen(path, "wb");
  if (!file) {
    REXLOG_ERROR("[native] S19 shot: cannot open {}", path);
    return;
  }
  const uint32_t image_size = width * height * 4;
  uint8_t header[54] = {};
  header[0] = 'B';
  header[1] = 'M';
  const uint32_t file_size = 54 + image_size;
  std::memcpy(header + 2, &file_size, 4);
  const uint32_t pixel_offset = 54;
  std::memcpy(header + 10, &pixel_offset, 4);
  const uint32_t dib_size = 40;
  std::memcpy(header + 14, &dib_size, 4);
  const int32_t width_i = int32_t(width);
  std::memcpy(header + 18, &width_i, 4);
  const int32_t height_i = -int32_t(height);  // Negative: top-down rows.
  std::memcpy(header + 22, &height_i, 4);
  const uint16_t planes = 1;
  std::memcpy(header + 26, &planes, 2);
  const uint16_t bits = 32;
  std::memcpy(header + 28, &bits, 2);
  std::memcpy(header + 34, &image_size, 4);
  std::fwrite(header, 1, sizeof(header), file);
  // The guest output image is A2B10G10R10_UNORM_PACK32; BMP here is BGRA.
  const uint32_t* source = static_cast<const uint32_t*>(readback_mapped_);
  std::vector<uint32_t> row(width);
  for (uint32_t y = 0; y < height; ++y) {
    const uint32_t* source_row = source + VkDeviceSize(y) * width;
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t pixel = source_row[x];
      const uint32_t r = (pixel & 0x3FFu) >> 2;
      const uint32_t g = ((pixel >> 10) & 0x3FFu) >> 2;
      const uint32_t b = ((pixel >> 20) & 0x3FFu) >> 2;
      row[x] = (UINT32_C(0xFF) << 24) | (b << 16) | (g << 8) | r;
    }
    std::fwrite(row.data(), 4, width, file);
  }
  std::fclose(file);
  ++screenshots_written_;
  REXLOG_INFO("[native] S19 shot: {} written ({}x{})", path, width, height);
}

// S22: per-draw GPU timestamps. Every indirect measurement of the heavy menu
// frame could be explained two different ways, so the GPU is asked directly how
// long each draw took. Two timestamp queries per replayed draw, read back one
// frame later once the fence has signalled, so the probe costs no stalls.
bool SwitchNativeCommandProcessor::InitializeDrawTimers(uint32_t draws) {
  if (draw_time_tried_ && !draw_time_ok_) {
    return false;
  }
  if (draw_time_ok_ && draw_time_capacity_ >= draws * 2) {
    return true;
  }
  draw_time_tried_ = true;
  if (!device_) {
    return false;
  }
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  const ui::vulkan::VulkanInstance* instance = device_->vulkan_instance();
  if (!instance) {
    return false;
  }
  const ui::vulkan::VulkanInstance::Functions& ifn = instance->functions();
  const VkDevice device = device_->device();
  if (!fp_cmd_reset_query_pool_) {
    fp_cmd_reset_query_pool_ = reinterpret_cast<PFN_vkCmdResetQueryPool>(
        ifn.vkGetDeviceProcAddr(device, "vkCmdResetQueryPool"));
    fp_cmd_write_timestamp_ = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        ifn.vkGetDeviceProcAddr(device, "vkCmdWriteTimestamp"));
    fp_get_query_pool_results_ = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        ifn.vkGetDeviceProcAddr(device, "vkGetQueryPoolResults"));
  }
  if (!fp_cmd_reset_query_pool_ || !fp_cmd_write_timestamp_ || !fp_get_query_pool_results_) {
    REXLOG_ERROR("[native] S22: timestamp entry points missing");
    return false;
  }
  VkPhysicalDeviceProperties raw{};
  ifn.vkGetPhysicalDeviceProperties(device_->physical_device(), &raw);
  draw_time_period_ = double(raw.limits.timestampPeriod);
  if (!raw.limits.timestampComputeAndGraphics || draw_time_period_ <= 0.0) {
    REXLOG_ERROR("[native] S22: timestamps unsupported (graphics={} period={})",
                 raw.limits.timestampComputeAndGraphics ? 1 : 0, draw_time_period_);
    return false;
  }
  if (draw_time_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyQueryPool(device, draw_time_pool_, nullptr);
    draw_time_pool_ = VK_NULL_HANDLE;
  }
  const uint32_t capacity = draws > 0 ? draws * 2 : 2048;
  VkQueryPoolCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  create_info.queryCount = capacity;
  if (dfn.vkCreateQueryPool(device, &create_info, nullptr, &draw_time_pool_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S22: query pool creation failed");
    draw_time_pool_ = VK_NULL_HANDLE;
    return false;
  }
  draw_time_capacity_ = capacity;
  draw_time_host_.assign(capacity, 0);
  draw_time_ms_.assign(capacity / 2, 0.0);
  draw_time_key_.assign(capacity / 2, 0);
  draw_time_ok_ = true;
  REXLOG_INFO("[native] S22: per-draw timestamps ready, queries={} period={} ns", capacity,
              draw_time_period_);
  return true;
}

void SwitchNativeCommandProcessor::CollectDrawTimers() {
  if (!draw_time_ok_ || draw_time_pool_ == VK_NULL_HANDLE || draw_time_recorded_ == 0) {
    return;
  }
  const uint32_t count = draw_time_recorded_;
  draw_time_recorded_ = 0;
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  std::memset(draw_time_host_.data(), 0, sizeof(uint64_t) * count);
  if (fp_get_query_pool_results_(device_->device(), draw_time_pool_, 0, count,
                                 sizeof(uint64_t) * count, draw_time_host_.data(),
                                 sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
    return;
  }
  for (uint32_t i = 0; i + 1 < count; i += 2) {
    const uint64_t begin = draw_time_host_[i];
    const uint64_t end = draw_time_host_[i + 1];
    if (end > begin) {
      draw_time_ms_[i / 2] += double(end - begin) * draw_time_period_ / 1e6;
    }
  }
  ++draw_time_frames_;
}

void SwitchNativeCommandProcessor::ReportDrawTimers() {
  if (!draw_time_ok_ || draw_time_frames_ == 0) {
    return;
  }
  const uint32_t count = draw_time_capacity_ / 2;
  std::vector<uint32_t> order(count);
  for (uint32_t i = 0; i < count; ++i) {
    order[i] = i;
  }
  const uint32_t top = count < 10 ? count : 10;
  std::partial_sort(order.begin(), order.begin() + top, order.end(),
                    [this](uint32_t a, uint32_t b) { return draw_time_ms_[a] > draw_time_ms_[b]; });
  double total = 0.0;
  for (uint32_t i = 0; i < count; ++i) {
    total += draw_time_ms_[i];
  }
  REXLOG_INFO("[native] S22: per-draw GPU time over {} frames, summed {:.1f} ms (avg {:.1f} ms/frame)",
              draw_time_frames_, total, total / double(draw_time_frames_));
  for (uint32_t i = 0; i < top; ++i) {
    const uint32_t index = order[i];
    if (draw_time_ms_[index] <= 0.0) {
      continue;
    }
    REXLOG_INFO("[native] S22:   draw {} avg {:.3f} ms/frame key={:016X}",
                index, draw_time_ms_[index] / double(draw_time_frames_), draw_time_key_[index]);
  }
}

bool SwitchNativeCommandProcessor::RenderFrame(VkImage image, VkImageView view,
                                               bool image_ever_written, uint32_t width,
                                               uint32_t height) {
  if (!device_) {
    return false;
  }
  // Take the queued draws under the lock: the guest's GPU worker thread keeps
  // appending while this runs on the UI thread.
  std::vector<FrameDraw> draws;
  {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    draws.swap(frame_draws_);
    // One copy of a page per frame is enough, and the guest rewrites vertex and
    // index data constantly, so the "already copied" marks start empty here.
    // Never resetting this was a second, independent reason for stale geometry.
    if (!copied_pages_.empty()) {
      std::memset(copied_pages_.data(), 0, copied_pages_.size() * sizeof(uint64_t));
    }
    pages_copied_this_frame_ = 0;
  }
  if (draws.empty()) {
    return false;
  }
  const VkDevice device = device_->device();
  const ui::vulkan::VulkanDevice::Functions& dfn = device_->functions();
  if (frames_rendered_ == 0) {
    for (unsigned i = 0; i < 20; ++i) {
      char stale[80];
      std::snprintf(stale, sizeof(stale), "sdmc:/switch/timeshift/shot_%02u.bmp", i);
      std::remove(stale);
    }
    std::remove("sdmc:/switch/timeshift/shot_live.bmp");
  }
  if (frames_rendered_ < 4) {
    REXLOG_INFO("[native] S4: rendering frame {} with {} draws", frames_rendered_ + 1,
                draws.size());
  }

  const uint64_t frame_start_ns = rex::graphics::perf::NowNs();
  uint64_t fence_wait_ns = 0;
  if (fence_submitted_) {
    const uint64_t wait_start_ns = rex::graphics::perf::NowNs();
    if (dfn.vkWaitForFences(device, 1, &clear_fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
      REXLOG_ERROR("[native] S4: waiting for the previous frame failed");
      return false;
    }
    fence_wait_ns = rex::graphics::perf::NowNs() - wait_start_ns;
    fence_submitted_ = false;
    // The copy recorded last frame is now complete, so the mapped buffer holds
    // that frame's pixels and can be written out from the CPU.
    if (shot_pending_) {
      WriteScreenshotBmp();
      shot_pending_ = false;
    }
  }
  dfn.vkResetFences(device, 1, &clear_fence_);

  // The submission that just completed carried last frame's timestamp queries.
  CollectDrawTimers();
  if (g_toggles.ab_mode == 16 && draw_time_frames_ >= 5) {
    ReportDrawTimers();
    std::fill(draw_time_ms_.begin(), draw_time_ms_.end(), 0.0);
    draw_time_frames_ = 0;
  }

  // The previous submission is complete, so the mapped window can be written and
  // the constant ring can be rewound.
  UploadGuestMemoryChunk(VkDeviceSize(8) << 20);
  constants_ring_offset_ = 0;
  if (texture_pool_ != VK_NULL_HANDLE) {
    dfn.vkResetDescriptorPool(device, texture_pool_, 0);
  }

  const bool time_draws = g_toggles.ab_mode == 16;
  const bool timers_armed = time_draws && InitializeDrawTimers(uint32_t(draws.size()));

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (dfn.vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
    return false;
  }
  if (timers_armed) {
    fp_cmd_reset_query_pool_(command_buffer_, draw_time_pool_, 0, draw_time_capacity_);
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.oldLayout = image_ever_written ? ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout
                                         : VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcAccessMask = image_ever_written
                              ? ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask
                              : 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dfn.vkCmdPipelineBarrier(command_buffer_,
                           image_ever_written
                               ? ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask
                               : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &barrier);

  // Depth: cleared every frame, so the previous contents are discarded and no
  // load op is needed. This is what makes the scene occlude itself instead of
  // being painted back to front.
  const bool depth_ready = EnsureDepthTarget(width, height);
  if (depth_ready) {
    VkImageMemoryBarrier depth_barrier{};
    depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_barrier.image = depth_image_;
    depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_barrier.subresourceRange.levelCount = 1;
    depth_barrier.subresourceRange.layerCount = 1;
    depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_barrier.srcAccessMask = 0;
    depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &depth_barrier);
  }
  // One screenshot every 30 frames, eight in total: enough to see the boot,
  // the menu and the first seconds of a level without filling the SD card.
  RefreshRuntimeToggles(frames_rendered_);
  // S20 sweep: advance the replay window once per automatic screenshot, so a
  // single run produces a series of pictures that show the frame being built up
  // draw group by draw group. The window is set before this frame is recorded.
  // Only sweep once the frame is a real scene: the boot logos have a hundred
  // draws and would waste the twenty screenshots.
  // Any A/B mode that does not touch the constants must leave them real.
  if (g_toggles.ab_mode != 15) {
    constant_zero_mode_ = 0;
  }
  if (g_toggles.ab_mode == 1) {
    // Even screenshots keep blending, odd ones drop it, so the same content is
    // captured both ways a few seconds apart.
    g_toggles.blend_enabled = (screenshots_written_ % 2) == 0;
  } else if (g_toggles.ab_mode == 2) {
    // Same idea for the guest window scissor: proof, not opinion, about whether
    // the scissor rects read from the register file match what the title wants.
    g_toggles.scissor_enabled = (screenshots_written_ % 2) == 0;
  } else if (g_toggles.ab_mode == 8) {
    // Five-phase draw-count sweep: unlimited, then 64 / 128 / 256 / 512 draws.
    // Linear scaling in the number of draws means a per-draw cost (state change,
    // tiler flush); a flat line means per-frame overhead.
    const int phase = int(frames_rendered_ / 10) % 5;
    g_toggles.draw_limit = phase == 0 ? -1 : (64 << (phase - 1));
  } else if (g_toggles.ab_mode == 15) {
    // Constant-locality test. The whole frame is replayed with the real pixel
    // shaders, but every draw is made to read the first slice of the constant
    // ring instead of its own. Nothing else changes: same draws, same geometry,
    // same textures, same pixels. A large speed-up can then only come from the
    // memory the constants are read from, which is the one thing that grows with
    // the number of draws in a frame and explains every measurement so far -
    // including why isolated 128-draw slices are free and whole frames are not.
    constant_zero_mode_ = int(frames_rendered_ / 12) % 3;
  } else if (g_toggles.ab_mode == 10 || g_toggles.ab_mode == 11) {
    // Per-range isolation, one 128-draw slice at a time. A prefix window (ab=8)
    // cannot separate "the first N draws" from "the draws that fill the frame".
    // Note draw_limit is an ABSOLUTE draw index, not a count: the first version
    // of this table asked for skip=128 limit=128 and replayed nothing at all,
    // which cost a whole console run. The last block repeats the whole frame
    // with the guest pixel shader replaced by one flat colour, which is the only
    // clean way to tell shader cost from fixed-function raster cost.
    static const int k_ends[8][2] = {
        {0, -1}, {0, 128}, {128, 256}, {256, 384}, {384, 512}, {512, 640}, {640, 776}, {0, -1}};
    const int block = int(frames_rendered_ / 10) % 8;
    g_toggles.draw_skip = k_ends[block][0];
    g_toggles.draw_limit = k_ends[block][1];
    g_toggles.flat_pixel_shader = (block == 7);
  } else if (g_toggles.ab_mode == 9) {
    // Per-draw cost probe: draw the same first 256 draws every frame, so only
    // the frame constant is left to measure.
    g_toggles.draw_limit = 256;
  } else if (g_toggles.ab_mode == 7) {
    // Depth test on/off in ten-frame blocks: if the frame is made of hundreds of
    // overlapping full-screen layers, depth rejection is the difference between
    // shading a few screens and shading hundreds.
    g_toggles.depth_enabled = (int(frames_rendered_ / 10) % 2) == 0;
  } else if (g_toggles.ab_mode == 6) {
    // Four-phase cycle: baseline, nearest samplers, LOD pinned to zero, and no
    // textures at all.
    const int phase = int(frames_rendered_ / 10) % 4;
    g_toggles.nearest_samplers = phase == 1;
    g_toggles.lod_zero = phase == 2;
    g_toggles.no_textures = phase == 3;
  } else if (g_toggles.ab_mode == 5) {
    // Three-frame-block perf bisection, one run: baseline, vertex-clamped,
    // texture-free. Frame based on purpose - the frame time in the S4 line then
    // belongs to a known variant.
    const int phase = int(frames_rendered_ / 10) % 3;
    g_toggles.clamp_vertices = phase == 1;
    g_toggles.no_textures = phase == 2;
  }
  const bool sweep_ready = draws.size() >= 500;
  if (g_toggles.sweep && !g_toggles.shot_once && screenshots_written_ < 20 && sweep_ready) {
    const int step = g_toggles.sweep_step > 0 ? g_toggles.sweep_step : 100;
    g_toggles.draw_skip = 0;
    g_toggles.draw_limit = int(screenshots_written_ + 1) * step;
  }
  {
    static int logged_skip = -1;
    static int logged_limit = -2;
    if (logged_skip != g_toggles.draw_skip || logged_limit != g_toggles.draw_limit) {
      logged_skip = g_toggles.draw_skip;
      logged_limit = g_toggles.draw_limit;
      REXLOG_INFO(
          "[native] S20 range: skip={} limit={} queued={} shots={} blend={} depth={} scissor={}",
          g_toggles.draw_skip, g_toggles.draw_limit, draws.size(), screenshots_written_,
          g_toggles.blend_enabled ? 1 : 0, g_toggles.depth_enabled ? 1 : 0,
          g_toggles.scissor_enabled ? 1 : 0);
    }
  }
  // Time based, not frame based: the title runs at 3 fps and at 30 fps on
  // different builds, and a fixed frame interval gave 40-second gaps.
  const uint64_t now_ns = rex::graphics::perf::NowNs();
  const uint64_t shot_interval_ns =
      (g_toggles.sweep && g_toggles.sweep_seconds > 0.0)
          ? uint64_t(g_toggles.sweep_seconds * 1.0e9)
          : UINT64_C(3000000000);
  const bool want_live_shot = g_toggles.shot_once;
  const bool shot_this_frame =
      (want_live_shot ||
       (screenshots_written_ < 20 && (!g_toggles.sweep || sweep_ready) &&
       now_ns - last_shot_ns_ >= shot_interval_ns)) &&
      EnsureReadback(width, height);
  if (shot_this_frame) {
    last_shot_ns_ = now_ns;
  }
  if (shot_this_frame) {
    g_toggles.shot_once = false;
    next_shot_live_ = want_live_shot;
  }

  // Textures decoded during this frame go from the staging buffer into their
  // images before any draw that samples them.
  for (const TextureUpload& upload : texture_uploads_) {
    VkImageMemoryBarrier upload_barrier{};
    upload_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    upload_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    upload_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    upload_barrier.image = upload.image;
    upload_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_barrier.subresourceRange.levelCount = 1;
    upload_barrier.subresourceRange.layerCount = 1;
    upload_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    upload_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    upload_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &upload_barrier);
    VkBufferImageCopy copy{};
    copy.bufferOffset = upload.staging_offset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = upload.width;
    copy.imageExtent.height = upload.height;
    copy.imageExtent.depth = 1;
    dfn.vkCmdCopyBufferToImage(command_buffer_, texture_staging_buffer_, upload.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    upload_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    upload_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    upload_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &upload_barrier);
  }

  // The staging area can only be reused once these copies have been recorded.
  texture_uploads_.clear();
  texture_staging_offset_ = 0;

  VkRenderingAttachmentInfo depth_attachment{};
  depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depth_attachment.imageView = depth_view_;
  depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.clearValue.depthStencil.depth = 1.0f;
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = view;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // Dark blue, not black: it tells "the render pass output reached the screen"
  // apart from "nothing was drawn at all".
  color_attachment.clearValue.color = {{0.02f, 0.05f, 0.25f, 1.0f}};
  if (!render_diagnostics_logged_) {
    render_diagnostics_logged_ = true;
    REXLOG_INFO(
        "[native] S7 render: image={} view={} format={} renderArea={}x{} layers={} "
        "ever_written={}",
        static_cast<const void*>(image), static_cast<const void*>(view),
        int32_t(ui::vulkan::VulkanPresenter::kGuestOutputFormat), width, height, 1,
        image_ever_written ? 1 : 0);
  }
  VkRenderingInfo rendering_info{};
  rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering_info.renderArea.extent.width = width;
  rendering_info.renderArea.extent.height = height;
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &color_attachment;
  if (depth_ready) {
    rendering_info.pDepthAttachment = &depth_attachment;
  }
  dfn.vkCmdBeginRendering(command_buffer_, &rendering_info);

  // Marker first as well: if the guest draws break the state machine, this one
  // still proves the pass writes to the image.
  const auto draw_marker = [&](float x, float y) {
    if (debug_pipeline_ == VK_NULL_HANDLE || !REXCVAR_QUERY(bool, native_draw_marker)) {
      return;
    }
    VkDescriptorSet marker_sets[4] = {shared_memory_set_, constants_set_, empty_texture_set_,
                                      empty_texture_set_};
    const uint32_t marker_offsets[5] = {};
    dfn.vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline_);
    dfn.vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                                0, 4, marker_sets, 5, marker_offsets);
    VkViewport marker_viewport{};
    marker_viewport.x = x;
    marker_viewport.y = y;
    marker_viewport.width = 240.0f;
    marker_viewport.height = 240.0f;
    marker_viewport.maxDepth = 1.0f;
    dfn.vkCmdSetViewport(command_buffer_, 0, 1, &marker_viewport);
    VkRect2D marker_scissor{};
    marker_scissor.offset.x = int32_t(x);
    marker_scissor.offset.y = int32_t(y);
    marker_scissor.extent.width = 240;
    marker_scissor.extent.height = 240;
    dfn.vkCmdSetScissor(command_buffer_, 0, 1, &marker_scissor);
    dfn.vkCmdDraw(command_buffer_, 3, 1, 0, 0);
  };
  draw_marker(0.0f, 0.0f);

  VkPipeline bound_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout bound_layout = VK_NULL_HANDLE;
  if (g_toggles.list_once && draws.size() >= 500) {
    g_toggles.list_once = false;
    // The listing always covers the whole frame: the replay window is a
    // separate axis and would cut it to the first sweep slice.
    const int list_limit = int(draws.size());
    REXLOG_INFO("[native] S20 list: queued={} skip={} limit={} frame={}", draws.size(),
                g_toggles.draw_skip, g_toggles.draw_limit, frames_rendered_);
    for (size_t i = size_t(g_toggles.draw_skip); i < draws.size() && int(i) < list_limit; ++i) {
      const FrameDraw& listed = draws[i];
      REXLOG_INFO(
          "[native] S20 list #{} prim={} count={} indexed={} vp={:.0f}x{:.0f}+{:.0f}+{:.0f} "
          "tex={}/{} key={:016X}",
          i, listed.prim_type, listed.index_count, listed.indexed ? 1 : 0, listed.viewport[2],
          listed.viewport[3], listed.viewport[0], listed.viewport[1],
          listed.texture_views_vertex.size(), listed.texture_views_pixel.size(), listed.state_key);
    }
  }
  uint64_t drawn = 0;
  uint32_t timed_draws = 0;
  frame_viewport_pixels_ = 0;
  frame_scissor_pixels_ = 0;
  empty_scissor_draws_ = 0;
  frame_vertices_ = 0;
  for (uint32_t& bucket : area_buckets_) {
    bucket = 0;
  }
  for (size_t replay_index = 0; replay_index < draws.size(); ++replay_index) {
    if (g_toggles.draw_limit >= 0 && int(replay_index) >= g_toggles.draw_limit) {
      break;
    }
    if (int(replay_index) < g_toggles.draw_skip) {
      continue;
    }
    const FrameDraw& draw = draws[replay_index];
    if (draw.pipeline == VK_NULL_HANDLE) {
      continue;
    }
    // Texture sets are per draw now, so all four sets are bound for every draw.
    const auto make_texture_set = [&](VkDescriptorSetLayout set_layout,
                                      const std::vector<VkImageView>& views,
                                      const std::vector<VkSampler>& samplers) -> VkDescriptorSet {
      if (set_layout == VK_NULL_HANDLE || set_layout == descriptor_set_layout_empty_) {
        return empty_texture_set_;
      }
      if (texture_pool_ == VK_NULL_HANDLE) {
        return empty_texture_set_;
      }
      VkDescriptorSetAllocateInfo allocate_info{};
      allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocate_info.descriptorPool = texture_pool_;
      allocate_info.descriptorSetCount = 1;
      allocate_info.pSetLayouts = &set_layout;
      VkDescriptorSet set = VK_NULL_HANDLE;
      if (dfn.vkAllocateDescriptorSets(device, &allocate_info, &set) != VK_SUCCESS) {
        return empty_texture_set_;
      }
      VkDescriptorImageInfo image_infos[64] = {};
      VkWriteDescriptorSet writes[64] = {};
      uint32_t write_count = 0;
      for (size_t i = 0; i < views.size() && write_count < 64; ++i) {
        image_infos[write_count].imageView =
            (!g_toggles.no_textures && views[i] != VK_NULL_HANDLE) ? views[i] : dummy_image_view_;
        image_infos[write_count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet& write = writes[write_count];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = uint32_t(i);
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &image_infos[write_count];
        ++write_count;
      }
      for (size_t i = 0; i < samplers.size() && write_count < 64; ++i) {
        image_infos[write_count].sampler =
            samplers[i] != VK_NULL_HANDLE ? samplers[i] : dummy_sampler_;
        VkWriteDescriptorSet& write = writes[write_count];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = uint32_t(views.size() + i);
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_infos[write_count];
        ++write_count;
      }
      dfn.vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
      return set;
    };
    VkDescriptorSet sets[4] = {
        shared_memory_set_, constants_set_,
        make_texture_set(draw.textures_layout_vertex, draw.texture_views_vertex,
                         draw.samplers_vertex),
        make_texture_set(draw.textures_layout_pixel, draw.texture_views_pixel,
                         draw.samplers_pixel)};
    uint32_t constant_offsets[5];
    std::memcpy(constant_offsets, draw.constant_offsets, sizeof(constant_offsets));
    if (constant_zero_mode_ >= 1) {
      // Keep the vertex block real so the geometry, and therefore the covered
      // pixels, stay identical.
      constant_offsets[SpirvShaderTranslator::kConstantBufferFloatPixel] = 0;
    }
    if (constant_zero_mode_ >= 2) {
      for (uint32_t& offset : constant_offsets) {
        offset = 0;
      }
    }
    dfn.vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.layout, 0,
                                4, sets, 5, constant_offsets);
    bound_layout = draw.layout;
    // Guest viewport, clamped to the presenter image so the driver never sees a
    // viewport outside the attachment.
    float viewport_x = draw.viewport[0];
    float viewport_y = draw.viewport[1];
    float viewport_width = draw.viewport[2];
    float viewport_height = draw.viewport[3];
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
      ++empty_viewport_draws_;
      continue;
    }
    if (viewport_x < 0.0f) {
      viewport_width += viewport_x;
      viewport_x = 0.0f;
    }
    if (viewport_y < 0.0f) {
      viewport_height += viewport_y;
      viewport_y = 0.0f;
    }
    if (viewport_x + viewport_width > float(width)) {
      viewport_width = float(width) - viewport_x;
    }
    if (viewport_y + viewport_height > float(height)) {
      viewport_height = float(height) - viewport_y;
    }
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
      ++empty_viewport_draws_;
      continue;
    }
    VkViewport viewport{};
    viewport.x = viewport_x;
    viewport.y = viewport_y;
    viewport.width = viewport_width;
    viewport.height = viewport_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    dfn.vkCmdSetViewport(command_buffer_, 0, 1, &viewport);
    // Guest window scissor intersected with the clamped viewport. The scissor is
    // not decoration: this title draws full-screen quads and lets the scissor
    // confine them to a UI element, so ignoring it rasterises the whole
    // attachment for every such draw (measured as the 0.4-0.5 swaps/s menu).
    int32_t scissor_x0 = int32_t(viewport_x);
    int32_t scissor_y0 = int32_t(viewport_y);
    int32_t scissor_x1 = int32_t(viewport_x + viewport_width);
    int32_t scissor_y1 = int32_t(viewport_y + viewport_height);
    frame_viewport_pixels_ += uint64_t(scissor_x1 - scissor_x0) * uint64_t(scissor_y1 - scissor_y0);
    if (g_toggles.scissor_enabled && draw.scissor[2] != 0 && draw.scissor[3] != 0) {
      const int32_t guest_x0 = int32_t(draw.scissor[0]);
      const int32_t guest_y0 = int32_t(draw.scissor[1]);
      const int32_t guest_x1 = guest_x0 + int32_t(draw.scissor[2]);
      const int32_t guest_y1 = guest_y0 + int32_t(draw.scissor[3]);
      if (guest_x0 > scissor_x0) {
        scissor_x0 = guest_x0;
      }
      if (guest_y0 > scissor_y0) {
        scissor_y0 = guest_y0;
      }
      if (guest_x1 < scissor_x1) {
        scissor_x1 = guest_x1;
      }
      if (guest_y1 < scissor_y1) {
        scissor_y1 = guest_y1;
      }
    }
    if (scissor_x1 <= scissor_x0 || scissor_y1 <= scissor_y0) {
      ++empty_scissor_draws_;
      continue;
    }
    const uint64_t scissor_area =
        uint64_t(scissor_x1 - scissor_x0) * uint64_t(scissor_y1 - scissor_y0);
    frame_scissor_pixels_ += scissor_area;
    {
      uint32_t bucket = 0;
      uint64_t area = scissor_area;
      while (area > 1024 && bucket < 7) {
        area >>= 1;
        ++bucket;
      }
      ++area_buckets_[bucket];
    }
    // A non-indexed rectangle list is three guest vertices per rectangle, but
    // the translated vertex shader consumes four per rectangle (the fourth is
    // the mirrored corner), so the host vertex count is four thirds of the
    // guest's. Indexed rectangle lists keep the guest count until the index
    // expansion is implemented.
    uint32_t draw_vertices = draw.index_count;
    if (!draw.indexed && draw.prim_type == uint32_t(xenos::PrimitiveType::kRectangleList)) {
      draw_vertices = (draw.index_count / 3) * 4;
    }
    // ab=5 variant: a draw clamped to three vertices keeps its pipeline, its
    // state changes and its texture bindings, so only vertex work disappears.
    if (g_toggles.clamp_vertices && draw_vertices > 3) {
      draw_vertices = 3;
    }
    frame_vertices_ += draw_vertices;
    ab_variant_ = g_toggles.no_textures ? 2
                  : (g_toggles.nearest_samplers ? 3
                                                : (g_toggles.lod_zero ? 4 : 0));
    if (g_toggles.ab_mode == 8) {
      ab_variant_ = drawn > 0 ? drawn : 0;
    }
    ab_variant_ = g_toggles.clamp_vertices ? 1 : ab_variant_;
    VkRect2D scissor{};
    scissor.offset.x = scissor_x0;
    scissor.offset.y = scissor_y0;
    scissor.extent.width = uint32_t(scissor_x1 - scissor_x0);
    scissor.extent.height = uint32_t(scissor_y1 - scissor_y0);
    dfn.vkCmdSetScissor(command_buffer_, 0, 1, &scissor);
    const bool time_this_draw =
        timers_armed && (timed_draws * 2 + 1) < draw_time_capacity_;
    if (time_this_draw) {
      fp_cmd_write_timestamp_(command_buffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              draw_time_pool_, timed_draws * 2);
      draw_time_key_[timed_draws] = draw.state_key;
    }
    if (draw.pipeline != bound_pipeline) {
      dfn.vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.pipeline);
      bound_pipeline = draw.pipeline;
    }
    dfn.vkCmdSetBlendConstants(command_buffer_, draw.blend_constants);
    if (draw.indexed) {
      dfn.vkCmdBindIndexBuffer(command_buffer_, shared_memory_buffer_, draw.index_base_bytes,
                               draw.index_type);
      dfn.vkCmdDrawIndexed(command_buffer_, draw_vertices, 1, 0, 0, 0);
    } else {
      dfn.vkCmdDraw(command_buffer_, draw_vertices, 1, 0, 0);
    }
    if (time_this_draw) {
      fp_cmd_write_timestamp_(command_buffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              draw_time_pool_, timed_draws * 2 + 1);
      ++timed_draws;
    }
    ++drawn;
  }
  if (timers_armed) {
    draw_time_recorded_ = timed_draws * 2;
  }
  draws_rendered_ += drawn;
  if (drawn == 0 && !draws.empty() && g_toggles.draw_limit >= 0) {
    static bool warned_empty_window = false;
    if (!warned_empty_window) {
      warned_empty_window = true;
      REXLOG_ERROR("[native] S4: window {}+{} replayed none of {} draws",
                   g_toggles.draw_skip, g_toggles.draw_limit, draws.size());
    }
  }
  if (empty_viewport_draws_ && frames_rendered_ < 440) {
    REXLOG_INFO("[native] S5: {} draws skipped with an empty viewport", empty_viewport_draws_);
    empty_viewport_draws_ = 0;
  }

  // Second marker on the other side, after every guest draw.
  draw_marker(260.0f, 0.0f);

  dfn.vkCmdEndRendering(command_buffer_);

  barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.newLayout = shot_this_frame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                      : ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
  barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = shot_this_frame
                              ? VK_ACCESS_TRANSFER_READ_BIT
                              : ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
  dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           shot_this_frame ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                           : ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
  if (shot_this_frame) {
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = width;
    copy.imageExtent.height = height;
    copy.imageExtent.depth = 1;
    dfn.vkCmdCopyImageToBuffer(command_buffer_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback_buffer_, 1, &copy);
    VkImageMemoryBarrier back{};
    back.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.image = image;
    back.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    back.subresourceRange.levelCount = 1;
    back.subresourceRange.layerCount = 1;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
    dfn.vkCmdPipelineBarrier(command_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask, 0, 0,
                             nullptr, 0, nullptr, 1, &back);
    shot_pending_ = true;
  }

  if (dfn.vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
    return false;
  }

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer_;
  const ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
      device_->AcquireQueue(device_->queue_family_graphics_compute(), 0);
  if (dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, clear_fence_) != VK_SUCCESS) {
    REXLOG_ERROR("[native] S4: frame submit failed");
    return false;
  }
  fence_submitted_ = true;
  ++frames_rendered_;
  if ((frames_rendered_ % 10) == 1 || g_toggles.ab_mode >= 10) {
    const uint64_t total_ns = rex::graphics::perf::NowNs() - frame_start_ns;
    REXLOG_INFO(
        "[native] S4: frame {} draws={} fences={} ms record={} ms total={} ms textures={} "
        "px={} spx={} empty_scissor={} verts={} abv={} areas={},{},{},{},{},{},{},{} "
        "edram=c{}/x{}/d{}/o{} surf={} surf0={:X} zt={} zw={} zless={} win={}+{} fl={} "
        "ring={}KB cz={}",
        frames_rendered_, drawn, double(fence_wait_ns) / 1e6,
        double(total_ns - fence_wait_ns) / 1e6, double(total_ns) / 1e6, textures_.size(),
        frame_viewport_pixels_, frame_scissor_pixels_, empty_scissor_draws_, frame_vertices_,
        ab_variant_, area_buckets_[0], area_buckets_[1], area_buckets_[2], area_buckets_[3],
        area_buckets_[4], area_buckets_[5], area_buckets_[6], area_buckets_[7],
        frame_edram_color_, frame_edram_copy_, frame_edram_depth_, frame_edram_other_,
        frame_surface_count_, frame_surface_[0], frame_depth_test_, frame_depth_write_,
        frame_depth_less_, g_toggles.draw_skip, g_toggles.draw_limit,
        g_toggles.flat_pixel_shader ? 1 : 0, constants_ring_offset_ / 1024, constant_zero_mode_);
  }
  // The EDRAM and surface counters describe the draws just replayed and logged;
  // zeroing them earlier than the report made every run print zeros.
  frame_edram_color_ = 0;
  frame_edram_copy_ = 0;
  frame_edram_depth_ = 0;
  frame_edram_other_ = 0;
  frame_surface_count_ = 0;
  frame_depth_test_ = 0;
  frame_depth_write_ = 0;
  frame_depth_less_ = 0;
  return true;
}

}  // namespace rex::graphics::switch_native
