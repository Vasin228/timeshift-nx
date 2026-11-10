"""Compile and run adversarial concurrency stress test with AddressSanitizer and UndefinedBehaviorSanitizer."""
from pathlib import Path
import subprocess, sys, shutil
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
assert (ROOT / 'analysis/source-fixture/completion.inc').is_file()
sanitize = '--sanitize' in sys.argv
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
exe = ROOT / 'build' / ('adversarial-stress-sanitized' if sanitize else 'adversarial-stress')
includes = [
    'adapter', 'tests', 'analysis/source-fixture', 'analysis/bootstrap-guest-fixture',
    'private/app/generated/default', 'private/sdk/include', 'upstream/sa2-nx/app/src'
]
external = '/root/lab/rexglue-sdk/thirdparty/'
includes += [external + n for n in ['simde', 'fmt/include', 'spdlog/include', 'o1heap/o1heap']]
sources = [
    'tests/adversarial_concurrency_stress.cpp', 'adapter/device_registry.cpp', 'adapter/device_bootstrap.cpp',
    'adapter/device_guest_calls.cpp', 'adapter/source_memory.cpp', 'adapter/device_lifecycle_hooks.cpp',
    'adapter/callback_hooks.cpp'
]
command = ['clang++', '-std=c++23', '-O1', '-g', '-pthread', '-DFMT_HEADER_ONLY', *flags]
command += ['-I' + str(ROOT / p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir = ROOT / 'build/host-adversarial' / exe.name
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

print("Compiling adversarial stress harness...")
with ThreadPoolExecutor(max_workers=3) as pool:
    objects = list(pool.map(compile_source, sources))

print("Linking executable...")
subprocess.run(['clang++', '-pthread', *flags, *objects, '-o', str(exe)], check=True)

print("Executing adversarial stress test under ASan/UBSan...")
result = subprocess.run([str(exe)], capture_output=True, text=True)
print(result.stdout, end='')
print(result.stderr, end='')
assert result.returncode == 0, f"Exited with code {result.returncode}"
print("SUCCESS: Adversarial concurrency challenge completed cleanly!")
