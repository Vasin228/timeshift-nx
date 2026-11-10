"""Read TimeShift's observed chunked-zlib .s3dpak envelope, without modifying inputs."""
from pathlib import Path
import argparse, hashlib, json, struct, zlib

ROOT = Path(__file__).resolve().parents[1]
def decompress(path):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data)[0]
    if not 0 < count <= 1000000 or 4 + count * 4 > len(data):
        raise ValueError('Invalid chunk directory')
    offsets = list(struct.unpack_from(f'<{count}I', data, 4)) + [len(data)]
    if offsets[0] < 4 + count * 4 or offsets[-2] >= len(data) or any(a >= b for a,b in zip(offsets, offsets[1:])):
        raise ValueError('Invalid chunk ranges')
    output = bytearray()
    sizes = []
    for first,last in zip(offsets,offsets[1:]):
        decoder = zlib.decompressobj()
        chunk = decoder.decompress(data[first:last], 32769)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or not 0 < len(chunk) <= 32768:
            raise ValueError(f'Invalid zlib chunk at {first:#x}')
        output.extend(chunk)
        sizes.append(len(chunk))
    return bytes(output), dict(source=str(path), source_bytes=len(data), source_sha256=hashlib.sha256(data).hexdigest(),
                              chunks=count, chunk_output_sizes=sorted(set(sizes)), output_bytes=len(output),
                              output_sha256=hashlib.sha256(output).hexdigest(), all_stream_checksums_verified=True)
if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('files',nargs='+',type=Path)
    args=parser.parse_args()
    out=ROOT/'analysis/unpacked';out.mkdir(exist_ok=True)
    reports=[]
    for path in args.files:
        data,report=decompress(path)
        (out/(path.stem+'.bin')).write_bytes(data)
        reports.append(report)
        print(path.name,report['chunks'],'checked zlib chunks ->',len(data),'bytes')
    (out/'manifest.json').write_text(json.dumps(reports,indent=2)+'\n')
