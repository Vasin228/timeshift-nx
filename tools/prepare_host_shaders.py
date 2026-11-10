"""Build all ReNut utility shaders for the isolated Switch object probe."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UP = ROOT / 'upstream/sa2-nx'
sys.path.insert(0, str(UP / 'tools'))
import deko3d_glsl

out = ROOT / 'build/renut-switch/host_shaders'
out.mkdir(exist_ok=True, parents=True)
dxc = ROOT / 'build/deps/dxc-1.8.2505.1/bin/dxc'
cross = ROOT / 'build/spirv-cross/spirv-cross'
uam = ROOT / 'build/uam-host/uam'
env = dict(os.environ, LD_LIBRARY_PATH=str(dxc.parent.parent / 'lib'))
stages = {'vs': ('vert', 0), 'gs': ('geom', 3), 'ps': ('frag', 4), 'cs': ('comp', 5)}
stems = ['overlay_vs', 'overlay_ps', 'present_vs', 'present_ps', 'depth_resolve_ps',
         'depth_resolve_ms_ps', 'texture_mip_ps', 'resolve_copy_ps', 'rectangle_list_gs',
         'point_sprite_gs', 'stream_convert_cs', 'target_readback_cs']
report = []
def run(args):
    r = subprocess.run([str(a) for a in args], capture_output=True, text=True, env=env)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return r.stdout + r.stderr
with tempfile.TemporaryDirectory() as work:
    work = Path(work)
    for stem in stems:
        kind = stem.rsplit('_', 1)[1]
        stage, stage_number = stages[kind]
        src = UP / 'app/src/renut_engine/gpu/shaders/host' / (stem + '.hlsl')
        args = [dxc, '-T', kind + '_6_0', '-E', 'main', '-HV', '2021', '-all-resources-bound',
                '-Wno-ignored-attributes', '-spirv', '-fvk-use-dx-layout', '-DTIMESHIFT_RECOMP']
        # Preserve the upstream Vulkan fallback shader's conventions too.
        vkargs = args + (['-fvk-invert-y'] if kind == 'vs' else [])
        run(vkargs + ['-Fh', out / (stem + '.spirv.h'), '-Vn', 'g_' + stem + '_spirv', src])
        spv, vk, glsl_path, dksh = [work / (stem + suffix) for suffix in ['.spv', '.vk.glsl', '.glsl', '.dksh']]
        run(args + ['-DBANJO_DEKO3D', '-Fo', spv, src])
        run([cross, spv, '--version', '460', '--vulkan-semantics', '--relax-nan-checks', '--output', vk])
        glsl, _ = deko3d_glsl.convert(vk.read_text(), stage, deko3d_glsl.DEFAULT_UBO_MAP)
        glsl_path.write_text(glsl)
        output = run([uam, '-s', stage, '-o', dksh, glsl_path])
        if 'warning' in output.lower():
            raise RuntimeError(stem + ': ' + output)
        code, text = dksh.read_bytes(), glsl.encode()
        assert code[:4] == b'DKSH'
        blob = struct.pack('<IIII', 0x334B4442, stage_number, len(text), len(code)) + text + code
        header = '// Private generated host shader; TIMESHIFT_RECOMP.\n#pragma once\n#include <cstdint>\n'
        header += 'static const uint8_t g_' + stem + '_deko3d[] = {\n'
        header += '\n'.join('  ' + ','.join(f'0x{b:02X}' for b in blob[i:i+24]) + ',' for i in range(0, len(blob), 24))
        (out / (stem + '.deko3d.h')).write_text(header + '\n};\n')
        report.append(dict(stem=stem, dksh_bytes=len(code), glsl_bytes=len(text),
                           dksh_sha256=hashlib.sha256(code).hexdigest()))
(ROOT / 'analysis/host-shader-build.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: all', len(report), 'host shaders compiled for SPIR-V and deko3d; no GPU execution')
