import socket

ips = [os.environ.get('SWITCH_IP', '192.168.1.100')]
for ip in ips:
    s = socket.socket()
    s.settimeout(1.5)
    rc = s.connect_ex((ip, 5000))
    print(f"IP {ip}:5000 -> {rc}")
    s.close()
