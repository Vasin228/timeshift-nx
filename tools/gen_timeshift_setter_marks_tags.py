#!/usr/bin/env python3
"""Generates upstream/sa2-nx/app/src/renut_engine/gpu/timeshift_setter_marks_tags.h
for TimeShift's Direct3D 2.0.6274 device (kMarkSnapshotEnd = 12704)."""
import glob
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GEN = os.path.join(ROOT, 'private/app/generated/default')
OUT = os.path.join(ROOT, 'upstream/sa2-nx/app/src/renut_engine/gpu/timeshift_setter_marks_tags.h')
MASK = (1 << 64) - 1
SNAPSHOT = 12704


def compared(off):
    # Matches queued.cpp's DeltaByteRead for TIMESHIFT_RECOMP:
    # unread < 0x480, unread private 10880..11775, float consts 0x780..0x277F handled by vs/ps masks.
    return (0x480 <= off < 0x780) or (0x2780 <= off < 10880) or (11776 <= off < SNAPSHOT)


def chunk_index(off):
    # queued.cpp: 30 head chunks (0..0x77F), then tail from 0x2780
    return off // 64 if off < 0x780 else 30 + (off - 0x2780) // 64


def rotl(v, n):
    n %= 64
    return ((v << n) | (v >> (64 - n))) & MASK


def mask_to(me):
    return MASK ^ ((1 << (63 - me)) - 1)


def read_setters():
    out = {}
    for f in sorted(glob.glob(os.path.join(GEN, 'timeshift_recomp.*.cpp'))):
        lines = open(f, encoding='utf-8', errors='ignore').read().split('\n')
        i = 0
        while i < len(lines):
            m = re.match(r'^DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\) \{$', lines[i])
            if not m or not (0x82635000 <= int(m.group(1), 16) < 0x82658000):
                i += 1
                continue
            name = m.group(1)
            ppc = []
            i += 1
            while i < len(lines) and lines[i] != '}':
                s = lines[i].strip()
                if s.startswith('// ') and not s.startswith('// ---'):
                    ppc.append(s[3:])
                i += 1
            regs, dev = {}, {'r3'}
            tags, offs = defaultdict(int), set()
            for ins in ppc:
                op, _, rest = ins.partition(' ')
                a = [x.strip() for x in rest.split(',')] if rest else []

                def based(x):
                    mm = re.match(r'(-?\d+)\((r\d+)\)$', x)
                    return int(mm.group(1)) if mm and mm.group(2) in dev else None

                if op == 'mr':
                    (dev.add if a[1] in dev else dev.discard)(a[0])
                    regs[a[0]] = regs.get(a[1])
                    continue
                if op == 'bl' and a and re.match(r'0x828e(0f|10)', a[0], re.I):
                    continue  # __savegprlr / __restgprlr / __savefpr
                if op.startswith('bl') and op != 'blr':
                    regs, _ = {}, dev.discard('r3')
                    continue
                if op == 'ld' and len(a) == 2 and based(a[1]) in (16, 24, 32):
                    regs[a[0]] = ('t', based(a[1]) // 8, 0)
                    dev.discard(a[0])
                    continue
                if op.startswith('st') and not op.endswith('x') and len(a) == 2 and based(a[1]) is not None:
                    off, s = based(a[1]), regs.get(a[0])
                    if op == 'std' and off in (16, 24, 32):
                        tags[off // 8] |= s[2] if s and s[0] == 't' and s[1] == off // 8 else MASK
                    else:
                        offs.add(off)
                    continue
                if not a or op.startswith(('st', 'cmp', 'tw', 'b')):
                    continue
                dev.discard(a[0])
                s = regs.get(a[1]) if len(a) > 1 else None
                if op == 'li':
                    regs[a[0]] = ('c', int(a[1], 0) & MASK)
                elif op == 'lis':
                    regs[a[0]] = ('c', (int(a[1], 0) << 16) & MASK)
                elif op in ('ori', 'oris'):
                    v = int(a[2], 0) << (16 if op == 'oris' else 0)
                    regs[a[0]] = (
                        ('c', s[1] | v) if s and s[0] == 'c'
                        else ('t', s[1], s[2] | v) if s and s[0] == 't'
                        else None
                    )
                elif op == 'rldicr' and s and s[0] == 'c':
                    regs[a[0]] = ('c', rotl(s[1], int(a[2])) & mask_to(int(a[3])))
                elif op == 'sldi' and s and s[0] == 'c':
                    regs[a[0]] = ('c', (s[1] << int(a[2])) & MASK)
                elif op == 'or':
                    x, y = regs.get(a[1]), regs.get(a[2])
                    if x and y and {x[0], y[0]} == {'t', 'c'}:
                        t, c = (x, y) if x[0] == 't' else (y, x)
                        regs[a[0]] = ('t', t[1], t[2] | c[1])
                    elif x and y and x[0] == y[0] == 'c':
                        regs[a[0]] = ('c', x[1] | y[1])
                    else:
                        regs[a[0]] = None
                else:
                    regs[a[0]] = None
            if tags or offs:
                out[name] = (dict(tags), offs)
            i += 1
    return out


def main():
    setters = read_setters()
    lines_of_bit = defaultdict(set)
    private_of_bit = defaultdict(bool)
    for name, (tags, offs) in setters.items():
        lines = {o & ~63 for o in offs if compared(o)}
        private = any(10880 <= o < 11776 for o in offs)
        for w, v in tags.items():
            if v == MASK:
                continue
            for b in range(64):
                if v >> b & 1:
                    lines_of_bit[(w, b)] |= lines
                    private_of_bit[(w, b)] |= private

    named = {k for k in set(lines_of_bit) | set(private_of_bit) if lines_of_bit[k] or private_of_bit[k]}
    for n in range(32):  # fetch constant n: word 3, bit 31 - n (6 bytes -> 24B at 0x480 + 24*n)
        k = (3, 31 - n)
        named.add(k)
        lines_of_bit[k] |= {o & ~63 for o in range(0x480 + 24 * n, 0x480 + 24 * n + 24)}

    # Stream stride tag in word 2 bit 19 covers stream_strides (offset 12520..12535 -> 0x30C0)
    lines_of_bit[(2, 19)].add(12520 & ~63)
    named.add((2, 19))

    # Unknown tags fall back to all compared render-state chunks
    state = sorted(
        {o for o in range(0x2780, 10880, 64)} |
        {o for o in range(11776, 12416, 64)}
    )

    rows = []
    for w in (2, 3, 4):
        for b in range(64):
            k = (w, b)
            lines = sorted(lines_of_bit[k]) if k in named else state
            idx = sorted({chunk_index(o) for o in lines if compared(o)})
            words = [0, 0]
            for i in idx:
                words[i // 64] |= 1 << (i % 64)
            rows.append((w, b, k in named, lines, words))

    if '--print' in sys.argv:
        for w, b, known, lines, _ in rows:
            print(f'w{w} b{b:2d} {"setters" if known else "unknown"}: {" ".join(f"0x{x:X}" for x in lines)}')
        return

    with open(OUT, 'w', newline='\n') as o:
        o.write(
            '// Generated by tools/gen_timeshift_setter_marks_tags.py (do not edit): per dirty tag\n'
            '// (m_Pending words 2-4, bit from LSB), TimeShift D3D 2.0.6274 delta chain chunks\n'
            '// (kMarkSnapshotEnd = 12704).\n'
            '#pragma once\n\n#include <cstdint>\n\nnamespace sa2::renut {\n\n'
            'inline constexpr uint64_t kTagChunks[3 * 64][2] = {\n'
        )
        for w, b, known, lines, words in rows:
            note = ' '.join(f'0x{x:X}' for x in lines if compared(x)) if known else 'unknown: the render state'
            o.write(f'    {{0x{words[0]:016X}ull, 0x{words[1]:016X}ull}},  // word {w} bit {b}: {note}\n')
        o.write('};\n\n}  // namespace sa2::renut\n')
    print(f'{OUT}: {sum(1 for r in rows if r[2])} tags named by setters, {len(setters)} setters read')


if __name__ == '__main__':
    main()
