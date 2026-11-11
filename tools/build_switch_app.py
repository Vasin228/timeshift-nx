#!/usr/bin/env python3
"""Build and audit the TimeShift Switch application candidate (timeshift.elf / timeshift.nro)."""

from pathlib import Path
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD_CORE = ROOT / 'build/core-switch'
BUILD_APP = ROOT / 'build/app-switch'
ANALYSIS = ROOT / 'analysis'

DEVKITPRO = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
os.environ['DEVKITPRO'] = str(DEVKITPRO)
os.environ['DEVKITA64'] = str(DEVKITPRO / 'devkitA64')
AARCH64_CXX = DEVKITPRO / 'devkitA64/bin/aarch64-none-elf-g++'
AARCH64_NM = DEVKITPRO / 'devkitA64/bin/aarch64-none-elf-nm'
ELF2NRO = DEVKITPRO / 'tools/bin/elf2nro'

DEFAULT_NACB = Path('/root/lab/switch-build-app/timeshift_nro.default.nacp')
DEFAULT_ICON = DEVKITPRO / 'libnx/default_icon.jpg'

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def compile_app_units():
    # Ensure core-switch objects are up to date
    print("Checking core-switch build with ninja...")
    core_res = subprocess.run(['ninja', '-C', str(BUILD_CORE)], capture_output=True, text=True)
    if core_res.returncode != 0:
        print("Error building core-switch:", file=sys.stderr)
        print(core_res.stderr, file=sys.stderr)
        sys.exit(1)

    BUILD_APP.mkdir(parents=True, exist_ok=True)
    
    # Common flags from core-switch
    flags = [
        str(AARCH64_CXX),
        '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft',
        '-ftls-model=local-exec', '-ffunction-sections', '-fdata-sections',
        '-D__SWITCH__', '-g', '-O2', '-DNDEBUG', '-std=gnu++23', '-fPIE',
        '-DFMT_HEADER_ONLY', '-DNX', '-DPLUME_DEKO3D=1', '-DRENUT_RECOMP',
        '-DREXGLUE_PLATFORM_SWITCH=1', '-DSA2_SWITCH_NO_NVK=1',
        '-DSPDLOG_FMT_EXTERNAL', '-DTIMESHIFT_RECOMP', '-DVK_USE_PLATFORM_VI_NN',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/thirdparty/xxHash',
        f'-I{ROOT}/adapter',
        f'-I{ROOT}/upstream/sa2-nx/app/src',
        f'-I{ROOT}/upstream/sa2-nx/app/src/renut_engine/gpu/shaders',
        f'-I{ROOT}/build/renut-switch',
        f'-I{ROOT}/private/sdk/include',
        f'-I{ROOT}/upstream/sa2-nx/app/src/renut_engine/gpu/../sa2_compat',
        f'-I{ROOT}/private/sdk/src/core',
        '-I/root/lab/rexglue-sdk/thirdparty/simde',
        '-I/root/lab/rexglue-sdk/thirdparty/fmt/include',
        '-I/root/lab/rexglue-sdk/thirdparty/spdlog/include',
        '-I/root/lab/rexglue-sdk/thirdparty/imgui',
        '-I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/XenosRecomp',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/plume',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/plume/contrib/volk',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/plume/contrib/Vulkan-Headers/include',
        f'-I{ROOT}/upstream/sa2-nx/app/thirdparty/plume/contrib/VulkanMemoryAllocator/include',
        '-I/opt/nvk-switch/include',
        '-I/opt/devkitpro/portlibs/switch/include',
        '-I/root/lab/rexglue-sdk/include',
        '-I/root/lab/switch-build-app/rexglue-sdk/include',
        f'-I{ROOT}/private/app',
        f'-I{ROOT}/private/app/src',
        f'-I{ROOT}/private/app/generated/default',
        '-isystem', f'{DEVKITPRO}/libnx/include',
    ]

    units = [
        (ROOT / 'adapter/timeshift_app.cpp', BUILD_APP / 'timeshift_app.o'),
        (ROOT / 'adapter/renut_tuning_shims.cpp', BUILD_APP / 'renut_tuning_shims.o'),
        (ROOT / 'private/app/generated/default/timeshift_thunk_adjustors.cpp', BUILD_APP / 'timeshift_thunk_adjustors.o'),
        (ROOT / 'private/app/generated/default/timeshift_register.cpp', BUILD_APP / 'timeshift_register.o'),
        (ROOT / 'private/app/generated/default/timeshift_init.cpp', BUILD_APP / 'timeshift_init.o'),
        (ROOT / 'private/sdk/src/ui/switch/rex_app_switch.cpp', BUILD_APP / 'rex_app_switch.o'),
    ]

    for src, obj in units:
        if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime:
            print(f"Up to date: {obj.name}")
            continue
        cmd = [*flags, '-c', str(src), '-o', str(obj)]
        print(f"Compiling {src.name} -> {obj.name}...")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Error compiling {src}:", file=sys.stderr)
            print(res.stderr, file=sys.stderr)
            sys.exit(1)
        assert obj.exists(), f"Object {obj} not generated"

def collect_objects():
    # 1. 159 recomp objects from switch-build-app (excluding 2, 78, 153 and replacing register.o)
    recomp_dir = Path('/root/lab/switch-build-app/CMakeFiles/timeshift_recomp.dir/generated/default')
    recomp_objs = sorted(recomp_dir.glob('*.o'))
    assert len(recomp_objs) == 163, f"Expected 163 recomp objects, found {len(recomp_objs)}"
    
    excluded_names = {'timeshift_recomp.2.o', 'timeshift_recomp.78.o', 'timeshift_recomp.153.o', 'timeshift_register.o', 'timeshift_init.o'}
    retained_recomp = [p for p in recomp_objs if p.name not in excluded_names]
    assert len(retained_recomp) == 158, f"Expected 158 retained recomp objects, found {len(retained_recomp)}"

    # 2. Extract objects from compile_commands.json in core-switch
    compile_cmds = json.loads((BUILD_CORE / 'compile_commands.json').read_text())
    
    def get_objs_for_target(target: str, expected_count: int):
        entries = [e for e in compile_cmds if f'CMakeFiles/{target}.dir/' in e['command']]
        assert len(entries) == expected_count, f"{target}: expected {expected_count}, got {len(entries)}"
        paths = []
        for e in entries:
            m = re.search(r' -o (\S+) -c ', e['command'])
            assert m, f"No -o in command: {e['command']}"
            p = Path(e['directory']) / m[1]
            assert p.exists(), f"Missing object: {p}"
            paths.append(p)
        return paths

    # 3 retained guest objects (replacing 2, 78, 153)
    guest_wait_objs = get_objs_for_target('timeshift_renut_guest_wait', 3)
    
    # 27 core renderer objects
    core_objs = get_objs_for_target('timeshift_renut_core', 27)

    # 24 adapter objects
    adapter_targets = [
        ('timeshift_renut_resource_hooks', 4),
        ('timeshift_renut_device_calls', 2),
        ('timeshift_renut_draw_hooks', 2),
        ('timeshift_renut_tiling', 2),
        ('timeshift_renut_sources', 2),
        ('timeshift_renut_lifecycle', 3),
        ('timeshift_renut_sdk_tiling', 2),
        ('timeshift_renut_fence_query', 1),
        ('timeshift_renut_fence_wait', 2),
        ('timeshift_renut_device_cleanup', 2),
        ('timeshift_renut_callback_hooks', 1),
        ('timeshift_renut_presentation_hooks', 1),
    ]
    adapter_objs = []
    for tgt, count in adapter_targets:
        adapter_objs.extend(get_objs_for_target(tgt, count))
    assert len(adapter_objs) == 24, f"Expected 24 adapter objects, got {len(adapter_objs)}"

    # 5 app objects
    app_objs = [
        BUILD_APP / 'timeshift_app.o',
        BUILD_APP / 'renut_tuning_shims.o',
        BUILD_APP / 'timeshift_register.o',
        BUILD_APP / 'timeshift_thunk_adjustors.o',
        BUILD_APP / 'timeshift_init.o',
    ]

    # 2 Switch platform UI objects
    switch_ui_objs = [
        Path('/root/lab/switch-build-app/CMakeFiles/timeshift.dir/root/lab/rexglue-sdk/src/ui/switch/windowed_app_main_switch.o'),
        BUILD_APP / 'rex_app_switch.o',
    ]

    # 1 uam_local.o object
    uam_local_obj = BUILD_CORE / 'plume/uam_local.o'
    assert uam_local_obj.exists(), f"Missing {uam_local_obj}"

    all_objs = (
        retained_recomp +
        guest_wait_objs +
        core_objs +
        adapter_objs +
        app_objs +
        switch_ui_objs +
        [uam_local_obj]
    )

    # Validate AArch64 ELF on all objects
    for p in all_objs:
        b = p.read_bytes()
        assert b[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', b, 18)[0] == 183, f"Not AArch64 ELF: {p}"

    assert len(all_objs) == 220, f"Expected 220 total objects, got {len(all_objs)}"
    print(f"Collected exactly {len(all_objs)} AArch64 objects:")
    print(f"  158 retained recomp objects")
    print(f"    3 managed guest objects (chunks 2, 78, 153)")
    print(f"   27 core renderer objects")
    print(f"   24 adapter objects")
    print(f"    5 app / tuning / thunk / init objects")
    print(f"    2 Switch platform UI objects")
    print(f"    1 uam_local object")

    return all_objs

def link_candidate(objects):
    elf_path = BUILD_APP / 'timeshift.elf'
    map_path = BUILD_APP / 'timeshift.map'
    nro_path = BUILD_APP / 'timeshift.nro'

    link_cmd = [
        str(AARCH64_CXX),
        '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft',
        '-ftls-model=local-exec', '-ffunction-sections', '-fdata-sections',
        '-D__SWITCH__', '-g', '-O2', '-DNDEBUG',
        '-L/opt/devkitpro/libnx/lib', '-L/opt/devkitpro/portlibs/switch/lib',
        '-fPIE', f'-specs={DEVKITPRO}/libnx/switch.specs',
        f'-Wl,-Map,{map_path}',
        '-Wl,--gc-sections',
        '-Wl,--wrap=nvGpuChannelKickoff',
        '-Wl,--wrap=nvioctlChannel_SubmitGpfifo',
        *map(str, objects),
        '-Wl,--start-group',
        str(BUILD_CORE / 'plume/libplume.a'),
        str(BUILD_CORE / 'plume/uam/libuamlib.a'),
        str(BUILD_CORE / 'libts_renut_zstd.a'),
        str(BUILD_CORE / 'libts_renut_smolv.a'),
        str(BUILD_CORE / 'libts_renut_xxhash.a'),
        '/root/lab/rexglue-sdk/out/switch-arm64/librexruntimerd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/librexui_vk.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libaes128rd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libmspackrd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libo1heaprd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libspdlogrd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libfmtrd.a',
        '/root/lab/rexglue-sdk/out/switch-arm64/libxxhashrd.a',
        '/root/lab/nvk_compat.o',
        '/root/lab/xma_zero_delay.o' if Path('/root/lab/xma_zero_delay.o').exists() else str(ROOT / 'adapter/xma_zero_delay.o'),
        '-ldeko3d',
        '-lavcodec',
        '-ldav1d',
        '-lswresample',
        '-lavutil',
        '-lz',
        '-lnx',
        '-lm',
        '-Wl,--end-group',
        '-o', str(elf_path),
    ]

    print("Linking timeshift.elf...")
    t0 = time.time()
    res = subprocess.run(link_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("Link failed!", file=sys.stderr)
        print(res.stderr, file=sys.stderr)
        sys.exit(1)
    print(f"Linked timeshift.elf in {time.time() - t0:.2f}s ({elf_path.stat().st_size:,} bytes)")

    # elf2nro
    print("Packaging timeshift.nro via elf2nro...")
    nro_cmd = [
        str(ELF2NRO),
        str(elf_path),
        str(nro_path),
        f'--nacp={DEFAULT_NACB}',
        f'--icon={DEFAULT_ICON}',
    ]
    res = subprocess.run(nro_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("elf2nro failed!", file=sys.stderr)
        print(res.stderr, file=sys.stderr)
        sys.exit(1)
    print(f"Packaged timeshift.nro ({nro_path.stat().st_size:,} bytes)")

    return elf_path, map_path, nro_path

def audit_candidate(elf_path: Path, nro_path: Path):
    print("Auditing symbols in timeshift.elf...")
    res = subprocess.run(
        [str(AARCH64_NM), '--defined-only', str(elf_path)],
        capture_output=True, text=True, check=True
    )

    # 1. Foreign hook check
    # Check for known foreign SA2 / Banjo symbols
    foreign_patterns = [
        r'\b(?:sub_|__imp__sub_|renut_native_sub_)(?:00[0-9A-Fa-f]{6}|02[0-9A-Fa-f]{6})\b',
    ]
    all_subs = set(re.findall(r'\b(?:sub_|__imp__sub_|renut_native_sub_)[0-9A-Fa-f]{8}\b', res.stdout))
    
    # 26 expected TimeShift hooks
    expected_ts_hooks = {
        'sub_8263F7C8', 'sub_82640FF0', 'sub_8263FB98', 'sub_8264D7C0', # Resource hooks
        'sub_826505F0', 'sub_8264FFD8', 'sub_82650550', 'sub_8264FB38', # Draw hooks
        'sub_82650020', 'sub_8264A810', 'sub_8264C8E8',
        'sub_826478E0', 'sub_82647600', 'sub_8264E6F0', 'sub_8264F3B8', 'sub_8264F5D8', # Lifecycle
        'sub_8264B8C0', 'sub_8264BD58', 'sub_8264B750', # Tiling
        'sub_8264F4F0', # Fence query
        'sub_8264ED40', 'sub_82647528', 'sub_82653648', # Fence wait
        'sub_8264F6B8', # Callback hook
        'sub_8264B1A8', 'sub_8264AD00', # Presentation hooks
    }

    # Verify all 26 hooks are defined in the binary
    for hook in expected_ts_hooks:
        assert hook in all_subs, f"Missing expected TimeShift hook: {hook}"
    print(f"Verified all 26 TimeShift hooks present in timeshift.elf")

    # Verify zero foreign SA2/Banjo hooks
    sa2_banjo_foreign = [s for s in all_subs if s.startswith(('sub_00', 'sub_02', 'renut_native_sub_00', 'renut_native_sub_02'))]
    assert len(sa2_banjo_foreign) == 0, f"Found foreign hooks: {sa2_banjo_foreign}"
    print("Verified 0 foreign title hooks in timeshift.elf")

    # Record hashes and metadata
    elf_size = elf_path.stat().st_size
    elf_sha = sha256_file(elf_path)
    nro_size = nro_path.stat().st_size
    nro_sha = sha256_file(nro_path)

    report = {
        "candidate": "timeshift-renut-ticket6",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "status": "PASS",
        "objects_linked": 219,
        "timeshift_hooks_count": len(expected_ts_hooks),
        "foreign_hooks_count": 0,
        "elf": {
            "path": str(elf_path),
            "size": elf_size,
            "sha256": elf_sha
        },
        "nro": {
            "path": str(nro_path),
            "size": nro_size,
            "sha256": nro_sha
        }
    }

    ANALYSIS.mkdir(parents=True, exist_ok=True)
    report_file = ANALYSIS / 'switch-candidate-ticket6.json'
    report_file.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Report written to {report_file}")
    print(f"timeshift.elf: {elf_size:,} bytes, sha256={elf_sha}")
    print(f"timeshift.nro: {nro_size:,} bytes, sha256={nro_sha}")

def main():
    print("=== TimeShift Switch App Build & Link (Ticket 6) ===")
    compile_app_units()
    objects = collect_objects()
    elf_path, map_path, nro_path = link_candidate(objects)
    audit_candidate(elf_path, nro_path)
    print("=== Switch App Build & Audit COMPLETED SUCCESSFULLY ===")

if __name__ == '__main__':
    main()
