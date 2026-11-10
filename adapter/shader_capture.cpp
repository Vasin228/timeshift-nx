#include "shader_capture.h"
#include <algorithm>
#include <limits>

namespace timeshift::d3d6274 {
namespace {
uint32_t Word(std::span<const uint8_t> p, size_t offset) {
  return uint32_t(p[offset]) << 24 | uint32_t(p[offset+1]) << 16 |
         uint32_t(p[offset+2]) << 8 | p[offset+3];
}
bool Range(uint32_t va, size_t bytes) {
  return va && bytes && bytes <= (uint64_t{1} << 32) - va;
}
std::span<const uint8_t> Read(ShaderMemory &m, uint32_t va, size_t bytes, bool physical = false) {
  if (!Range(va, bytes)) return {};
  auto span = physical ? m.Physical(va, bytes) : m.Virtual(va, bytes);
  return span.size() == bytes ? span : std::span<const uint8_t>{};
}
}
CapturedShader CaptureShader(ShaderMemory &memory, uint32_t shader, uint32_t physical, bool pixel) {
  CapturedShader result; result.pixel = pixel;
  const uint32_t offset = pixel ? kPixelContainerOffset : kVertexContainerOffset;
  auto fail = [&](CaptureError error) { result.error = error; return result; };
  if (!Range(shader, size_t(offset) + 36)) return fail(CaptureError::Address);
  const uint32_t container_va = shader + offset;
  auto header = Read(memory, container_va, 36);
  if (header.empty()) return fail(CaptureError::Address);
  const uint32_t flags = Word(header, 0), virtual_bytes = Word(header, 4), physical_bytes = Word(header, 8);
  if ((flags & 0xFFFFFF00u) != 0x102A1100u || Word(header, 28) || Word(header, 32))
    return fail(CaptureError::Header);
  if (bool(flags & 1) == pixel) return fail(CaptureError::Stage);
  if (virtual_bytes < 36 || virtual_bytes > 0x10000 || !physical_bytes || physical_bytes > 0x100000)
    return fail(CaptureError::Extent);
  auto virt = Read(memory, container_va, virtual_bytes);
  if (virt.empty()) return fail(CaptureError::Address);
  const uint32_t ct = Word(header, 16), dt = Word(header, 20), code_header = Word(header, 24);
  if (ct >= virtual_bytes || dt >= virtual_bytes || code_header < 36 ||
      uint64_t(code_header) + 24 > virtual_bytes) return fail(CaptureError::Tables);
  const uint32_t code_offset = Word(virt, code_header), code_bytes = Word(virt, code_header+4);
  if (!code_bytes || code_bytes % 12 || uint64_t(code_offset) + code_bytes > physical_bytes)
    return fail(CaptureError::Microcode);
  if (!physical) {
    auto pointer = Read(memory, shader + (pixel ? kPixelPhysicalOffset : kVertexPhysicalOffset), 4);
    if (pointer.empty()) return fail(CaptureError::Address);
    physical = Word(pointer, 0);
  }
  // This is the same physical-vs-virtual pointer convention the upstream
  // registry uses. ShaderMemory keeps translation and accessible-range checks
  // in the platform adapter instead of guessing that every guest page exists.
  auto phys = Read(memory, physical, physical_bytes, physical < 0x20000000u);
  if (phys.empty()) return fail(CaptureError::Address);
  result.container.reserve(size_t(virtual_bytes) + physical_bytes);
  result.container.insert(result.container.end(), virt.begin(), virt.end());
  result.container.insert(result.container.end(), phys.begin(), phys.end());
  return result;
}
CapturedDeclaration CaptureDeclaration(ShaderMemory &memory, uint32_t elements) {
  CapturedDeclaration result;
  for (uint32_t i = 0; i <= 64; ++i) {
    if (!Range(elements, size_t(i+1) * kVertexElementBytes)) {
      result.error = CaptureError::Address; return result;
    }
    auto e = Read(memory, elements + i*kVertexElementBytes, kVertexElementBytes);
    if (e.empty()) { result.error = CaptureError::Address; return result; }
    const uint32_t stream = uint32_t(e[0]) << 8 | e[1];
    if (stream == 0xFF || Word(e, 4) == 0xFFFFFFFFu) return result;
    if (i == 64 || stream >= 16) { result.error = CaptureError::Declaration; return result; }
    result.elements.insert(result.elements.end(), e.begin(), e.end());
  }
  result.error = CaptureError::Declaration; return result;
}
}
