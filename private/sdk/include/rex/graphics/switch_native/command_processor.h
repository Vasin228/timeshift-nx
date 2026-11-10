/**
 * Part 111/112: native Switch renderer - command processor.
 *
 * M0: backend exists, runs its worker thread, counts swaps.
 * M1: clears the presenter's guest output image to a cycling colour and presents.
 * S2: descriptor set layouts, pipeline layout and a VkPipelineCache, proven by
 *     creating and destroying one pipeline at startup.
 * S3: fixed-function state from the register file, hashed into a pipeline key.
 * Geometry, textures and render targets follow in M2-M5
 * (see PLAN_NATIVE_RENDERER_EXEC.md).
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <rex/graphics/command_processor.h>
#include <rex/hash.h>

#include <rex/ui/vulkan/instance.h>

namespace rex::graphics {
class SpirvShaderTranslator;
}  // namespace rex::graphics

namespace rex::ui::vulkan {
class VulkanDevice;
}  // namespace rex::ui::vulkan

namespace rex::system {
class KernelState;
}  // namespace rex::system

namespace thps::native {
class Renderer;
}

namespace rex::graphics::switch_native {

class SwitchNativeCommandProcessor final : public CommandProcessor {
 public:
  SwitchNativeCommandProcessor(GraphicsSystem* graphics_system,
                               system::KernelState* kernel_state);
  ~SwitchNativeCommandProcessor() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;
  void WriteRegister(uint32_t index, uint32_t value) override;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
  bool IssueCopy() override;
  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address, uint32_t dword_count) override;
  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info, bool major_mode_explicit) override;

 private:
  // Records the clear of the guest output image and submits it on the CP thread.
  bool ClearGuestOutput(VkImage image, bool image_ever_written);

  // S2: the device objects every guest drawing pipeline will be built on.
  bool CreatePipelineInfrastructure();
  bool RunPipelineSelfTest();
  void LogDeviceLimits();

  std::unique_ptr<thps::native::Renderer> native_renderer_;
  ui::vulkan::VulkanDevice* device_ = nullptr;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
  // ab=15: 0 = real constant offsets, 1 = pixel block forced to zero, 2 = all
  // blocks forced to zero. Diagnostic for constant-ring locality only.
  int constant_zero_mode_ = 0;

  // S22: per-draw GPU timestamps (ab=16). Two queries per replayed draw, read
  // back one frame later after the fence, so no run is slowed by the probe.
  // Needed because every indirect measurement so far could be explained two
  // ways; the only way left is to ask the GPU what each draw actually cost.
  bool InitializeDrawTimers(uint32_t draws);
  void CollectDrawTimers();
  void ReportDrawTimers();
  VkQueryPool draw_time_pool_ = VK_NULL_HANDLE;
  uint32_t draw_time_capacity_ = 0;
  std::vector<uint64_t> draw_time_host_;
  std::vector<double> draw_time_ms_;
  std::vector<uint64_t> draw_time_key_;
  uint32_t draw_time_recorded_ = 0;
  uint32_t draw_time_frames_ = 0;
  double draw_time_period_ = 0.0;
  bool draw_time_ok_ = false;
  bool draw_time_tried_ = false;
  PFN_vkCmdResetQueryPool fp_cmd_reset_query_pool_ = nullptr;
  PFN_vkCmdWriteTimestamp fp_cmd_write_timestamp_ = nullptr;
  PFN_vkGetQueryPoolResults fp_get_query_pool_results_ = nullptr;
  VkFence clear_fence_ = VK_NULL_HANDLE;
  bool fence_submitted_ = false;

  // S4: the four sets the translated SPIR-V declares, in the order of
  // SpirvShaderTranslator::DescriptorSet.
  //   0: shared memory (binding 0) + EDRAM (binding 1), both storage buffers
  //   1: the five guest draw constant buffers
  //   2: vertex textures, 3: pixel textures (empty until S6)
  VkDescriptorSetLayout descriptor_set_layout_empty_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_shared_memory_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_constants_ = VK_NULL_HANDLE;
  // How many storage buffers cover guest memory on this device.
  uint32_t shared_memory_buffer_count_ = 1;

  // S4: guest physical memory exposed to the translated shaders as a storage
  // buffer (set 0 binding 0). Created on the first draw, host visible so the
  // CPU can copy guest pages straight into it.
  bool EnsureSharedMemoryBuffer();
  void UploadGuestMemoryChunk(VkDeviceSize budget_bytes);
  // S4: descriptor pool and the two sets the draws will bind (shared memory,
  // constants). Placeholders are used for the constants until the per-draw
  // constant buffers exist.
  bool CreateDescriptorSets();
  bool shared_memory_ready_ = false;
  VkBuffer shared_memory_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory shared_memory_memory_ = VK_NULL_HANDLE;
  void* shared_memory_mapped_ = nullptr;
  VkDeviceSize shared_memory_size_ = 0;

  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet shared_memory_set_ = VK_NULL_HANDLE;
  VkDescriptorSet constants_set_ = VK_NULL_HANDLE;
  // One small uniform buffer standing in for the five constant buffers.
  VkBuffer constants_placeholder_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory constants_placeholder_memory_ = VK_NULL_HANDLE;
  VkDeviceSize constants_placeholder_size_ = 0;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
  bool pipeline_self_test_ok_ = false;
  // Kept so a marker triangle can be drawn over the guest frame: if it shows
  // up, presentation and the command buffer path work and the problem is in the
  // translated guest pipeline.
  VkPipeline debug_pipeline_ = VK_NULL_HANDLE;

  uint64_t swaps_ = 0;
  uint64_t draws_ = 0;
  // Largest index_count seen: a title that only ever submits handfuls of
  // vertices is not submitting a scene.
  uint32_t max_draw_vertices_ = 0;
  // Vertex count of the draw currently being prepared, so shader dumping can
  // tell scene shaders (thousands of vertices) from menu ones.
  uint32_t last_draw_vertex_count_ = 0;
  // Per-swap histogram: a scene arrives as a few big draws, a menu can be many
  // tiny ones. Without this split "778 draws per frame" says nothing.
  uint64_t big_draws_ = 0;
  uint64_t tiny_draws_ = 0;
  uint64_t indices_ = 0;
  uint64_t last_report_draws_ = 0;

  // M2: guest ucode hash -> translated shader (VulkanShader from the shared backend).
  std::unordered_map<uint64_t, Shader*> shaders_;

  // S3: the fixed-function state a pipeline depends on, assembled from the
  // register file exactly once per draw. Comparing whole structs is fine
  // because every field is zero-initialised before use.
  struct PipelineStateKey {
    uint64_t vertex_shader_hash = 0;
    uint64_t pixel_shader_hash = 0;
    uint32_t primitive_type = 0;
    uint32_t depth_control = 0;
    uint32_t color_mask = 0;
    uint32_t pa_su_sc_mode = 0;
    uint32_t color_control = 0;
    uint32_t blend_control[4] = {};
    // Runtime diagnostics toggled from sdmc:/switch/timeshift/debug.txt, so the
    // mode can be changed between frames without rebuilding or relaunching.
    uint32_t debug_bits = 0;

    bool operator==(const PipelineStateKey& other) const {
      return std::memcmp(this, &other, sizeof(*this)) == 0;
    }
  };
  struct PipelineStateKeyHash {
    size_t operator()(const PipelineStateKey& key) const {
      return size_t(XXH3_64bits(&key, sizeof(key)));
    }
  };

  // S3: reads the state registers for this draw and counts how many distinct
  // pipeline keys the title actually uses.
  void TrackPipelineState(xenos::PrimitiveType prim_type);
  void LogFirstPipelineKeys();

  // S4: the draw list of the frame in flight, replayed inside the presenter's
  // guest output image at swap time (only there is the image known).
  struct FrameDraw {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet textures_vertex = VK_NULL_HANDLE;
    VkDescriptorSet textures_pixel = VK_NULL_HANDLE;
    // Dynamic uniform buffer offsets of the five constant blocks for this draw.
    uint32_t constant_offsets[5] = {};
    // Guest viewport for this draw: x, y, width, height in pixels.
    float viewport[4] = {};
    // Guest window scissor for this draw: x, y (offset), width, height (extent).
    // Captured per draw because the register file is shared and the frame is
    // replayed later; ignoring it rasterises every full-screen quad whole.
    uint32_t scissor[4] = {};
    VkDescriptorSetLayout textures_layout_vertex = VK_NULL_HANDLE;
    VkDescriptorSetLayout textures_layout_pixel = VK_NULL_HANDLE;
    std::vector<VkImageView> texture_views_vertex;
    std::vector<VkImageView> texture_views_pixel;
    std::vector<VkSampler> samplers_vertex;
    std::vector<VkSampler> samplers_pixel;
    uint32_t index_count = 0;
    uint32_t index_base_bytes = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT16;
    bool indexed = false;
    // S20 diagnostics: the guest primitive and the pipeline state hash, so the
    // draw order can be read from a log line.
    uint32_t prim_type = 0;
    uint64_t state_key = 0;
    // Constant blend factors of this draw (RB_BLEND_RED..ALPHA).
    float blend_constants[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  };
  struct PipelineEntry {
    VkPipeline pipeline = VK_NULL_HANDLE;
    // Same state, but the fragment stage replaced by a flat colour shader
    // (native_flat_ps). Diagnostic only: it isolates the vertex path.
    VkPipeline pipeline_flat = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet textures_vertex = VK_NULL_HANDLE;
    VkDescriptorSet textures_pixel = VK_NULL_HANDLE;
    VkDescriptorSetLayout textures_layout_vertex = VK_NULL_HANDLE;
    VkDescriptorSetLayout textures_layout_pixel = VK_NULL_HANDLE;
    uint32_t uses = 0;
  };

  // Sets 2 and 3 depend on the texture and sampler bindings a translated shader
  // declares, so they cannot be shared between pipelines - giving the driver a
  // pipeline layout that lacks a binding the shader uses killed it inside
  // vkCreateGraphicsPipelines.
  struct TextureLayoutEntry {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
  };
  struct PipelineLayoutEntry {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet textures_vertex = VK_NULL_HANDLE;
    VkDescriptorSet textures_pixel = VK_NULL_HANDLE;
  };
  static uint64_t TextureLayoutKey(bool is_vertex, uint32_t texture_count,
                                   uint32_t sampler_count);
  TextureLayoutEntry& GetOrCreateTextureLayout(bool is_vertex, uint32_t texture_count,
                                               uint32_t sampler_count);
  PipelineLayoutEntry& GetOrCreatePipelineLayout(uint32_t texture_count_vertex,
                                                 uint32_t sampler_count_vertex,
                                                 uint32_t texture_count_pixel,
                                                 uint32_t sampler_count_pixel);
  // 1x1 stand-in image and a default sampler, so every texture binding points at
  // something valid before the real texture cache exists (S6).
  bool InitializeDummyTextureResources();
  // S5: one host-visible ring holding all five constant blocks of every draw in
  // the frame, selected at bind time with dynamic uniform buffer offsets.
  bool CreateConstantsRing();
  void PrepareDraw(FrameDraw& draw, uint32_t index_count);

  // S6: guest textures, decoded to RGBA8 on the CPU and cached as VkImages.
  struct TextureEntry {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
  };
  struct TextureUpload {
    VkImage image = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkDeviceSize staging_offset = 0;
  };
  bool EnsureTextureStaging(VkDeviceSize size);
  VkImageView GetOrCreateTexture(const xenos::xe_gpu_texture_fetch_t& fetch);
  VkSampler GetOrCreateSampler(const xenos::xe_gpu_texture_fetch_t& fetch);


  std::unordered_map<uint64_t, TextureEntry> textures_;
  std::unordered_map<uint64_t, VkSampler> samplers_;
  VkBuffer texture_staging_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory texture_staging_memory_ = VK_NULL_HANDLE;
  void* texture_staging_mapped_ = nullptr;
  VkDeviceSize texture_staging_size_ = 0;
  VkDeviceSize texture_staging_offset_ = 0;
  std::vector<TextureUpload> texture_uploads_;
  VkDescriptorPool texture_pool_ = VK_NULL_HANDLE;
  uint64_t textures_created_ = 0;
  uint32_t texture_failures_ = 0;
  VkBuffer constants_ring_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory constants_ring_memory_ = VK_NULL_HANDLE;
  void* constants_ring_mapped_ = nullptr;
  VkDeviceSize constants_ring_size_ = 0;
  VkDeviceSize constants_ring_offset_ = 0;
  std::unordered_map<uint64_t, TextureLayoutEntry> texture_layouts_;
  std::unordered_map<uint64_t, PipelineLayoutEntry> pipeline_layouts_;
  VkImage dummy_image_ = VK_NULL_HANDLE;
  VkDeviceMemory dummy_image_memory_ = VK_NULL_HANDLE;
  VkImageView dummy_image_view_ = VK_NULL_HANDLE;
  VkSampler dummy_sampler_ = VK_NULL_HANDLE;
  VkDescriptorSet empty_texture_set_ = VK_NULL_HANDLE;

  const PipelineEntry* GetOrCreatePipeline(const PipelineStateKey& key);
  // rectangle_list translates the vertex shader with the rectangle-list host
  // type, which turns one three-vertex guest rectangle into a four-vertex
  // triangle strip (the fourth vertex is the mirrored corner).
  VkShaderModule GetOrCreateModule(Shader* shader, bool is_vertex, bool rectangle_list = false);
  // Reads guest physical memory for diagnostics only. Pages the title never
  // committed fault on Horizon: an unguarded 64-byte peek inside a log line cost
  // two runs (data abort at PrepareDraw, fault address 0), so every diagnostic
  // read goes through here and simply reports "not readable" instead.
  bool ReadGuestForDiagnostics(uint64_t address, void* out, size_t size) const;
  VkShaderModule flat_pixel_module_ = VK_NULL_HANDLE;
  VkShaderModule marker_vertex_module_ = VK_NULL_HANDLE;
  VkShaderModule probe_pixel_module_ = VK_NULL_HANDLE;
  // Records every queued draw into the guest output image and submits it.
  bool RenderFrame(VkImage image, VkImageView view, bool image_ever_written, uint32_t width,
                   uint32_t height);

  std::unordered_map<PipelineStateKey, PipelineEntry, PipelineStateKeyHash> pipelines_;
  std::vector<FrameDraw> frame_draws_;
  // A pixel shader is optional for depth-only draws; the pipeline then has one.
  VkPipelineLayout pipeline_layout_for_draws_ = VK_NULL_HANDLE;
  uint64_t frames_rendered_ = 0;
  // Depth buffer. Guest depth lives in EDRAM, which is not emulated yet, so the
  // renderer keeps its own D32 target. Without it every draw paints over the
  // previous one, the whole scene shows through and the frame is unreadable.
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  uint32_t depth_width_ = 0;
  uint32_t depth_height_ = 0;
  bool EnsureDepthTarget(uint32_t width, uint32_t height);
  // Self-screenshot: the finished frame is copied into a host-visible buffer and
  // written to the SD card, so a run can be inspected without any capture
  // sysmodule (the boot-time one crashes firmware 22.x, see MODLOG Part 125).
  VkBuffer readback_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
  void* readback_mapped_ = nullptr;
  VkDeviceSize readback_size_ = 0;
  uint32_t readback_width_ = 0;
  uint32_t readback_height_ = 0;
  uint32_t screenshots_written_ = 0;
  bool shot_pending_ = false;
  // A screenshot requested from debug.txt is written to a fixed name so it can
  // be fetched immediately, and is not limited by the eight automatic shots.
  bool next_shot_live_ = false;
  uint64_t last_shot_ns_ = 0;
  bool EnsureReadback(uint32_t width, uint32_t height);
  void WriteScreenshotBmp();
  uint64_t draws_rendered_ = 0;
  uint64_t pipeline_failures_ = 0;
  uint64_t empty_viewport_draws_ = 0;
  // Rasterised area of the last replayed frame: viewport rectangles versus the
  // guest-scissor intersection. The ratio is the overdraw the scissor prevents.
  uint64_t frame_viewport_pixels_ = 0;
  uint64_t frame_scissor_pixels_ = 0;
  uint64_t empty_scissor_draws_ = 0;
  // Vertices submitted by the last replayed frame: separates vertex work from
  // fill work when the frame time is read.
  uint64_t frame_vertices_ = 0;
  // Which ab=5 bisection variant the last replayed frame used (0 baseline,
  // 1 vertices clamped, 2 textures replaced by a dummy).
  int ab_variant_ = 0;
  // Scissor-area histogram of the last frame (log2 buckets): how many draws are
  // nearly full screen versus genuinely small. 245x screen coverage cannot be
  // read off total pixels alone.
  uint32_t area_buckets_[8] = {};
  // Guest EDRAM mode and colour surface of the draws recorded for the next
  // frame. A resolve or clear (kCopy) replayed as a colour draw runs whatever
  // pixel shader happened to be bound; more than one surface per frame means the
  // title renders offscreen and this backend does not.
  uint32_t frame_edram_color_ = 0;
  uint32_t frame_edram_copy_ = 0;
  uint32_t frame_edram_depth_ = 0;
  uint32_t frame_edram_other_ = 0;
  uint32_t frame_surface_count_ = 0;
  uint32_t frame_surface_[8] = {};
  // Depth state of the recorded draws: if the depth test is never enabled, or
  // never writes, every layer shades its whole rectangle because nothing can be
  // rejected - which is what 245 screens of coverage per frame looks like.
  uint32_t frame_depth_test_ = 0;
  uint32_t frame_depth_write_ = 0;
  uint32_t frame_depth_less_ = 0;
  uint64_t vertex_fetch_copies_ = 0;
  // Guest memory pages already copied into the storage buffer during the frame.
  // Without this the same multi-megabyte vertex buffer is copied once per draw -
  // 780 draws x several MiB per frame is what pinned the console at 0.1 fps.
  std::vector<uint64_t> copied_pages_;
  uint64_t vertex_pages_copied_ = 0;
  bool render_diagnostics_logged_ = false;
  // Pages uploaded for the frame currently being recorded, and how often the
  // per-frame upload ceiling was reached (a garbage fetch constant shows up as a
  // non-zero count here instead of as a frame rate collapse).
  uint32_t pages_copied_this_frame_ = 0;
  // Pages the guest never committed: reading them faults, so they are skipped
  // and counted instead of being copied.
  uint64_t uncommitted_pages_skipped_ = 0;
  uint32_t uncommitted_page_logs_ = 0;
  uint64_t copy_budget_hits_ = 0;
  bool copy_budget_logged_ = false;
  uint32_t logged_textured_draws_ = 0;
  // Guest memory is uploaded in bounded chunks so no single frame stalls on
  // a large memcpy (a 512 MiB snapshot took the console down).
  // IssueDraw runs on the GPU worker thread, RenderFrame on the UI thread.
  // Every shared container (draw queue, pipelines, state keys) is guarded by
  // this mutex; without it a concurrent vector reallocation killed the app.
  std::mutex state_mutex_;
  VkDeviceSize shared_memory_upload_offset_ = 0;

  // S1: shared Xenos ucode -> SPIR-V translator and its inventory counters.
  std::unique_ptr<SpirvShaderTranslator> translator_;
  uint64_t translated_shaders_ = 0;
  uint64_t failed_shaders_ = 0;

  // S3 counters.
  std::unordered_map<PipelineStateKey, uint32_t, PipelineStateKeyHash> pipeline_state_keys_;
  uint64_t state_key_draws_ = 0;
  uint32_t logged_state_keys_ = 0;
  PipelineStateKey last_state_key_{};

  uint64_t last_report_swaps_ = 0;
  uint64_t last_report_ns_ = 0;
};

}  // namespace rex::graphics::switch_native