import ftplib, os, sys, socket, time
from pathlib import Path

# Paths
if sys.platform == 'win32':
    OUT_DIR = Path(r"D:\Anti\timeshift-renut\latest_logs_p236")
else:
    OUT_DIR = Path("/mnt/d/Anti/timeshift-renut/latest_logs_p236")

OUT_DIR.mkdir(parents=True, exist_ok=True)

candidate_ips = [os.environ.get('SWITCH_IP', '192.168.1.100')]
port = 5000
ip = None

print(f"Checking DBI FTP on {candidate_ips}:{port}...")
ftp = ftplib.FTP()
connected = False
for cand in candidate_ips:
    try:
        print(f"Trying {cand}:{port}...")
        ftp.connect(cand, port, timeout=5)
        ftp.login()
        ip = cand
        connected = True
        print(f"Connected to {ip}:{port}!")
        break
    except Exception:
        pass

if not connected:
    print("ERROR: DBI FTP server is not reachable. Make sure 'Run FTP server' is active in DBI on the Switch!")
    sys.exit(1)

ftp.cwd('/switch/timeshift')

files_to_get = ['timeshift.log', 'deko3d.log', 'probe_report.txt', 'timeshift.toml']
remote_files = [os.path.basename(f) for f in ftp.nlst()]
print(f"Remote files in /switch/timeshift: {remote_files}")

downloaded = []
for fname in files_to_get:
    if fname in remote_files:
        local_path = OUT_DIR / fname
        with open(local_path, 'wb') as f:
            ftp.retrbinary(f'RETR {fname}', f.write)
        sz = local_path.stat().st_size
        print(f"Downloaded {fname}: {sz} bytes")
        downloaded.append(fname)
    else:
        print(f"File {fname} not found on remote Switch")

ftp.quit()
print(f"\nSuccessfully downloaded {len(downloaded)} files to {OUT_DIR}")
