"""Expose the pinned host registry without installing another title's hooks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / 'upstream/sa2-nx/app/src/renut_engine/gpu/d3d/hooks/shader.cpp'
original = path.read_text()
text = original
marker = '// void XGRegisterVertexShader(D3DVertexShader*, void *pPhysicalPart) [sub_8255E658; N&B 0x8264E7B8]'
if '// TimeShift copied-container bridge.' not in text:
    assert text.count(marker) == 1
    bridge = '''// TimeShift copied-container bridge. Original6274 constructors/registrars
// own the guest objects; the queue receives only immutable identity/metadata.
#if defined(TIMESHIFT_RECOMP)
namespace renut::gpu {
void RegisterCopiedGuestShader(uint32_t shader_va, std::span<const uint8_t> container, bool pixel) {
  if (container.size() < sizeof(d3d::ShaderContainer)) return;
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container.data());
  if (uint64_t(u32(header->virtualSize)) + u32(header->physicalSize) != container.size()) return;
  const uint64_t hash = HashShaderContainer(container.data());
  const RecordKind kind = pixel ? RecordKind::PixelShader : RecordKind::VertexShader;
  if (!FindShaderEntry(hash)) CaptureMissingShader(container.data(), pixel, hash);
  render_thread::Queue([shader_va, hash, kind] {
    auto &shader = g_guest_shaders[shader_va];
    if (shader && shader->hash == hash && shader->kind == kind) return;
    ForgetShader(shader_va);
    shader = std::make_unique<ShaderRecord>();
    shader->kind = kind;
    shader->self_va = shader_va;
    shader->hash = hash;
    shader->entry = FindShaderEntry(hash);
    PipelineShaderLoaded(shader->entry);
  });
}
void RegisterCopiedGuestDeclaration(uint32_t declaration_va, std::span<const uint8_t> elements) {
  if (elements.size() % sizeof(GuestVertexElement) || elements.size() > 64*sizeof(GuestVertexElement)) return;
  auto declaration = std::make_shared<VertexDeclarationRecord>();
  declaration->x360.resource.Common = u32(d3d::ResourceType::VertexDeclaration);
  declaration->self_va = declaration_va;
  declaration->elements.resize(elements.size()/sizeof(GuestVertexElement));
  if (!elements.empty()) std::memcpy(declaration->elements.data(), elements.data(), elements.size());
  BuildDeclarationLayout(*declaration);
  render_thread::Queue([declaration_va, declaration] {
    ForgetVertexDeclarationLocked(declaration_va);
    g_guest_declarations[declaration_va] = std::make_unique<VertexDeclarationRecord>(*declaration);
  });
}
}
#else
'''
    text = text.replace('#include <cstring>', '#include <cstring>\n#include <span>')
    text = text.replace(marker, bridge + marker) + '\n#endif  // TIMESHIFT_RECOMP: no foreign title hooks.\n'
if text != original:
    path.write_text(text, newline='\n')
memory=ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/guest/guest_memory.h'
original=memory.read_text()
old='#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))'
new='''#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__)) || \\
    (defined(TIMESHIFT_RECOMP) && REX_PLATFORM_SWITCH)
  // The frozen TimeShift Switch SDK has a 4 KiB host offset for the
  // 0xE0000000 physical alias. Use its translator, not a flat VA sum.'''
if new not in original:
    assert original.count(old)==1
    memory.write_text(original.replace(old,new),newline='\n')
print('Copied-container registry enabled; TimeShift Switch memory uses SDK translation; foreign shader hooks excluded')
