"""Compile and execute Part 216 empirical challenger verification fixture under ASan/UBSan."""
from pathlib import Path
import subprocess, sys, json, shutil, platform

ROOT = Path(__file__).resolve().parents[1]

# If invoked on Windows without clang++, delegate to WSL environment where toolchain is installed
if platform.system() == 'Windows' and shutil.which('clang++') is None:
    wsl_root = '/mnt/' + str(ROOT)[0].lower() + str(ROOT)[2:].replace('\\', '/')
    args_str = ' '.join(sys.argv[1:])
    cmd = ['wsl', '-d', 'Ubuntu-24.04', 'bash', '-c', f'cd {wsl_root} && python3 tools/check_challenger_part216.py {args_str}']
    sys.exit(subprocess.run(cmd).returncode)

sanitize = '--sanitize' in sys.argv or True  # Default to sanitized testing
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
name = 'challenger-part216' + ('-sanitized' if sanitize else '-fixture')
exe = ROOT / 'build' / name

includes = [
    'private/sdk/include',
    'upstream/sa2-nx/app/src',
    'upstream/sa2-nx/app/thirdparty/plume',
    'upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/thirdparty/xxHash'
]

sources = [
    'tests/challenger_part216_fixture.cpp',
    'upstream/sa2-nx/app/src/renut_engine/gpu/render/format.cpp'
]

cmd = ['clang++', '-std=c++23', '-O1', '-g', '-pthread', *flags]
cmd += ['-I' + str(ROOT / p if not p.startswith('/') else Path(p)) for p in includes]

build_dir = ROOT / 'build'
build_dir.mkdir(parents=True, exist_ok=True)

compile_cmd = cmd + [str(ROOT / s) for s in sources] + ['-o', str(exe)]
subprocess.run(compile_cmd, check=True)

r = subprocess.run([str(exe)], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='', file=sys.stderr)
assert r.returncode == 0, f"Exit code: {r.returncode}"

out = ROOT / 'analysis/challenger-part216-validation'
out.mkdir(parents=True, exist_ok=True)
(out / ('sanitized.json' if sanitize else 'normal.json')).write_text(json.dumps(dict(
    success=True,
    sanitizers=sanitize,
    tests=[
        "LodBias full 10-bit signed range (-512..+511) with aniso bits 10/11",
        "desc.minLOD == 0.0f Mip 0 sampling across streamer states",
        "D3D9 MaxMipLevel inversion handling (lodClampMin <= lodClampMax)",
        "Track 3 lightning occlusion depth test pipeline states",
        "Track 3 fragment depth rejection oracle (wall occlusion vs sky)",
        "Hardware sampler shadow comparison state transitions"
    ],
    status="ALL_PASS"
), indent=2) + '\n')
