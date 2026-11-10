"""Compile draw and resource lease fixture and native hook interposition with sanitizers."""
from pathlib import Path
import subprocess, sys, json, shutil, os, platform
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]

# If invoked on Windows without clang++, delegate to WSL environment where toolchain is installed
if platform.system() == 'Windows' and shutil.which('clang++') is None:
    wsl_root = '/mnt/' + str(ROOT)[0].lower() + str(ROOT)[2:].replace('\\', '/')
    args_str = ' '.join(sys.argv[1:])
    cmd = ['wsl', 'bash', '-c', f'cd {wsl_root} && python3 tools/check_draw_leases.py {args_str}']
    sys.exit(subprocess.run(cmd).returncode)

sanitize = '--sanitize' in sys.argv
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
exe = ROOT / 'build' / ('draw-lease-sanitized' if sanitize else 'draw-lease-fixture')
includes = [
    'adapter', 'tests', 'analysis/source-fixture', 'analysis/bootstrap-guest-fixture',
    'analysis/draw-fixture', 'private/app/generated/default', 'private/sdk/include', 'upstream/sa2-nx/app/src'
]
external = '/root/lab/rexglue-sdk/thirdparty/'
includes += [external + n for n in ['simde', 'fmt/include', 'spdlog/include', 'o1heap/o1heap']]
sources = [
    'tests/draw_lease_fixture.cpp', 'adapter/draw_hooks.cpp', 'adapter/shader_resource_hooks.cpp',
    'adapter/device_registry.cpp', 'adapter/device_bootstrap.cpp',
    'adapter/device_guest_calls.cpp', 'adapter/source_memory.cpp', 'adapter/device_lifecycle_hooks.cpp'
]
command = ['clang++', '-std=c++23', '-O1', '-g', '-pthread', '-DFMT_HEADER_ONLY', *flags]
command += ['-I' + str(ROOT / p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir = ROOT / 'build/host-draw-leases' / exe.name
objects_dir.mkdir(parents=True, exist_ok=True)

def compile_source(source):
    obj = objects_dir / (Path(source).stem + '.o')
    prefix = [shutil.which('ccache')] if shutil.which('ccache') else []
    result = subprocess.run(prefix + command + ['-c', str(ROOT / source), '-o', str(obj)], capture_output=True, text=True)
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
    return str(obj)

with ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(compile_source, sources))

subprocess.run(['clang++', '-pthread', *flags, *objects, '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True)
print(result.stdout, end='')
print(result.stderr, end='')
assert result.returncode == 0, result.returncode

out = ROOT / 'analysis/draw-lease-validation'
out.mkdir(exist_ok=True)
(out / ('sanitized.json' if sanitize else 'normal.json')).write_text(json.dumps(dict(
    success=True, sanitizers=sanitize,
    scenarios=5,
    leased_root_draws=True,
    unowned_fallback=True,
    nested_up_begin_single_queueing=True,
    reentrant_release_quarantine=True,
    resource_destroy_scratch_flush_under_lease=True,
    actual_gpu_or_kernel=False
), indent=2) + '\n')
