/**
 * @file    gpu/overlay/overlay_drawer.cpp
 */
#include "renut_engine/gpu/overlay/overlay_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include <plume_render_interface.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/video.h"
#if defined(TIMESHIFT_RECOMP)
#include <rex/cvar.h>
#include "perf_breakdown.h"
#endif

#include "host_shaders/overlay_ps.spirv.h"
#include "host_shaders/overlay_vs.spirv.h"
#if defined(PLUME_DEKO3D)  // banjo-nx (2026-10-17): the deko3d back end's
#include "host_shaders/overlay_ps.deko3d.h"
#include "host_shaders/overlay_vs.deko3d.h"
#endif
#if defined(_WIN32)
#include "host_shaders/overlay_ps.dxil.h"
#include "host_shaders/overlay_vs.dxil.h"
#endif

#if defined(TIMESHIFT_RECOMP)
REXCVAR_DEFINE_BOOL(renut_fps_overlay, true, "Renut",
                    "Draw real-time FPS/frametime HUD overlay on screen");
REXCVAR_DECLARE(bool, renut_skip_shadows);
REXCVAR_DECLARE(int32_t, renut_far_plane_m);
REXCVAR_DECLARE(bool, renut_vsync);
REXCVAR_DECLARE(std::string, renut_anisotropic_filtering);
REXCVAR_DECLARE(bool, renut_skip_orig_d3d);
REXCVAR_DECLARE(bool, renut_deko3d_zcull);
REXCVAR_DECLARE(bool, renut_deko3d_depth_compression);
REXCVAR_DECLARE(std::string, renut_force_level);
#endif

namespace renut::gpu {

// What the SDK drew in one ImGuiDrawer::Draw, in render target pixels of
// width x height.
struct OverlayDrawer::Frame {
  struct Batch {
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
  };
  struct DrawCall {
    uint32_t batch = 0;
    bool lines = false;
    uint32_t count = 0;
    uint32_t index_offset = 0;
    int32_t base_vertex = 0;
    plume::RenderRect scissor;
    std::shared_ptr<OverlayTextureData> texture;
  };

  uint32_t width = 0;
  uint32_t height = 0;
  float scale[2] = {};      // coordinate space -> clip space, y down
  float translate[2] = {};
  std::vector<rex::ui::ImmediateVertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<Batch> batches;
  std::vector<DrawCall> draws;
};

namespace {

const plume::RenderInputSlot kVertexSlot(0, sizeof(rex::ui::ImmediateVertex),
                                         plume::RenderInputSlotClassification::PER_VERTEX_DATA);

// The latest recorded frame (UI thread writes, presenting thread reads).
std::mutex g_frame_mutex;
std::shared_ptr<const OverlayDrawer::Frame> g_latest;
uint64_t g_frames_recorded = 0;  // UI thread

// Host textures of textures gone, for the presenting thread to release.
struct Released {
  std::unique_ptr<plume::RenderTexture> texture;
  std::unique_ptr<plume::RenderTextureView> view;
  uint32_t slot = kInvalidDescriptor;
};
std::mutex g_release_mutex;
std::vector<Released> g_released;

// Pipelines and the white texel, made on the presenting thread on first use.
// Kept to the end of the process: the device outlives every frame drawn.
struct Resources {
  bool ready = false;
  bool failed = false;
  std::unique_ptr<plume::RenderShader> vs;
  std::unique_ptr<plume::RenderShader> ps;
  std::unique_ptr<plume::RenderPipeline> triangles;
  std::unique_ptr<plume::RenderPipeline> lines;
  std::unique_ptr<plume::RenderTexture> white;
  std::unique_ptr<plume::RenderTextureView> white_view;
  uint32_t white_slot = kInvalidDescriptor;
#if defined(TIMESHIFT_RECOMP)
  std::unique_ptr<plume::RenderTexture> font;
  std::unique_ptr<plume::RenderTextureView> font_view;
  uint32_t font_slot = kInvalidDescriptor;
#endif
};
Resources &GetResources() {
  static Resources *resources = new Resources;
  return *resources;
}

std::unique_ptr<plume::RenderPipeline> CreateOverlayPipeline(plume::RenderDevice *device,
                                                             plume::RenderShader *vs,
                                                             plume::RenderShader *ps,
                                                             plume::RenderPrimitiveTopology topology) {
  const plume::RenderInputElement elements[] = {
      plume::RenderInputElement("POSITION", 0, 0, plume::RenderFormat::R32G32_FLOAT, 0, 0),
      plume::RenderInputElement("TEXCOORD", 0, 1, plume::RenderFormat::R32G32_FLOAT, 0, 8),
      plume::RenderInputElement("COLOR", 0, 2, plume::RenderFormat::R8G8B8A8_UNORM, 0, 16),
  };
  plume::RenderGraphicsPipelineDesc desc;
  desc.pipelineLayout = State().utility_layout.get();
  desc.vertexShader = vs;
  desc.pixelShader = ps;
  desc.primitiveTopology = topology;
  desc.cullMode = plume::RenderCullMode::NONE;
  desc.renderTargetCount = 1;
  desc.renderTargetFormat[0] = plume::RenderFormat::B8G8R8A8_UNORM;
  desc.renderTargetBlend[0] = plume::RenderBlendDesc::AlphaBlend();
  desc.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  desc.inputSlots = &kVertexSlot;
  desc.inputSlotsCount = 1;
  desc.inputElements = elements;
  desc.inputElementsCount = 3;
  return device->createGraphicsPipeline(desc);
}

// Creates a sampleable RGBA8 texture, records its upload and binds a slot.
bool CreateUploadedTexture(uint32_t width, uint32_t height, const uint8_t *rgba,
                           std::unique_ptr<plume::RenderTexture> &out_texture,
                           std::unique_ptr<plume::RenderTextureView> &out_view,
                           uint32_t &out_slot) {
  plume::RenderDevice *device = Video::Device();
  auto texture = device->createTexture(
      plume::RenderTextureDesc::Texture2D(width, height, 1, plume::RenderFormat::R8G8B8A8_UNORM));
  if (!texture)
    return false;
  texture->setName("renut overlay texture");
  auto view = texture->createTextureView(
      plume::RenderTextureViewDesc::Texture2D(plume::RenderFormat::R8G8B8A8_UNORM));
  if (!view)
    return false;
  if (!Video::UploadRGBA8Locked(texture.get(), width, height, rgba))
    return false;
  const uint32_t slot = Video::AllocateTextureSlotLocked();
  if (slot == kInvalidDescriptor)
    return false;
  Video::SetTextureSlotLocked(slot, texture.get(), view.get());
  out_texture = std::move(texture);
  out_view = std::move(view);
  out_slot = slot;
  return true;
}

#if defined(TIMESHIFT_RECOMP)
constexpr uint32_t kFontAtlasW = 128;
constexpr uint32_t kFontAtlasH = 64;
constexpr uint32_t kFontCellW = 8;
constexpr uint32_t kFontCellH = 16;

constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

int FontGlyphIndex(char ch) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'A' && ch <= 'Z')
    return 10 + (ch - 'A');
  if (ch >= 'a' && ch <= 'z')
    return 10 + (ch - 'a');
  switch (ch) {
  case '.': return 36;
  case ':': return 37;
  case '/': return 38;
  case '-': return 39;
  case '>': return 41;
  case '%': return 42;
  default: return 40;  // space
  }
}

void BuildFontAtlasRGBA8(std::vector<uint8_t> &rgba) {
  // 5x7 glyphs: 0-9, A-Z, '.', ':', '/', '-', ' ', '>', '%'
  static constexpr uint8_t kGlyphs[43][7] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
      {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
      {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 2
      {0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E}, // 3
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
      {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
      {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
      {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
      {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
      {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
      {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
      {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, // D
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
      {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
      {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
      {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
      {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
      {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
      {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
      {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
      {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
      {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
      {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
      {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}, // S
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, // W
      {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
      {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, // .
      {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, // :
      {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}, // /
      {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // -
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
      {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}, // >
      {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, // %
  };
  rgba.assign(size_t(kFontAtlasW) * kFontAtlasH * 4, 0);
  auto set_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= int(kFontAtlasW) || y >= int(kFontAtlasH))
      return;
    const size_t off = (size_t(y) * kFontAtlasW + size_t(x)) * 4;
    if (rgba[off + 3] < a || r == 255) {
      rgba[off + 0] = r;
      rgba[off + 1] = g;
      rgba[off + 2] = b;
      rgba[off + 3] = a;
    }
  };
  for (int idx = 0; idx < 43; ++idx) {
    const int cx = (idx % 16) * int(kFontCellW);
    const int cy = (idx / 16) * int(kFontCellH);
    for (int gy = 0; gy < 7; ++gy) {
      for (int gx = 0; gx < 5; ++gx) {
        if (!(kGlyphs[idx][gy] & (1u << (4 - gx))))
          continue;
        const int px = cx + 1 + gx;
        const int py = cy + 1 + gy;
        for (int oy = -1; oy <= 1; ++oy) {
          for (int ox = -1; ox <= 1; ++ox) {
            if (ox || oy)
              set_px(px + ox, py + oy, 0, 0, 0, 220);
          }
        }
        set_px(px, py, 255, 255, 255, 255);
      }
    }
  }
}

void AppendQuad(std::vector<rex::ui::ImmediateVertex> &verts, float x0, float y0, float x1, float y1,
                float u0, float v0, float u1, float v1, uint32_t color) {
  verts.push_back({x0, y0, u0, v0, color});
  verts.push_back({x1, y0, u1, v0, color});
  verts.push_back({x1, y1, u1, v1, color});
  verts.push_back({x0, y0, u0, v0, color});
  verts.push_back({x1, y1, u1, v1, color});
  verts.push_back({x0, y1, u0, v1, color});
}

void DrawFpsHudLocked(plume::RenderCommandList *cmd, plume::RenderFramebuffer *framebuffer,
                      uint32_t width, uint32_t height) {
  const bool show_hud = REXCVAR_GET(renut_fps_overlay);
  const bool show_menu = timeshift::telemetry::g_menu_open.load(std::memory_order_relaxed);
  if (!show_hud && !show_menu)
    return;
  Resources &r = GetResources();
  if (r.font_slot == kInvalidDescriptor || r.white_slot == kInvalidDescriptor)
    return;

  static std::vector<rex::ui::ImmediateVertex> bg_verts;
  static std::vector<rex::ui::ImmediateVertex> text_verts;
  bg_verts.clear();
  text_verts.clear();

  const float scale = width >= 1200u ? 2.0f : 1.0f;
  const float adv = 6.0f * scale;
  const float glyph_w = 8.0f * scale;
  const float glyph_h = 10.0f * scale;

  auto emit_str_at = [&](float x, float y, const char *str, size_t len, uint32_t col) {
    for (size_t i = 0; i < len; ++i) {
      const int idx = FontGlyphIndex(str[i]);
      const float u0 = float((idx % 16) * kFontCellW) / float(kFontAtlasW);
      const float v0 = float((idx / 16) * kFontCellH) / float(kFontAtlasH);
      const float u1 = float((idx % 16) * kFontCellW + 8) / float(kFontAtlasW);
      const float v1 = float((idx / 16) * kFontCellH + 10) / float(kFontAtlasH);
      AppendQuad(text_verts, x, y, x + glyph_w, y + glyph_h, u0, v0, u1, v1, col);
      x += adv;
    }
  };

  if (show_hud) {
    const uint32_t fps_x10 = timeshift::telemetry::g_hud_fps_x10.load(std::memory_order_relaxed);
    const uint32_t min_x10 = timeshift::telemetry::g_hud_min_fps_x10.load(std::memory_order_relaxed);
    const uint32_t ms_x10 = timeshift::telemetry::g_hud_frame_ms_x10.load(std::memory_order_relaxed);
    const uint32_t gpu_x10 = timeshift::telemetry::g_hud_gpu_ms_x10.load(std::memory_order_relaxed);

    char fps_buf[24];
    char detail_buf[48];
    std::snprintf(fps_buf, sizeof(fps_buf), "FPS %u.%u", fps_x10 / 10u, fps_x10 % 10u);
    std::snprintf(detail_buf, sizeof(detail_buf), "  MIN %u.%u  %u.%uMS  GPU %u.%uMS",
                  min_x10 / 10u, min_x10 % 10u, ms_x10 / 10u, ms_x10 % 10u,
                  gpu_x10 / 10u, gpu_x10 % 10u);

    const uint32_t fps_color = fps_x10 >= 285u ? PackRGBA(95, 255, 115, 255)
                             : fps_x10 >= 220u ? PackRGBA(255, 212, 71, 255)
                                               : PackRGBA(255, 84, 84, 255);
    const uint32_t detail_color = PackRGBA(232, 238, 246, 255);
    const uint32_t bg_color = PackRGBA(8, 12, 18, 190);

    const float pad_x = 8.0f * scale;
    const float pad_y = 4.0f * scale;
    const size_t fps_len = std::strlen(fps_buf);
    const size_t detail_len = std::strlen(detail_buf);
    const float text_w = float(fps_len + detail_len) * adv;
    const float box_w = text_w + 2.0f * pad_x;
    const float box_h = glyph_h + 2.0f * pad_y;
    const float box_x = std::floor((float(width) - box_w) * 0.5f);
    const float box_y = 8.0f;

    AppendQuad(bg_verts, box_x, box_y, box_x + box_w, box_y + box_h, 0.0f, 0.0f, 1.0f, 1.0f, bg_color);
    AppendQuad(bg_verts, box_x, box_y, box_x + box_w, box_y + 2.0f * scale, 0.0f, 0.0f, 1.0f, 1.0f, fps_color);
    emit_str_at(box_x + pad_x, box_y + pad_y, fps_buf, fps_len, fps_color);
    emit_str_at(box_x + pad_x + float(fps_len) * adv, box_y + pad_y, detail_buf, detail_len, detail_color);
  }

  if (show_menu) {
    const int cursor = timeshift::telemetry::g_menu_cursor.load(std::memory_order_relaxed);
    const uint64_t now_ms = static_cast<uint64_t>(timeshift::telemetry::TicksToMs(timeshift::telemetry::NowTicks()));
    const bool just_saved = timeshift::telemetry::g_menu_saved_until_ms.load(std::memory_order_relaxed) > now_ms;

    const std::string &af = REXCVAR_GET(renut_anisotropic_filtering);
    char af_upper[8] = {};
    for (size_t i = 0; i < af.size() && i < 7; ++i)
      af_upper[i] = (af[i] >= 'a' && af[i] <= 'z') ? char(af[i] - 32) : af[i];

    char dist_buf[8];
    const int32_t far_m = REXCVAR_GET(renut_far_plane_m);
    if (far_m <= 0)
      std::snprintf(dist_buf, sizeof(dist_buf), "MAX");
    else
      std::snprintf(dist_buf, sizeof(dist_buf), "%dM", far_m);

    const std::string &force_lvl = REXCVAR_GET(renut_force_level);
    const char *lvl_label = "OFF";
    bool lvl_active = !force_lvl.empty();
    if (lvl_active) {
      lvl_label = "CUSTOM";
      for (int i = 1; i < timeshift::telemetry::kCampaignLevelCount; ++i) {
        if (force_lvl == timeshift::telemetry::kCampaignLevels[i].pak) {
          lvl_label = timeshift::telemetry::kCampaignLevels[i].label;
          break;
        }
      }
    }

    struct Row { const char *label; const char *val; uint32_t vcol; };
    const uint32_t col_on = PackRGBA(95, 255, 115, 255);
    const uint32_t col_off = PackRGBA(255, 110, 110, 255);
    const uint32_t col_val = PackRGBA(120, 210, 255, 255);
    const uint32_t col_dim = PackRGBA(175, 185, 200, 255);
    const Row rows[10] = {
        {"FPS HUD", REXCVAR_GET(renut_fps_overlay) ? "ON" : "OFF", REXCVAR_GET(renut_fps_overlay) ? col_on : col_off},
        {"SHADOWS 1024", REXCVAR_GET(renut_skip_shadows) ? "OFF" : "ON", REXCVAR_GET(renut_skip_shadows) ? col_off : col_on},
        {"DRAW DIST", dist_buf, col_val},
        {"VSYNC", REXCVAR_GET(renut_vsync) ? "ON" : "OFF", REXCVAR_GET(renut_vsync) ? col_on : col_off},
        {"ANISO FILTER", af_upper[0] ? af_upper : "2X", col_val},
        {"SKIP D3D COPY", REXCVAR_GET(renut_skip_orig_d3d) ? "ON" : "OFF", REXCVAR_GET(renut_skip_orig_d3d) ? col_on : col_off},
        {"ZCULL RST", REXCVAR_GET(renut_deko3d_zcull) ? "ON" : "OFF", REXCVAR_GET(renut_deko3d_zcull) ? col_on : col_off},
        {"DEPTH CMP RST", REXCVAR_GET(renut_deko3d_depth_compression) ? "ON" : "OFF", REXCVAR_GET(renut_deko3d_depth_compression) ? col_on : col_off},
        {"LEVEL JUMP", lvl_label, lvl_active ? col_on : col_dim},
        {"SAVE CONFIG", just_saved ? "SAVED" : "APPLY", just_saved ? col_on : col_val},
    };

    const float row_h = 15.0f * scale;
    const float pad_x = 12.0f * scale;
    const float pad_y = 10.0f * scale;
    const float box_w = 240.0f * scale;
    const float box_h = pad_y * 2.0f + row_h * 12.2f;
    const float box_x = std::floor((float(width) - box_w) * 0.5f);
    const float box_y = std::floor((float(height) - box_h) * 0.5f);

    const uint32_t panel_bg = PackRGBA(10, 14, 22, 225);
    const uint32_t accent = PackRGBA(80, 175, 255, 255);
    const uint32_t sel_bg = PackRGBA(45, 85, 145, 210);
    const uint32_t col_white = PackRGBA(240, 245, 255, 255);

    AppendQuad(bg_verts, box_x, box_y, box_x + box_w, box_y + box_h, 0.0f, 0.0f, 1.0f, 1.0f, panel_bg);
    AppendQuad(bg_verts, box_x, box_y, box_x + box_w, box_y + 2.5f * scale, 0.0f, 0.0f, 1.0f, 1.0f, accent);

    const char *title = "RENUT GRAPHICS MENU";
    emit_str_at(box_x + pad_x, box_y + pad_y, title, std::strlen(title), accent);

    for (int i = 0; i < 10; ++i) {
      const float ry = box_y + pad_y + float(i + 1) * row_h + 2.0f * scale;
      if (i == cursor)
        AppendQuad(bg_verts, box_x + 4.0f * scale, ry - 2.0f * scale, box_x + box_w - 4.0f * scale,
                   ry + glyph_h + 2.0f * scale, 0.0f, 0.0f, 1.0f, 1.0f, sel_bg);
      char line_l[32];
      std::snprintf(line_l, sizeof(line_l), "%c %s", (i == cursor) ? '>' : ' ', rows[i].label);
      emit_str_at(box_x + pad_x, ry, line_l, std::strlen(line_l), (i == cursor) ? col_white : col_dim);
      const size_t vlen = std::strlen(rows[i].val);
      emit_str_at(box_x + box_w - pad_x - float(vlen) * adv, ry, rows[i].val, vlen, rows[i].vcol);
    }

    const char *hint = "DPAD:NAV  L/R/A:SET  B/PLUS:EXIT";
    emit_str_at(box_x + pad_x, box_y + box_h - pad_y - glyph_h, hint, std::strlen(hint), col_dim);
  }

  UploadRing &uploads = Video::UploadsLocked();
  const UploadAllocation bg_alloc =
      uploads.Upload(bg_verts.data(), sizeof(rex::ui::ImmediateVertex) * bg_verts.size(), 16);
  const UploadAllocation text_alloc =
      uploads.Upload(text_verts.data(), sizeof(rex::ui::ImmediateVertex) * text_verts.size(), 16);
  if (!bg_alloc || !text_alloc)
    return;

  cmd->setFramebuffer(framebuffer);
  cmd->setViewports(plume::RenderViewport(0.0f, 0.0f, float(width), float(height)));
  cmd->setScissors(plume::RenderRect(0, 0, int32_t(width), int32_t(height)));
  cmd->setGraphicsPipelineLayout(State().utility_layout.get());
  cmd->setGraphicsDescriptorSet(State().texture_set.get(), 0);
  cmd->setGraphicsDescriptorSet(State().sampler_set.get(), 1);
  cmd->setPipeline(r.triangles.get());

  UtilityPush push{};
  push.scale[0] = 2.0f / float(width);
  push.scale[1] = -2.0f / float(height);
  push.translate[0] = -1.0f;
  push.translate[1] = 1.0f;

  const plume::RenderVertexBufferView bg_view(bg_alloc.ref, bg_alloc.size);
  cmd->setVertexBuffers(0, &bg_view, 1, &kVertexSlot);
  push.texture_index = r.white_slot;
  push.sampler_index = kPointClampSamplerSlot;
  cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
  cmd->drawInstanced(uint32_t(bg_verts.size()), 1, 0, 0);

  const plume::RenderVertexBufferView text_view(text_alloc.ref, text_alloc.size);
  cmd->setVertexBuffers(0, &text_view, 1, &kVertexSlot);
  push.texture_index = r.font_slot;
  push.sampler_index = kPointClampSamplerSlot;
  cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
  cmd->drawInstanced(uint32_t(text_verts.size()), 1, 0, 0);
}
#endif

bool EnsureResourcesLocked() {
  Resources &r = GetResources();
  if (r.ready || r.failed)
    return r.ready;
  plume::RenderDevice *device = Video::Device();
  if (!device || !State().utility_layout)
    return false;
  r.failed = true;
  r.vs = CreateHostShader(device, RENUT_HOST_SHADER(overlay_vs), "overlay_vs");
  r.ps = CreateHostShader(device, RENUT_HOST_SHADER(overlay_ps), "overlay_ps");
  if (!r.vs || !r.ps)
    return false;
  r.triangles = CreateOverlayPipeline(device, r.vs.get(), r.ps.get(),
                                      plume::RenderPrimitiveTopology::TRIANGLE_LIST);
  r.lines = CreateOverlayPipeline(device, r.vs.get(), r.ps.get(),
                                  plume::RenderPrimitiveTopology::LINE_LIST);
  if (!r.triangles || !r.lines) {
    RGPU_ERROR("Overlay pipeline creation failed");
    return false;
  }
  // Untextured ImGui geometry samples a white texel.
  static const uint8_t kWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (!CreateUploadedTexture(1, 1, kWhite, r.white, r.white_view, r.white_slot)) {
    RGPU_ERROR("Overlay white texture creation failed");
    return false;
  }
#if defined(TIMESHIFT_RECOMP)
  std::vector<uint8_t> font_rgba;
  BuildFontAtlasRGBA8(font_rgba);
  if (!CreateUploadedTexture(kFontAtlasW, kFontAtlasH, font_rgba.data(), r.font, r.font_view, r.font_slot)) {
    RGPU_ERROR("Overlay FPS font texture creation failed");
    return false;
  }
#endif
  r.failed = false;
  r.ready = true;
  return true;
}

void ReleaseTexturesLocked() {
  std::vector<Released> released;
  {
    std::lock_guard lock(g_release_mutex);
    released.swap(g_released);
  }
  for (Released &r : released) {
    // Frame lists in flight may still sample it: retire rather than free.
    if (r.slot != kInvalidDescriptor)
      Video::FreeTextureSlotLocked(r.slot);
    Video::RetireLocked(std::move(r.view));
    Video::RetireLocked(std::move(r.texture));
  }
}

// The host texture of 'data', made from its pixels the first time; the
// white texel's slot when there is none.
uint32_t TextureSlotLocked(OverlayTextureData *data) {
  if (!data)
    return GetResources().white_slot;
  if (!data->texture && !data->failed) {
    data->failed = !CreateUploadedTexture(data->width, data->height, data->rgba.data(),
                                          data->texture, data->view, data->slot);
    data->rgba.clear();
    data->rgba.shrink_to_fit();
  }
  return data->failed ? GetResources().white_slot : data->slot;
}

} // namespace

OverlayTextureData::OverlayTextureData(uint32_t width, uint32_t height, uint32_t sampler_slot,
                                       const uint8_t *pixels)
    : width(width), height(height), sampler_slot(sampler_slot),
      rgba(pixels, pixels + size_t(width) * height * 4), slot(kInvalidDescriptor) {}

OverlayTextureData::~OverlayTextureData() {
  if (!texture && !view && slot == kInvalidDescriptor)
    return;
  std::lock_guard lock(g_release_mutex);
  g_released.push_back({std::move(texture), std::move(view), slot});
}

OverlayTexture::OverlayTexture(std::shared_ptr<OverlayTextureData> data)
    : rex::ui::ImmediateTexture(data->width, data->height), data_(std::move(data)) {}

OverlayDrawer::OverlayDrawer() = default;

OverlayDrawer::~OverlayDrawer() {
  std::lock_guard lock(g_frame_mutex);
  g_latest.reset();
}

std::unique_ptr<rex::ui::ImmediateTexture>
OverlayDrawer::CreateTexture(uint32_t width, uint32_t height, rex::ui::ImmediateTextureFilter filter,
                             bool is_repeated, const uint8_t *data) {
  (void)is_repeated;
  if (!data || width == 0 || height == 0)
    return nullptr;
  const uint32_t sampler = filter == rex::ui::ImmediateTextureFilter::kNearest
                               ? kPointClampSamplerSlot
                               : kLinearClampSamplerSlot;
  return std::make_unique<OverlayTexture>(
      std::make_shared<OverlayTextureData>(width, height, sampler, data));
}

void OverlayDrawer::Begin(rex::ui::UIDrawContext &ctx, float coord_width, float coord_height) {
  // The base resolves non-positive sizes; read them back through its accessors.
  rex::ui::ImmediateDrawer::Begin(ctx, coord_width, coord_height);
  recording_ = std::make_unique<Frame>();
  recording_->width = ctx.render_target_width();
  recording_->height = ctx.render_target_height();
  recording_->scale[0] = 2.0f / coordinate_space_width();
  recording_->scale[1] = -2.0f / coordinate_space_height();
  recording_->translate[0] = -1.0f;
  recording_->translate[1] = 1.0f;
}

void OverlayDrawer::BeginDrawBatch(const rex::ui::ImmediateDrawBatch &batch) {
  if (!recording_)
    return;
  Frame::Batch b;
  b.first_vertex = uint32_t(recording_->vertices.size());
  b.first_index = uint32_t(recording_->indices.size());
  if (batch.vertices && batch.vertex_count > 0) {
    recording_->vertices.insert(recording_->vertices.end(), batch.vertices,
                                batch.vertices + batch.vertex_count);
    b.vertex_count = uint32_t(batch.vertex_count);
  }
  if (batch.indices && batch.index_count > 0) {
    recording_->indices.insert(recording_->indices.end(), batch.indices,
                               batch.indices + batch.index_count);
    b.index_count = uint32_t(batch.index_count);
  }
  recording_->batches.push_back(b);
}

void OverlayDrawer::Draw(const rex::ui::ImmediateDraw &draw) {
  if (!recording_ || recording_->batches.empty() || draw.count <= 0 ||
      !recording_->batches.back().vertex_count)
    return;
  uint32_t left, top, width, height;
  if (!ScissorToRenderTarget(draw, left, top, width, height))
    return;
  Frame::DrawCall call;
  call.batch = uint32_t(recording_->batches.size() - 1);
  call.lines = draw.primitive_type == rex::ui::ImmediatePrimitiveType::kLines;
  call.count = uint32_t(draw.count);
  call.index_offset = uint32_t(draw.index_offset);
  call.base_vertex = draw.base_vertex;
  call.scissor = plume::RenderRect(int32_t(left), int32_t(top), int32_t(left + width),
                                   int32_t(top + height));
  if (draw.texture)
    call.texture = static_cast<OverlayTexture *>(draw.texture)->data();
  recording_->draws.push_back(std::move(call));
}

void OverlayDrawer::EndDrawBatch() {}

void OverlayDrawer::End() {
  std::shared_ptr<const Frame> frame(std::move(recording_));
  {
    std::lock_guard lock(g_frame_mutex);
    g_latest.swap(frame);
  }
  // The frame replaced (and any texture only it held) goes here, unlocked.
  frame.reset();
  ++g_frames_recorded;
  rex::ui::ImmediateDrawer::End();
}

uint64_t OverlayDrawer::FramesRecorded() {
  return g_frames_recorded;
}

bool OverlayDrawer::HasLatest() {
  std::lock_guard lock(g_frame_mutex);
  return g_latest && !g_latest->draws.empty();
}

void OverlayDrawer::ClearLatest() {
  std::shared_ptr<const Frame> frame;
  {
    std::lock_guard lock(g_frame_mutex);
    g_latest.swap(frame);
  }
}

void OverlayDrawer::DrawLatestLocked(plume::RenderCommandList *cmd,
                                     plume::RenderFramebuffer *framebuffer, uint32_t width,
                                     uint32_t height) {
  ReleaseTexturesLocked();
  std::shared_ptr<const Frame> frame;
  {
    std::lock_guard lock(g_frame_mutex);
    frame = g_latest;
  }
#if defined(TIMESHIFT_RECOMP)
  if (!cmd || !width || !height || !EnsureResourcesLocked())
    return;
  if (!frame || frame->draws.empty() || !frame->width || !frame->height) {
    DrawFpsHudLocked(cmd, framebuffer, width, height);
    return;
  }
#else
  if (!cmd || !frame || frame->draws.empty() || !frame->width || !frame->height || !width ||
      !height || !EnsureResourcesLocked())
    return;
#endif
  Resources &r = GetResources();

  cmd->setFramebuffer(framebuffer);
  cmd->setViewports(plume::RenderViewport(0.0f, 0.0f, float(width), float(height)));
  cmd->setGraphicsPipelineLayout(State().utility_layout.get());
  cmd->setGraphicsDescriptorSet(State().texture_set.get(), 0);
  cmd->setGraphicsDescriptorSet(State().sampler_set.get(), 1);

  // Scissors are in the pixels of the size the frame was recorded at; the
  // window may have been resized since.
  const float to_x = float(width) / float(frame->width);
  const float to_y = float(height) / float(frame->height);
  UploadRing &uploads = Video::UploadsLocked();
  uint32_t bound_batch = UINT32_MAX;
  bool batch_ok = false;
  bool indexed = false;
  for (const Frame::DrawCall &call : frame->draws) {
    if (call.batch != bound_batch) {
      bound_batch = call.batch;
      const Frame::Batch &b = frame->batches[call.batch];
      batch_ok = false;
      indexed = b.index_count > 0;
      const UploadAllocation vertices =
          uploads.Upload(frame->vertices.data() + b.first_vertex,
                         sizeof(rex::ui::ImmediateVertex) * uint64_t(b.vertex_count), 16);
      if (!vertices)
        continue;
      const plume::RenderVertexBufferView vertex_view(vertices.ref, vertices.size);
      cmd->setVertexBuffers(0, &vertex_view, 1, &kVertexSlot);
      if (indexed) {
        const UploadAllocation indices = uploads.Upload(
            frame->indices.data() + b.first_index, sizeof(uint16_t) * uint64_t(b.index_count), 16);
        if (!indices)
          continue;
        const plume::RenderIndexBufferView index_view(indices.ref, indices.size,
                                                      plume::RenderFormat::R16_UINT);
        cmd->setIndexBuffer(&index_view);
      }
      batch_ok = true;
    }
    if (!batch_ok)
      continue;

    const int32_t left = std::clamp(int32_t(float(call.scissor.left) * to_x), 0, int32_t(width));
    const int32_t top = std::clamp(int32_t(float(call.scissor.top) * to_y), 0, int32_t(height));
    const int32_t right =
        std::clamp(int32_t(float(call.scissor.right) * to_x + 0.5f), left, int32_t(width));
    const int32_t bottom =
        std::clamp(int32_t(float(call.scissor.bottom) * to_y + 0.5f), top, int32_t(height));
    if (right <= left || bottom <= top)
      continue;
    cmd->setScissors(plume::RenderRect(left, top, right, bottom));
    cmd->setPipeline(call.lines ? r.lines.get() : r.triangles.get());

    UtilityPush push{};
    push.scale[0] = frame->scale[0];
    push.scale[1] = frame->scale[1];
    push.translate[0] = frame->translate[0];
    push.translate[1] = frame->translate[1];
    push.texture_index = TextureSlotLocked(call.texture.get());
    push.sampler_index = call.texture ? call.texture->sampler_slot : kLinearClampSamplerSlot;
    cmd->setGraphicsPushConstants(0, &push, 0, sizeof(push));
    if (indexed)
      cmd->drawIndexedInstanced(call.count, 1, call.index_offset, call.base_vertex, 0);
    else
      cmd->drawInstanced(call.count, 1, uint32_t(call.base_vertex), 0);
  }
  cmd->setScissors(plume::RenderRect(0, 0, int32_t(width), int32_t(height)));
#if defined(TIMESHIFT_RECOMP)
  DrawFpsHudLocked(cmd, framebuffer, width, height);
#endif
}

} // namespace renut::gpu
