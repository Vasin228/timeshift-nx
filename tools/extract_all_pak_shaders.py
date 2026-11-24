"""Extract native Xbox 360 shader containers across all TimeShift archives.

Sources scanned:
1. All *.s3dpak archives under --game-root/paks360 (using a relaxed resource
   directory parser so level archives with empty resource names before kind==4
   shader groups are parsed cleanly).
2. All *.sdrc caches under --game-root/SHADERS_cache/local.
3. Optional captured shader container files or directories passed via --captures.

If --baseline is provided, containers whose SHA-256 already exists there are
skipped; all remaining unique containers are written once by SHA-256 into
<out>/new_containers and sharded round-robin across <out>/shards/in_0..in_<k-1>.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct

from read_pak import decompress
from dump_shaders import containers as iter_pak_containers

ROOT = Path(__file__).resolve().parents[1]


def parse_directory_relaxed(data: bytes) -> list[dict]:
    """Parse an .s3dpak resource directory allowing zero-length names."""
    count = struct.unpack_from("<I", data)[0]
    if not 0 < count < 100000:
        raise ValueError(f"Invalid resource count: {count}")
    cursor = 4
    entries = []
    for _ in range(count):
        if cursor + 12 > len(data):
            raise ValueError("Truncated resource directory")
        offset, size, length = struct.unpack_from("<3I", data, cursor)
        cursor += 12
        if length >= 4096 or cursor + length + 12 > len(data):
            raise ValueError(f"Invalid resource name bounds: {length}")
        name = data[cursor:cursor + length].decode("ascii", errors="replace")
        cursor += length
        kind, _r0, _r1 = struct.unpack_from("<3I", data, cursor)
        cursor += 12
        if offset + size > len(data):
            raise ValueError(f"Resource extends past EOF: {offset}+{size}>{len(data)}")
        entries.append(dict(name=name, offset=offset, size=size, kind=kind))
    return entries


def validate_container(buf: bytes, start: int = 0) -> bytes | None:
    """Validate and slice a single Xenos shader container starting at buf[start]."""
    if start + 36 > len(buf):
        return None
    flags, virtual, physical, _, ct, dt, shader_off, r0, r1 = struct.unpack_from(">9I", buf, start)
    if (flags & 0xFFFFFF00) != 0x102A1100 or r0 != 0 or r1 != 0:
        return None
    if virtual < 36 or virtual > 0x10000 or physical == 0 or physical > 0x100000:
        return None
    total = virtual + physical
    if start + total > len(buf):
        return None
    if ct >= virtual or dt >= virtual or shader_off < 36 or shader_off + 24 > virtual:
        return None
    code_offset, code_size = struct.unpack_from(">2I", buf, start + shader_off)
    if code_size == 0 or code_size % 12 != 0 or code_offset + code_size > physical:
        return None
    return buf[start:start + total]


def iter_sdrc_containers(data: bytes):
    """Scan an .sdrc cache blob for complete, valid Xbox 360 shader containers."""
    pos = 0
    while True:
        hit = data.find(b"\x10\x2a\x11", pos)
        if hit == -1 or hit + 36 > len(data):
            break
        container = validate_container(data, hit)
        if container is not None:
            flags = struct.unpack_from(">I", container)[0]
            yield container, ("vs" if (flags & 1) else "ps")
            pos = hit + len(container)
        else:
            pos = hit + 1


def load_baseline_shas(baseline_dir: Path | None) -> set[str]:
    if not baseline_dir or not baseline_dir.exists():
        return set()
    return {hashlib.sha256(path.read_bytes()).hexdigest() for path in baseline_dir.glob("*.bin")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, default=ROOT / "game")
    parser.add_argument("--baseline", type=Path, default=None)
    parser.add_argument("--captures", type=Path, nargs="*", default=[])
    parser.add_argument("--out", type=Path, default=ROOT / "analysis/all-paks-v1")
    parser.add_argument("--shards", type=int, default=4)
    args = parser.parse_args()

    baseline_shas = load_baseline_shas(args.baseline)
    print(f"Loaded {len(baseline_shas)} baseline SHA-256 hashes")

    new_dir = args.out / "new_containers"
    shards_root = args.out / "shards"
    for d in [new_dir, *(shards_root / f"in_{i}" for i in range(args.shards))]:
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True, exist_ok=True)

    seen_new: dict[str, tuple[str, Path]] = {}
    all_pak_unique = set()
    source_stats = {"s3dpak": 0, "sdrc": 0, "captures": 0}

    def record(container: bytes, kind: str, origin: str, bucket: str):
        sha = hashlib.sha256(container).hexdigest()
        if bucket == "s3dpak":
            all_pak_unique.add(sha)
        if sha in baseline_shas or sha in seen_new:
            return
        name = f"{kind}_{sha}.bin"
        out_path = new_dir / name
        out_path.write_bytes(container)
        seen_new[sha] = (origin, out_path)
        source_stats[bucket] += 1

    paks_dir = args.game_root / "paks360"
    if paks_dir.exists():
        for pak in sorted(paks_dir.glob("*.s3dpak")):
            data, _ = decompress(pak)
            for entry in parse_directory_relaxed(data):
                if entry["kind"] != 4:
                    continue
                blob = data[entry["offset"]:entry["offset"] + entry["size"]]
                for container, meta in iter_pak_containers(blob):
                    if container is not None:
                        record(container, meta["kind"], pak.name, "s3dpak")

    sdrc_dir = args.game_root / "SHADERS_cache" / "local"
    if sdrc_dir.exists():
        for sdrc in sorted(sdrc_dir.glob("*.sdrc")):
            for container, kind in iter_sdrc_containers(sdrc.read_bytes()):
                record(container, kind, sdrc.name, "sdrc")

    for src in args.captures:
        files = [src] if src.is_file() else (sorted(src.glob("*.bin")) if src.is_dir() else [])
        for path in files:
            raw = path.read_bytes()
            container = validate_container(raw, 0)
            if container is None or len(container) != len(raw):
                raise ValueError(f"Invalid capture container: {path}")
            flags = struct.unpack_from(">I", container)[0]
            record(container, "vs" if (flags & 1) else "ps", path.name, "captures")

    sorted_files = sorted(new_dir.glob("*.bin"), key=lambda p: p.name)
    shard_counts = [0] * args.shards
    for idx, src in enumerate(sorted_files):
        shard = idx % args.shards
        shutil.copy2(src, shards_root / f"in_{shard}" / src.name)
        shard_counts[shard] += 1

    summary = {
        "baseline_unique": len(baseline_shas),
        "all_pak_unique": len(all_pak_unique),
        "new_unique": len(seen_new),
        "total_unique_after_merge": len(baseline_shas) + len(seen_new),
        "new_by_bucket": source_stats,
        "shard_counts": shard_counts,
    }
    (args.out / "extract_manifest.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
