"""Reconstruct this owned basic-compressed XEX using the frozen SDK algorithm.

Input and output stay local. No key or decrypted game bytes are printed.
Only the BASIC/NORMAL format actually used by this title is supported.
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import tarfile
import zipfile
from Crypto.Cipher import AES

ROOT = Path(__file__).resolve().parents[1]
xex = Path('D:/Anti/timeshift-recomp/dump360/default.xex').read_bytes()
if hashlib.sha256(xex).hexdigest() != '0eb9b13de4b44b14f89907b91973209112bca8f229cf7b55e26cc5daa0841050':
    raise ValueError('Unexpected title image; mapping is specific to the recorded XEX')
u32 = lambda offset: struct.unpack_from('>I', xex, offset)[0]
headers = dict(struct.unpack_from('>II', xex, 24 + i * 8) for i in range(u32(20)))
info = headers[0x3FF]
size, encryption, compression = struct.unpack_from('>IHH', xex, info)
if (encryption, compression) != (1, 1) or size < 8 or (size - 8) % 8 or info + size > len(xex):
    raise ValueError('Unsupported or invalid file format')
with tarfile.open('D:/Anti/timeshift-recomp/frozen/vulkan-part181/sdk-source.tar.gz') as archive:
    sdk = archive.extractfile('src/system/xex_module.cpp').read().decode()
match = re.search(r'xe_xex2_retail_key\[16\]\s*=\s*\{([^}]+)', sdk)
key = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{2})', match[1]))
security = u32(16)
session = AES.new(key, AES.MODE_CBC, iv=bytes(16)).decrypt(xex[security+0x150:security+0x160])
cipher = AES.new(session, AES.MODE_CBC, iv=bytes(16))
pos = u32(8)
image = bytearray()
blocks = []
for offset in range(info + 8, info + size, 8):
    data_size, zero_size = struct.unpack_from('>II', xex, offset)
    if data_size % 16 or pos + data_size > len(xex) or len(image) + data_size + zero_size > 128 * 1024 * 1024:
        raise ValueError('Invalid basic compression bounds')
    image.extend(cipher.decrypt(xex[pos:pos+data_size]))
    image.extend(bytes(zero_size))
    pos += data_size
    blocks.append({'data_bytes': data_size, 'zero_bytes': zero_size})
if image[:2] != b'MZ':
    raise ValueError('Missing decrypted DOS header')
pe = struct.unpack_from('<I', image, 0x3C)[0]
if image[pe:pe+4] != b'PE\0\0':
    raise ValueError('Missing decrypted PE header')
_, sections, _, _, _, opt_size, _ = struct.unpack_from('<HHIIIHH', image, pe + 4)
base = struct.unpack_from('<I', image, pe + 24 + 28)[0]
if base != headers[0x10201]:
    raise ValueError('PE/XEX base mismatch')
records = []
for i in range(sections):
    offset = pe + 24 + opt_size + i * 40
    name, virtual_size, rva, raw_size, raw_ptr = struct.unpack_from('<8s4I', image, offset)
    records.append({'name': name.rstrip(b'\0').decode(), 'va': f'0x{base+rva:08X}', 'virtual_size': virtual_size,
                    'raw_size': raw_size, 'raw_pointer': raw_ptr})
# XEX payload is already laid out in memory; do not remap it as an on-disk PE.
text = next(row for row in records if row['name'] == '.text')
start = int(text['va'], 16) - base
old_text = Path('D:/Anti/timeshift-recomp/work/ts_text.bin').read_bytes()
text_match = image[start:start+len(old_text)] == old_text
normalized = bytearray(image[start:start+len(old_text)])
with zipfile.ZipFile('D:/Anti/timeshift-recomp/frozen/vulkan-part181/actual-app-source.zip') as archive:
    init = archive.read('generated/default/timeshift_init.cpp').decode()
import_thunks = [int(value, 16) for value in re.findall(r'\{ 0x([0-9A-F]+), __imp__', init)]
normalized_count = 0
for address in import_thunks:
    pos = address - int(text['va'], 16)
    if pos < 0 or pos + 16 > len(normalized):
        raise ValueError('Import thunk outside .text')
    words = struct.unpack_from('>4I', normalized, pos)
    if words[0] >> 24 != 1 or words[1] >> 24 != 2 or words[0] & 0xFFFFFF != words[1] & 0xFFFFFF or words[2:] != (0x7D6903A6, 0x4E800420):
        raise ValueError(f'Unexpected import thunk shape at {address:08X}')
    struct.pack_into('>4I', normalized, pos, 0x60000000, 0x60000000, 0x60000000, 0x4E800020)
    normalized_count += 1
normalized_match = normalized == old_text
if not normalized_match:
    raise ValueError('Text differences outside the independently checked import-thunk normalization')
out = ROOT / 'analysis'
(out / 'default_image.bin').write_bytes(image)
report = {'xex_sha256': hashlib.sha256(xex).hexdigest(), 'image_sha256': hashlib.sha256(image).hexdigest(),
          'image_bytes': len(image), 'image_base': f'0x{base:08X}', 'sections': records, 'blocks': blocks,
          'source_algorithm': 'frozen SDK src/system/xex_module.cpp ReadImageBasicCompressed',
          'previous_text_byte_identical': text_match, 'normalized_import_thunks': normalized_count,
          'previous_text_full_match_after_import_normalization': normalized_match, 'text_sha256': hashlib.sha256(old_text).hexdigest(),
          'note': 'Entire earlier .text matches after exactly 227 import thunks become NOP/NOP/NOP/BLR. Generated instruction correspondence is checked separately.'}
(out / 'image-identity.json').write_text(json.dumps(report, indent=2)+'\n')
print('Owned image reconstructed:', len(image), 'bytes; normalized full .text matches:', normalized_match, 'import thunks:', normalized_count)
print('Image SHA256:', report['image_sha256'])
print('Sections:', [(row['name'], row['va'], row['virtual_size']) for row in records])
