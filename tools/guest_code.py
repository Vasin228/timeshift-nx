from pathlib import Path
import json, re, zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = Path('D:/Anti/timeshift-recomp/frozen/vulkan-part181/actual-app-source.zip')

class GuestCode:
    def __init__(self):
        self.archive = zipfile.ZipFile(ARCHIVE)
        self.index = json.loads((ROOT / 'analysis/function-index.json').read_text())
        self.cache = {}

    def function(self, symbol):
        entry = self.index[symbol]
        name = entry['file']
        if name not in self.cache:
            self.cache[name] = self.archive.read(name).decode()
        text = self.cache[name]
        match = re.search(r'DEFINE_REX_FUNC\(' + symbol + r'\)\s*\{', text)
        end = text.find('DEFINE_REX_FUNC(', match.end())
        return text[match.start():end if end >= 0 else len(text)].rstrip()

    def instructions(self, symbol):
        pc = int(symbol[4:], 16)
        result = []
        for line in self.function(symbol).splitlines():
            stripped = line.strip()
            label = re.fullmatch(r'loc_([0-9A-F]{8}):', stripped)
            if label and int(label[1], 16) != pc:
                raise ValueError(f'{symbol}: label {label[1]} != computed PC {pc:08X}')
            lr = re.fullmatch(r'ctx.lr = 0x([0-9A-F]+);', stripped)
            if lr and int(lr[1], 16) != pc:
                raise ValueError(f'{symbol}: return address {lr[1]} != computed PC {pc:08X}')
            if stripped.startswith('// '):
                result.append((pc, stripped[3:]))
                pc += 4
        return result

    def callers(self, symbol):
        return [key for key, value in self.index.items() if symbol in value['calls']]

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('symbols', nargs='+')
    parser.add_argument('--cpp', action='store_true')
    args = parser.parse_args()
    code = GuestCode()
    out = ROOT / 'analysis/anchors'
    out.mkdir(exist_ok=True)
    for symbol in args.symbols:
        body = code.function(symbol)
        (out / f'{symbol}.cpp').write_text(body + '\n')
        assembly = '\n'.join(f'{pc:08X}  {op}' for pc, op in code.instructions(symbol))
        (out / f'{symbol}.asm').write_text(assembly + '\n')
        print(symbol, 'callers:', ', '.join(code.callers(symbol)))
        print(body if args.cpp else assembly)
