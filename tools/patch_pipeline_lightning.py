import sys

path = 'upstream/sa2-nx/app/src/renut_engine/gpu/render/pipeline.cpp'
with open(path, 'rb') as f:
    content = f.read()

target = (b'    const u32 zfunc = (dc >> 4) & 7;\n'
          b'    desc.depthFunction = ConvertCompareFunction(zfunc);')

replacement = (b'    const u32 zfunc = (dc >> 4) & 7;\n'
               b'    if (desc.depthEnabled && !desc.depthWriteEnabled && zfunc == 7 && !cull_front) {\n'
               b'      desc.depthFunction = plume::RenderComparisonFunction::LESS_EQUAL;\n'
               b'    } else {\n'
               b'      desc.depthFunction = ConvertCompareFunction(zfunc);\n'
               b'    }')

if target not in content:
    print('Target not found in pipeline.cpp')
    sys.exit(1)

content = content.replace(target, replacement, 1)
with open(path, 'wb') as f:
    f.write(content)
print('Patched pipeline.cpp successfully!')
