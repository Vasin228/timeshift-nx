import sys

path = 'upstream/sa2-nx/app/src/renut_engine/gpu/render/draw.cpp'
with open(path, 'rb') as f:
    content = f.read()

target = (b'  if (reuse_on)\r\n'
          b'    preuse = {hint.seq, PipelineLookupsLocked(), prebuild_epoch, pipeline};\r\n'
          b'  if (!pipeline || !BindTargets(targets))\r\n'
          b'    return false;')

replacement = (b'  if (reuse_on)\r\n'
               b'    preuse = {hint.seq, PipelineLookupsLocked(), prebuild_epoch, pipeline};\r\n'
               b'  const u32 dc = key.depth_control;\r\n'
               b'  const bool depth_used = (key.depth_format != plume::RenderFormat::UNKNOWN) &&\r\n'
               b'                          ((dc & 2) != 0 || (dc & 4) != 0 || (dc & 1) != 0);\r\n'
               b'  Targets draw_targets = targets;\r\n'
               b'  if (!depth_used) {\r\n'
               b'    draw_targets.depth = nullptr;\r\n'
               b'  }\r\n'
               b'  if (!pipeline || !BindTargets(draw_targets))\r\n'
               b'    return false;')

if target not in content:
    print('Target not found in draw.cpp')
    sys.exit(1)

content = content.replace(target, replacement, 1)
with open(path, 'wb') as f:
    f.write(content)
print('Patched draw.cpp successfully!')
