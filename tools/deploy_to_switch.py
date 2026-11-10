import ftplib
import hashlib
import os
import sys
import time
from pathlib import Path

SWITCH_IP = os.environ.get('SWITCH_IP', '192.168.1.100')
SWITCH_PORT = 5000
SWITCH_DIR = "/switch/timeshift"

LOCAL_NRO = Path("timeshift-renut/build/app-switch/timeshift.nro")
if not LOCAL_NRO.exists():
    LOCAL_NRO = Path("build/app-switch/timeshift.nro")
if not LOCAL_NRO.exists():
    LOCAL_NRO = Path("d:/Anti/timeshift.nro")

assert LOCAL_NRO.exists(), f"Local NRO not found: {LOCAL_NRO}"

local_bytes = LOCAL_NRO.read_bytes()
local_len = len(local_bytes)
local_hash = hashlib.sha256(local_bytes).hexdigest()

print(f"Local NRO: {LOCAL_NRO} ({local_len:,} bytes)")
print(f"Local SHA256: {local_hash}")

LOCAL_CACHE = Path("d:/Anti/deko3d_shaders_f0.bin")
cache_bytes = LOCAL_CACHE.read_bytes() if LOCAL_CACHE.exists() else None
cache_hash = hashlib.sha256(cache_bytes).hexdigest() if cache_bytes else None
if cache_bytes:
    print(f"Local Shader Cache: {LOCAL_CACHE} ({len(cache_bytes):,} bytes, SHA256={cache_hash[:16]}...)")

print(f"Connecting to ftp://{SWITCH_IP}:{SWITCH_PORT}...")
ftp = ftplib.FTP()
ftp.connect(SWITCH_IP, SWITCH_PORT, timeout=30)
ftp.login()
print(ftp.getwelcome())

ftp.cwd(SWITCH_DIR)

# Remove old logs for fresh telemetry
for log_name in ["timeshift.log", "perf.txt", "deko3d.log"]:
    try:
        ftp.delete(log_name)
        print(f"Deleted old remote log: {log_name}")
    except Exception as e:
        print(f"Notice: could not delete {log_name}: {e}")

# Upload NRO with progress
print(f"Uploading {LOCAL_NRO.name} ({local_len:,} bytes) to {SWITCH_DIR}...")
t0 = time.time()
uploaded = 0

def callback(chunk):
    global uploaded
    uploaded += len(chunk)
    pct = uploaded * 100.0 / local_len
    mb = uploaded / (1024 * 1024)
    speed = mb / max(time.time() - t0, 0.001)
    sys.stdout.write(f"\rUploaded NRO {mb:.1f} MB / {local_len/(1024*1024):.1f} MB ({pct:.1f}%) @ {speed:.2f} MB/s")
    sys.stdout.flush()

with open(LOCAL_NRO, "rb") as f:
    ftp.storbinary("STOR timeshift.nro", f, blocksize=65536, callback=callback)

dt = time.time() - t0
print(f"\nUpload NRO complete in {dt:.2f}s ({uploaded / (1024*1024) / dt:.2f} MB/s)")

# Upload timeshift.toml
toml_path = Path("d:/Anti/timeshift.toml")
if toml_path.exists():
    with open(toml_path, "rb") as f:
        ftp.storbinary("STOR timeshift.toml", f)
    print("Uploaded timeshift.toml")

# Upload deko3d_shaders_f0.bin
if cache_bytes:
    cache_len = len(cache_bytes)
    print(f"Uploading {LOCAL_CACHE.name} ({cache_len:,} bytes) to {SWITCH_DIR}...")
    t0 = time.time()
    uploaded_cache = 0

    def cache_callback(chunk):
        global uploaded_cache
        uploaded_cache += len(chunk)
        pct = uploaded_cache * 100.0 / cache_len
        mb = uploaded_cache / (1024 * 1024)
        speed = mb / max(time.time() - t0, 0.001)
        sys.stdout.write(f"\rUploaded Shader Cache {mb:.1f} MB / {cache_len/(1024*1024):.1f} MB ({pct:.1f}%) @ {speed:.2f} MB/s")
        sys.stdout.flush()

    with open(LOCAL_CACHE, "rb") as f:
        ftp.storbinary("STOR deko3d_shaders_f0.bin", f, blocksize=65536, callback=cache_callback)
    dt = time.time() - t0
    print(f"\nUpload Shader Cache complete in {dt:.2f}s ({uploaded_cache / (1024*1024) / dt:.2f} MB/s)")

# Verify uploaded NRO
print("Verifying uploaded NRO SHA256...")
remote_hash_calc = hashlib.sha256()
t0 = time.time()
downloaded = 0

def verify_callback(chunk):
    global downloaded
    downloaded += len(chunk)
    remote_hash_calc.update(chunk)
    pct = downloaded * 100.0 / local_len
    sys.stdout.write(f"\rReadback verification NRO: {pct:.1f}%")
    sys.stdout.flush()

ftp.retrbinary("RETR timeshift.nro", verify_callback, blocksize=65536)
print("")

remote_hash = remote_hash_calc.hexdigest()
print(f"Remote NRO SHA256: {remote_hash}")
assert remote_hash == local_hash, f"HASH MISMATCH! Local={local_hash}, Remote={remote_hash}"
print("VERIFICATION SUCCESSFUL: NRO 100% BIT-FOR-BIT MATCH!")

# Verify uploaded shader cache
if cache_bytes:
    print("Verifying uploaded deko3d_shaders_f0.bin SHA256...")
    remote_cache_calc = hashlib.sha256()
    cache_len = len(cache_bytes)
    downloaded_cache = 0

    def verify_cache_cb(chunk):
        global downloaded_cache
        downloaded_cache += len(chunk)
        remote_cache_calc.update(chunk)
        pct = downloaded_cache * 100.0 / cache_len
        sys.stdout.write(f"\rReadback verification cache: {pct:.1f}%")
        sys.stdout.flush()

    ftp.retrbinary("RETR deko3d_shaders_f0.bin", verify_cache_cb, blocksize=65536)
    print("")
    remote_cache_hash = remote_cache_calc.hexdigest()
    print(f"Remote Cache SHA256: {remote_cache_hash}")
    assert remote_cache_hash == cache_hash, f"CACHE HASH MISMATCH! Local={cache_hash}, Remote={remote_cache_hash}"
    print("VERIFICATION SUCCESSFUL: Shader Cache 100% BIT-FOR-BIT MATCH!")

ftp.quit()
print("All files deployed and verified successfully!")
