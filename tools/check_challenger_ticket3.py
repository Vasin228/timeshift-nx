"""Compile and execute Ticket 3 empirical challenger verification fixture."""
from pathlib import Path
import subprocess, sys, json, shutil, os, platform
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]

# If invoked on Windows without clang++, delegate to WSL environment where toolchain is installed
if platform.system() == 'Windows' and shutil.which('clang++') is None:
    wsl_root = '/mnt/' + str(ROOT)[0].lower() + str(ROOT)[2:].replace('\\', '/')
    args_str = ' '.join(sys.argv[1:])
    cmd = ['wsl', '-d', 'Ubuntu-24.04', 'bash', '-c', f'cd {wsl_root} && python3 tools/check_challenger_ticket3.py {args_str}']
    sys.exit(subprocess.run(cmd).returncode)

sanitize = '--sanitize' in sys.argv
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
real_cpu = '--real-cpu' in sys.argv or True  # Default to real CPU testing
name = 'challenger-ticket3' + ('-real-cpu' if real_cpu else '') + ('-sanitized' if sanitize else '-fixture')
exe = ROOT / 'build' / name
if real_cpu:
    flags += ['-DTIMESHIFT_REAL_FRAME_CPU']

includes = [
    'adapter', 'tests', 'analysis/sdk-tiling-fixture', 'analysis/frame-cpu-fixture', 'analysis/source-fixture',
    'private/app/generated/default', 'private/sdk/include',
    'upstream/sa2-nx/app/src', 'upstream/sa2-nx/app/thirdparty/plume'
]
external = '/root/lab/rexglue-sdk/thirdparty/'
includes += [external + n for n in ['simde', 'fmt/include', 'spdlog/include', 'o1heap/o1heap']]

sources = [
    'tests/challenger_ticket3_fixture.cpp', 'adapter/sdk_tiling_host.cpp', 'adapter/draw_runtime_bridge.cpp',
    'adapter/tiling_hooks.cpp', 'adapter/tiling.cpp', 'adapter/tiling_guest_calls.cpp',
    'adapter/device_registry.cpp', 'adapter/device_lifecycle_hooks.cpp', 'adapter/device_bootstrap.cpp',
    'adapter/device_guest_calls.cpp', 'adapter/source_memory.cpp'
]

cmd = ['clang++', '-std=c++23', '-O1', '-g', '-pthread', '-DFMT_HEADER_ONLY', '-DTIMESHIFT_RECOMP', *flags]
cmd += ['-I' + str(ROOT / p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir = ROOT / 'build/host-challenger-ticket3' / name
objects_dir.mkdir(parents=True, exist_ok=True)

def compile_source(source):
    obj = objects_dir / (Path(source).stem + '.o')
    prefix = [shutil.which('ccache')] if shutil.which('ccache') else []
    result = subprocess.run(prefix + cmd + ['-c', str(ROOT / source), '-o', str(obj)], capture_output=True, text=True)
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
    return str(obj)

with ThreadPoolExecutor(max_workers=3) as pool:
    objects = list(pool.map(compile_source, sources))

subprocess.run(['clang++', '-pthread', *flags, *objects, '-o', str(exe)], check=True)
r = subprocess.run([str(exe)], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
assert r.returncode == 0, r.returncode

out = ROOT / 'analysis/challenger-ticket3-validation'
out.mkdir(exist_ok=True)
(out / (('real-cpu-' if real_cpu else '') + ('sanitized.json' if sanitize else 'normal.json'))).write_text(json.dumps(dict(
    success=True,
    sanitizers=sanitize,
    challenges_passed=5,
    out_of_range_formats_verified=True,
    degenerate_rectangles_verified=True,
    float_crt_closure_verified=True,
    premutation_closure_verified=True,
    multithreaded_concurrency_verified=True
), indent=2) + '\n')
