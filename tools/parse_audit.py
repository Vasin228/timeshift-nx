import json, os, re

ROOT = r'D:\Anti\timeshift-renut'

def inspect_category(json_path, name):
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    print(f'=== {name}: {len(data)} items ===')
    by_type = {}
    for d in data:
        t = d['type']
        by_type.setdefault(t, []).append(d)
    for t, items in by_type.items():
        print(f'  {t}: {len(items)}')
        for item in items[:8]:
            print(f'    {item["file"]}:{item["line"]} - {item["match"]} | {item["code"][:80]}')

inspect_category(os.path.join(ROOT, 'analysis', 'raw_audio_stubs.json'), 'AUDIO')
inspect_category(os.path.join(ROOT, 'analysis', 'raw_gpu_stubs.json'), 'GPU')
inspect_category(os.path.join(ROOT, 'analysis', 'raw_kernel_stubs.json'), 'KERNEL')
