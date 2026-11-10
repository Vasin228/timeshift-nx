"""Extract complete native shader containers from TimeShift's owned PS archive.

Two checked compression layers: .s3dpak chunks and per-group shader batches.
Output is always private under analysis/. File names use SHA256; XenosRecomp
computes its own XXH3 cache key from the complete bytes when it reads them.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib
from read_pak import decompress, ROOT


def directory(data):
    count = struct.unpack_from('<I', data)[0]
    if not 0 < count < 100000:
        raise ValueError('Invalid resource count')
    cursor = 4
    entries = []
    for _ in range(count):
        if cursor + 12 > len(data):
            raise ValueError('Truncated resource directory')
        offset, size, length = struct.unpack_from('<3I', data, cursor)
        cursor += 12
        if not 0 < length < 4096 or cursor + length + 12 > len(data):
            raise ValueError('Invalid resource name bounds')
        name = data[cursor:cursor+length].decode('ascii')
        cursor += length
        kind, reserved0, reserved1 = struct.unpack_from('<3I', data, cursor)
        cursor += 12
        if reserved0 or reserved1 or offset + size > len(data):
            raise ValueError('Unsupported resource record')
        entries.append(dict(name=name, offset=offset, size=size, kind=kind))
    expected = cursor
    for entry in entries:
        if entry['offset'] != expected:
            raise ValueError('Resource data is not contiguous after the directory')
        expected += entry['size']
    if expected != len(data):
        raise ValueError('Unaccounted resource bytes')
    return entries


def containers(blob):
    if len(blob) < 12:
        raise ValueError('Truncated shader cache group')
    batches, total_count, total_compressed = struct.unpack_from('<3I', blob)
    if not 0 < batches <= total_count < 100000 or len(blob) != 12 + batches*8 + total_count*12 + total_compressed:
        raise ValueError('Inconsistent shader batch directory totals')
    cursor = 12
    count = compressed_count = 0
    tables = []
    for batch in range(batches):
        compressed, entries = struct.unpack_from('<2I', blob, cursor)
        cursor += 8
        if not 0 < entries <= total_count or cursor + entries*12 + compressed > len(blob):
            raise ValueError('Truncated shader batch')
        table = [struct.unpack_from('<QI', blob, cursor+i*12) for i in range(entries)]
        cursor += entries*12
        tables.append((compressed, table))
    if cursor + total_compressed != len(blob):
        raise ValueError('Batch tables do not precede the complete payload region')
    for batch, (compressed, table) in enumerate(tables):
        expected = sum(size for _,size in table)
        if expected > 64*1024*1024 or any(size < 4 for _,size in table):
            raise ValueError('Invalid decompressed shader sizes')
        decoder = zlib.decompressobj()
        decoded = decoder.decompress(blob[cursor:cursor+compressed], expected+1)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or len(decoded) != expected:
            raise ValueError('Shader batch zlib stream/size mismatch')
        cursor += compressed
        compressed_count += compressed
        offset = 0
        for permutation, size in table:
            container = decoded[offset:offset+size]
            offset += size
            count += 1
            flags = struct.unpack_from('>I', container)[0]
            if flags & 0xFFFFFF00 != 0x102A1100:
                yield None, dict(batch=batch, permutation=f'{permutation:016x}', kind='non_container',
                                 bytes=size, sha256=hashlib.sha256(container).hexdigest())
                continue
            if size < 36:
                raise ValueError('Truncated native shader header')
            flags, virtual, physical, _, ct, dt, shader_offset, reserved0, reserved1 = struct.unpack_from('>9I', container)
            if virtual < 36 or virtual+physical != size or not physical or reserved0 or reserved1:
                raise ValueError(f'Unexpected shader header: flags={flags:08X}, virtual={virtual}, physical={physical}, record={size}, reserved={reserved0}/{reserved1}, batch={batch}, index={count}')
            if not (ct < virtual and dt < virtual and 36 <= shader_offset <= virtual-24):
                raise ValueError('Shader table outside the virtual part')
            code_offset, code_size = struct.unpack_from('>2I', container, shader_offset)
            if not code_size or code_size % 12 or code_offset+code_size > physical:
                raise ValueError('Shader microcode outside the physical part')
            yield container, dict(batch=batch, permutation=f'{permutation:016x}', kind='vs' if flags&1 else 'ps',
                                  virtual_bytes=virtual, physical_bytes=physical, microcode_bytes=code_size)
    if cursor != len(blob) or count != total_count or compressed_count != total_compressed:
        raise ValueError('Shader group totals do not cover the input exactly')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    data, source = decompress(args.archive)
    output = ROOT/'analysis/shader-dump'
    target = output/'containers'
    target.mkdir(parents=True, exist_ok=True)
    unique = {}
    records = []
    non_containers = []
    groups = []
    for entry in directory(data):
        if entry['kind'] != 4:
            continue
        group_count = 0
        for container, record in containers(data[entry['offset']:entry['offset']+entry['size']]):
            if container is None:
                non_containers.append(dict(source_group=entry['name'], **record))
                continue
            sha = hashlib.sha256(container).hexdigest()
            name = f"{record['kind']}_{sha}.bin"
            path = target/name
            if path.exists() and path.read_bytes() != container:
                raise ValueError('Existing shader path has different content')
            if not path.exists():
                path.write_bytes(container)
            unique[sha] = dict(kind=record['kind'], bytes=len(container))
            records.append(dict(source_group=entry['name'], file=name, sha256=sha, **record))
            group_count += 1
        groups.append(dict(name=entry['name'], containers=group_count))
        print(entry['name'], group_count, 'complete checked containers')
    report = dict(success=True, source=source, groups=groups, total_containers=len(records), unique_containers=len(unique),
                  unique_vertex=sum(row['kind']=='vs' for row in unique.values()),
                  unique_pixel=sum(row['kind']=='ps' for row in unique.values()),
                  unique_bytes=sum(row['bytes'] for row in unique.values()), records=records,
                  non_container_records=non_containers,
                  coverage='initial_ps archive only; not proof of complete level/runtime shader coverage',
                  naming='SHA256 file names; native translator calculates its XXH3 key from the container bytes')
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('TOTAL:',len(records),'containers;',len(unique),'unique;',report['unique_bytes'],'bytes; no translation yet')

if __name__=='__main__':
    main()
