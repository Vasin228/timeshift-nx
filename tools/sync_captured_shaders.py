import ftplib
import glob
import os
import sys
import socket

ROOT = r'D:\Anti\timeshift-renut' if sys.platform == 'win32' else '/mnt/d/Anti/timeshift-renut'
DEST_DIR = os.path.join(ROOT, 'analysis', 'all-containers')
CAPTURED_DIR = os.path.join(ROOT, 'analysis', 'captured_shaders')

os.makedirs(DEST_DIR, exist_ok=True)
os.makedirs(CAPTURED_DIR, exist_ok=True)

candidate_ips = [os.environ.get('SWITCH_IP', '192.168.1.100')]
port = 5000
ip = None

for cand in candidate_ips:
    s = socket.socket()
    s.settimeout(1.0)
    try:
        s.connect((cand, port))
        s.close()
        ip = cand
        print(f"Connected to {ip}:{port}")
        break
    except Exception:
        pass

if not ip:
    print("ERROR: Cannot connect to Switch DBI FTP!")
    sys.exit(1)

existing_all = {os.path.basename(p).lower(): p for p in glob.glob(os.path.join(DEST_DIR, '*.bin'))}

print(f"Connecting to {ip}:{port}...")
f = ftplib.FTP()
f.connect(ip, port, timeout=15)
f.login()
f.cwd('/switch/timeshift/shader_capture')

remote_files = f.nlst()
print(f"Total remote files in /switch/timeshift/shader_capture: {len(remote_files)}")

downloaded = 0
for r in remote_files:
    fname = os.path.basename(r)
    lower = fname.lower()
    local_path = os.path.join(DEST_DIR, fname)
    cap_path = os.path.join(CAPTURED_DIR, fname)
    if lower not in existing_all:
        with open(local_path, 'wb') as out_f:
            f.retrbinary(f'RETR {fname}', out_f.write)
        with open(local_path, 'rb') as in_f:
            with open(cap_path, 'wb') as cap_f:
                cap_f.write(in_f.read())
        downloaded += 1
        print(f"  New shader downloaded: {fname}")

print(f"Finished downloading {downloaded} new shaders into all-containers and captured_shaders.")
f.quit()
