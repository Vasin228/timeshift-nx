"""Generate private, unmodified guest functions for the CPU bootstrap fixture."""
from guest_code import GuestCode, ROOT
code = GuestCode()
out = ROOT / 'analysis/fixture'
out.mkdir(exist_ok=True)
symbols = ['sub_826478E0', 'sub_8264E4C0', 'sub_82654078', 'sub_82647600', 'sub_82652DB8']
(out/'guest_subset.inc').write_text('\n\n'.join(code.function(name) for name in symbols)+'\n')
print('Private bootstrap fixture:', len(symbols), 'original guest functions')
