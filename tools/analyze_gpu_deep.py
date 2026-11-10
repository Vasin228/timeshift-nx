import os, re, json

ROOT = r'D:\Anti\timeshift-renut'

gpu_files = [
    r'upstream\sa2-nx\app\src\renut_engine\gpu\render\draw.cpp',
    r'upstream\sa2-nx\app\src\renut_engine\gpu\render\textures.cpp',
    r'upstream\sa2-nx\app\src\renut_engine\gpu\render\pipeline.cpp',
    r'upstream\sa2-nx\app\src\renut_engine\gpu\render\surfaces.cpp',
    r'upstream\sa2-nx\app\src\renut_engine\gpu\backend\present.cpp',
    r'upstream\sa2-nx\app\src\renut_engine\gpu\backend\render_thread.cpp',
    r'upstream\sa2-nx\app\thirdparty\plume\plume_deko3d.cpp',
    r'adapter\draw_hooks.cpp',
    r'adapter\draw_runtime_bridge.cpp',
    r'adapter\shader_resource_hooks.cpp',
    r'adapter\shader_runtime_bridge.cpp',
    r'adapter\tiling.cpp',
    r'adapter\tiling_hooks.cpp',
    r'adapter\sdk_tiling_host.cpp',
    r'adapter\renut_tuning_shims.cpp'
]

results = []

for rel in gpu_files:
    full_path = os.path.join(ROOT, rel)
    if not os.path.exists(full_path): continue
    with open(full_path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()
    
    for i, line in enumerate(lines):
        line_no = i + 1
        l = line.strip()
        
        # 1. Explicit
        m = re.search(r'\b(TODO|FIXME|STUB|UNIMPLEMENTED|HACK|dummy|placeholder)\b', l, re.I)
        if m:
            results.append({
                'file': rel.replace('\\', '/'),
                'line': line_no,
                'category': 'Explicit Marker',
                'marker': m.group(0),
                'code': l
            })
            
        # 2. Suppressed args
        m_void = re.search(r'\(void\)\s*([a-zA-Z0-9_]+);', l)
        if m_void:
            results.append({
                'file': rel.replace('\\', '/'),
                'line': line_no,
                'category': 'Suppressed Argument',
                'marker': m_void.group(1),
                'code': l
            })
            
        # 3. Early return / bypass
        if re.search(r'^\s*if\s*\(\s*(true|1)\s*\)\s*return\b', l):
            results.append({
                'file': rel.replace('\\', '/'),
                'line': line_no,
                'category': 'Early Return Override',
                'marker': 'if (true) return',
                'code': l
            })
            
        # 4. #if 0
        if re.search(r'^\s*#\s*if\s+0\b', l):
            results.append({
                'file': rel.replace('\\', '/'),
                'line': line_no,
                'category': '#if 0 Block',
                'marker': '#if 0',
                'code': l
            })

print(f'Total targeted GPU findings: {len(results)}')
with open(os.path.join(ROOT, 'analysis', 'targeted_gpu_findings.json'), 'w', encoding='utf-8') as f:
    json.dump(results, f, indent=2)

for r in results:
    print(f"{r['file']}:{r['line']} [{r['category']}] ({r.get('marker', '')}) -> {r['code'][:90]}")
