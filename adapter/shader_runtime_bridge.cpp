#include "shader_capture.h"
#include "shader_runtime_bridge.h"
#include <algorithm>
#include <atomic>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/resources.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/render/surfaces.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/log.h"

namespace timeshift::renderer {
namespace {
std::atomic<bool> g_coordinated_backend_active{true};
}

void SetCoordinatedBackendActive(bool active) {
  g_coordinated_backend_active.store(active, std::memory_order_release);
}

bool ResourceHooksEnabled() {
  return g_coordinated_backend_active.load(std::memory_order_acquire);
}
namespace {
class SdkShaderMemory final : public d3d6274::ShaderMemory {
 public:
  std::span<const uint8_t> Virtual(uint32_t address, size_t size) override {
    auto *memory = REX_KERNEL_MEMORY();
    if (!address || uint64_t(address) + size > uint64_t{1} << 32) return {};
    // A virtual span can cross committed regions with different attributes.
    // Validate every region before exposing the bytes to the capture helper.
    uint64_t at = address, end = uint64_t(address) + size;
    while (at < end) {
      auto *heap = memory->LookupHeap(uint32_t(at));
      rex::memory::HeapAllocationInfo info{};
      if (!heap || !heap->page_size()) return {};
      // This SDK reports a whole-page count from the queried page but echoes
      // the input as base_address. Query its page boundary, or a nonzero byte
      // offset would incorrectly extend the reported end into the next page.
      const uint32_t aligned = heap->heap_base() +
          uint32_t((at-heap->heap_base())/heap->page_size())*heap->page_size();
      if (!heap->QueryRegionInfo(aligned, &info) ||
          !(info.state & rex::memory::kMemoryAllocationCommit) ||
          !(info.protect & rex::memory::kMemoryProtectRead)) return {};
      const uint64_t next = uint64_t(info.base_address) + info.region_size;
      if (next <= at) return {};
      at = std::min(next, end);
    }
    return {memory->TranslateVirtual<const uint8_t *>(address), size};
  }
  std::span<const uint8_t> Physical(uint32_t address, size_t size) override {
    if (!address || uint64_t(address) + size > 0x20000000ull) return {};
    // The 64 KiB guest physical alias used by the renderer's physical uploads.
    auto checked = Virtual(0xA0000000u + address, size);
    return checked.empty() ? checked : std::span(renut::gpu::GuestPhysicalPtr<const uint8_t>(address), size);
  }
};
void Register(uint32_t shader, uint32_t physical, bool pixel) {
  SdkShaderMemory memory;
  auto copy = d3d6274::CaptureShader(memory, shader, physical, pixel);
  if (copy) renut::gpu::RegisterCopiedGuestShader(shader, copy.container, pixel);
  else {
    static std::atomic<unsigned> errors{0};
    if (errors.fetch_add(1) < 32)
      RGPU_ERROR("TimeShift {} shader {:08X}: capture rejected ({})", pixel ? "pixel" : "vertex", shader,
                 unsigned(copy.error));
  }
}
}
void AfterVertexRegistration(uint32_t shader, uint32_t physical) { Register(shader, physical, false); }
void AfterPixelCreation(uint32_t shader) { Register(shader, 0, true); }
void AfterDeclarationCreation(uint32_t declaration, uint32_t elements) {
  SdkShaderMemory memory;
  auto copy = d3d6274::CaptureDeclaration(memory, elements);
  if (copy) renut::gpu::RegisterCopiedGuestDeclaration(declaration, copy.elements);
  else RGPU_ERROR("TimeShift declaration {:08X}: capture rejected ({})", declaration, unsigned(copy.error));
}
void BeforeResourceDestroy(uint32_t resource) {
  using namespace renut::gpu;
  // BeginVertices may still hold scratch owned by the resource about to die.
  FlushDeferredVertices(Video::GuestDevice());
  render_thread::Queue([resource] {
    ReleaseHostSurfaceLocked(resource);
    ReleaseHostTextureLocked(resource);
    ReleaseGuestShaderLocked(resource);
    ReleaseGuestDeclarationLocked(resource);
  });
  // The hook then executes the original destructor, including its fence waits.
}
}
