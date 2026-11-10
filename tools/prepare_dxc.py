from pathlib import Path
import hashlib,json,tarfile,urllib.request
root=Path('D:/Anti/timeshift-renut')
url='https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz'
sha='f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c'
out=root/'build/deps';out.mkdir(parents=True,exist_ok=True)
archive=out/'linux_dxc_2025_07_14.x86_64.tar.gz'
if not archive.exists():
    with urllib.request.urlopen(url,timeout=60) as response:
        archive.write_bytes(response.read())
actual=hashlib.sha256(archive.read_bytes()).hexdigest()
if actual!=sha:raise ValueError('Official DXC asset digest mismatch')
dest=out/'dxc-1.8.2505.1';dest.mkdir(exist_ok=True)
with tarfile.open(archive) as source:
    source.extractall(dest,filter='data')
(root/'analysis/dxc-dependency.json').write_text(json.dumps(dict(tag='v1.8.2505.1',url=url,sha256=sha,bytes=archive.stat().st_size,source='Microsoft official release asset/API digest'),indent=2)+'\n')
print('Official DXC verified:',archive.stat().st_size,'bytes; SHA256',sha)
