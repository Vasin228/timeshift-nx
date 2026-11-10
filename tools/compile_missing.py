import subprocess
from pathlib import Path

ROOT = Path('d:/Anti/timeshift-renut')
GLSL_DIR = ROOT / 'analysis/deko-combined/glsl'
OUT_DIR = ROOT / 'analysis/deko-all/dksh'
UAM_BIN = '/mnt/d/Anti/timeshift-renut/build/uam-host/uam'

missing = ['ps_0D910154B2E28B1F', 'ps_308E6D466435B8F1', 'ps_9F0711D5E2C58CC5', 'ps_BDF113B9645DB2BA', 'ps_F4D5631F0FF07996']

for stem in missing:
    glsl_path = f'/mnt/d/Anti/timeshift-renut/analysis/deko-combined/glsl/{stem}.glsl'
    dksh_path = f'/mnt/d/Anti/timeshift-renut/analysis/deko-all/dksh/{stem}.dksh'
    stage = 'vert' if stem.startswith('vs_') else 'frag'
    cmd = ['wsl', UAM_BIN, '-s', stage, '-o', dksh_path, glsl_path]
    print(f"Compiling {stem}...")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error compiling {stem}: {res.stderr}")
    else:
        print(f"Success: {stem}.dksh ({Path(ROOT / 'analysis/deko-all/dksh' / (stem + '.dksh')).stat().st_size} bytes)")
