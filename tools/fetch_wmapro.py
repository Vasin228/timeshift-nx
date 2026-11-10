import urllib.request
import os

url = 'https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/wmaprodec.c'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req, timeout=10) as resp:
        content = resp.read().decode('utf-8', errors='ignore')
    out_path = os.path.join(os.path.dirname(__file__), 'wmaprodec.c')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f'Downloaded wmaprodec.c to {out_path}, length: {len(content)}')
except Exception as e:
    print('Failed:', e)
