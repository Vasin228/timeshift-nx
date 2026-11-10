import ftplib, hashlib, os, time, socket, sys
from pathlib import Path

# Detect ROOT path based on OS
if sys.platform == 'win32':
    ROOT = Path(r'D:\Anti\timeshift-renut')
else:
    ROOT = Path('/mnt/d/Anti/timeshift-renut')

local_nro = ROOT / 'build' / 'app-switch' / 'timeshift.nro'
local_toml = ROOT / 'remote_timeshift.toml'

if not local_nro.exists():
    print(f"ERROR: {local_nro} does not exist!")
    sys.exit(1)

local_data = local_nro.read_bytes()
actual_local_sha = hashlib.sha256(local_data).hexdigest()
size_bytes = len(local_data)
mtime = time.ctime(os.path.getmtime(local_nro))

print(f"=== Local Binary Details ===")
print(f"Path: {local_nro}")
print(f"Size: {size_bytes} bytes")
print(f"LastWriteTime: {mtime}")
print(f"SHA256: {actual_local_sha}")

candidate_ips = [os.environ.get('SWITCH_IP', '192.168.1.100')]
port = 5000
ip = None

print(f"\nWaiting for DBI FTP on {candidate_ips}:{port} (timeout 600s)...")
connected = False
t_start = time.time()
ftp = ftplib.FTP()
while time.time() - t_start < 600:
    for cand in candidate_ips:
        try:
            print(f"Trying {cand}:{port}...")
            ftp.connect(cand, port, timeout=5)
            ftp.login()
            ip = cand
            connected = True
            print(f"DBI FTP logged in successfully on {ip}:{port}!")
            break
        except Exception as e:
            # print(f"  {cand}: {e}")
            pass
    if connected:
        break
    time.sleep(2)

if not connected:
    print(f"ERROR: Could not connect to any candidate IP on port {port} within 600 seconds. Please turn on 'Run FTP server' in DBI on the Switch!")
    sys.exit(1)

ftp.cwd('/switch/timeshift')

for log_name in ['timeshift.log', 'deko3d.log']:
    try:
        ftp.delete(log_name)
        print(f"Deleted remote {log_name}")
    except Exception:
        pass

# Upload timeshift.toml
if local_toml.exists():
    print("Uploading timeshift.toml...")
    with open(local_toml, 'rb') as f:
        ftp.storbinary('STOR timeshift.toml', f)
    print("timeshift.toml uploaded successfully!")

shader_bin = Path(r'D:\Anti\deko3d_shaders_f0.bin') if sys.platform == 'win32' else Path('/mnt/d/Anti/deko3d_shaders_f0.bin')
if shader_bin.exists():
    shader_size = shader_bin.stat().st_size
    print(f"Uploading deko3d_shaders_f0.bin ({shader_size:,} bytes)...")
    t0 = time.time()
    with open(shader_bin, 'rb') as f:
        ftp.storbinary('STOR deko3d_shaders_f0.bin', f, blocksize=1024*1024)
    t1 = time.time()
    print(f"deko3d_shaders_f0.bin uploaded in {t1 - t0:.2f}s - Transfer complete: 100%")

print("Uploading timeshift.nro...")
t0 = time.time()
with open(local_nro, 'rb') as f:
    ftp.storbinary('STOR timeshift.nro', f, blocksize=1024*1024)
t1 = time.time()
speed = (size_bytes / (t1 - t0)) / (1024 * 1024)
print(f"Uploaded in {t1 - t0:.2f}s ({speed:.2f} MB/s) - Transfer complete: 100%")

print("Verifying upload via readback SHA256 from Switch SD card...")
hasher = hashlib.sha256()
def chunk_handler(data):
    hasher.update(data)

ftp.retrbinary('RETR timeshift.nro', chunk_handler, blocksize=1024*1024)
readback_sha = hasher.hexdigest()
print(f"Readback SHA256: {readback_sha}")
if readback_sha.lower() == actual_local_sha.lower():
    print("SUCCESS: Bit-for-bit 100% verification MATCH! (Transfer complete)")
else:
    print(f"FAILURE: Readback SHA mismatch! Local: {actual_local_sha}, Remote: {readback_sha}")
    sys.exit(2)

ftp.quit()
print("\n[DEPLOYMENT FINISHED SUCCESSFULLY]")
