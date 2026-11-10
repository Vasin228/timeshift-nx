from guest_code import GuestCode, ROOT
import json, re
code = GuestCode()
rows = []
for symbol in sorted(code.index):
    if not 'sub_8263A000' <= symbol < 'sub_8265C000':
        continue
    ops = code.instructions(symbol)
    rows.append({'symbol': symbol, 'ops': ops, 'calls': code.index[symbol]['calls'], 'callers': code.callers(symbol)})
(ROOT / 'analysis/d3d-functions.json').write_text(json.dumps(rows))
print('D3D region functions:', len(rows))
for entry in rows:
    ops = entry['ops']
    literals = []
    for i, (pc, op) in enumerate(ops):
        match = re.fullmatch(r'lis r(\d+),(-?\d+)', op)
        if not match:
            continue
        for _, later in ops[i+1:i+16]:
            tail = re.fullmatch(r'ori r(\d+),r(\d+),(\d+)', later)
            if tail and match[1] == tail[1] == tail[2]:
                value = ((int(match[2]) << 16) | int(tail[3])) & 0xFFFFFFFF
                if value >> 30 == 3 and (value >> 8) & 127 in (0x22, 0x36, 0x27):
                    literals.append(f'{value:08X}@{pc:08X}')
                break
    if literals:
        print(entry['symbol'], len(ops), ' '.join(literals), 'callers:', ','.join(entry['callers']))
