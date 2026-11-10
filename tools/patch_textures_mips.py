import sys

path = 'upstream/sa2-nx/app/src/renut_engine/gpu/render/textures.cpp'
with open(path, 'rb') as f:
    content = f.read()

target1 = b'  const bool has_mips = (fetch.MipPage() != 0 || (fetch.PackedMips() && width <= 16 && height <= 16));\n'
replacement1 = b'  const bool has_mips = (fetch.MipPage() != 0 || (fetch.PackedMips() && (width <= 16 || height <= 16)));\n'

target2 = b'  desc.mipLODBias = std::clamp(lod_bias - 0.5f, -2.0f, 0.0f);\n'
replacement2 = b'  desc.mipLODBias = std::clamp(lod_bias - 1.0f, -2.0f, 0.0f);\n'

if target1 not in content:
    print('Target 1 not found')
    sys.exit(1)
if target2 not in content:
    print('Target 2 not found')
    sys.exit(1)

content = content.replace(target1, replacement1, 1)
content = content.replace(target2, replacement2, 1)

with open(path, 'wb') as f:
    f.write(content)
print('Patched textures.cpp successfully!')
