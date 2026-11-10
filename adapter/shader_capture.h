#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace timeshift::d3d6274 {
inline constexpr uint32_t kVertexContainerOffset = 872;
inline constexpr uint32_t kVertexPhysicalOffset = 32;
inline constexpr uint32_t kPixelContainerOffset = 40;
inline constexpr uint32_t kPixelPhysicalOffset = 24;
inline constexpr uint32_t kVertexElementBytes = 12;

class ShaderMemory {
 public:
  virtual ~ShaderMemory() = default;
  virtual std::span<const uint8_t> Virtual(uint32_t address, size_t bytes) = 0;
  virtual std::span<const uint8_t> Physical(uint32_t address, size_t bytes) = 0;
};
enum class CaptureError {
  None, Address, Header, Stage, Extent, Tables, Microcode, Declaration,
};
struct CapturedShader {
  CaptureError error = CaptureError::None;
  std::vector<uint8_t> container;
  bool pixel = false;
  explicit operator bool() const { return error == CaptureError::None && !container.empty(); }
};
// Copies the virtual and physical parts before the game may reuse them. A zero
// physical argument reads the object's stored pointer (pixel creation path).
CapturedShader CaptureShader(ShaderMemory &memory, uint32_t shader, uint32_t physical, bool pixel);

struct CapturedDeclaration {
  CaptureError error = CaptureError::None;
  std::vector<uint8_t> elements;  // complete12-byte records, no terminator
  explicit operator bool() const { return error == CaptureError::None; }
};
CapturedDeclaration CaptureDeclaration(ShaderMemory &memory, uint32_t elements);
}
