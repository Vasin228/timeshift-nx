import os, re, json

ROOT = r'D:\Anti\timeshift-renut'

with open(os.path.join(ROOT, 'analysis', 'raw_kernel_stubs.json'), 'r', encoding='utf-8') as f:
    stubs = json.load(f)

print(f'Total raw kernel stubs: {len(stubs)}')

# Classify by file
by_file = {}
for s in stubs:
    by_file.setdefault(s['file'], []).append(s)

for f, items in sorted(by_file.items()):
    print(f'{f}: {len(items)} items')
    for item in items[:5]:
        print(f"  L{item['line']} [{item['type']}] ({item['match']}) -> {item['code'][:80]}")
