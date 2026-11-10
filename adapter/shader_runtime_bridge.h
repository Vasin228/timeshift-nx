#pragma once
#include <cstdint>
#include <span>

namespace timeshift::renderer {
// Every hook currently remains dormant in production. The full backend's
// startup coordinator must explicitly replace this gate after all mandatory
// device/draw/tiling/fence/presentation paths exist. There is no auto-enabling
// preference or console file associated with this partial integration.
bool ResourceHooksEnabled();
void SetCoordinatedBackendActive(bool active);
void AfterVertexRegistration(uint32_t shader, uint32_t physical);
void AfterPixelCreation(uint32_t shader);
void AfterDeclarationCreation(uint32_t declaration, uint32_t elements);
void BeforeResourceDestroy(uint32_t resource);
}
namespace renut::gpu {
// Game-thread copies only. Host maps/modules are updated later in queue order.
void RegisterCopiedGuestShader(uint32_t shader, std::span<const uint8_t> container, bool pixel);
void RegisterCopiedGuestDeclaration(uint32_t declaration, std::span<const uint8_t> elements);
}
