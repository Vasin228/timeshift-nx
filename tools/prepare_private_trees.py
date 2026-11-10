"""Restore/verify independent source trees from the frozen owned reference.

Existing differing files cause an error, never an overwrite. Renderer changes
belong in upstream patches/adapter, not the original SDK/app or frozen files.
"""
from pathlib import Path, PurePosixPath
import hashlib
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
frozen = ROOT.parent / 'timeshift-recomp/frozen/vulkan-part181'
private = ROOT / 'private'

def install(root, name, content):
    relative = PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name:
        raise ValueError('Unsafe archive member')
    path = root.joinpath(*relative.parts)
    if path.exists():
        if path.read_bytes() != content:
            raise ValueError(f'Existing independent source differs: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)

archive = frozen / 'sdk-source.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == 'b39f2c383853c4832683f11c951117b22bcb5c066b5b720b799da2081c69e342'
count = 0
with tarfile.open(archive) as source:
    for member in source:
        if member.isdir():
            continue
        if not member.isfile():
            raise ValueError('Source snapshot contains a non-regular member')
        install(private / 'sdk', member.name, source.extractfile(member).read())
        count += 1
app_archive = frozen / 'actual-app-source.zip'
assert hashlib.sha256(app_archive.read_bytes()).hexdigest() == '0ad150023558aed5e9601268cd22f49e7c706db1a74b171cf401ebf0fc9a43f2'
with zipfile.ZipFile(app_archive) as source:
    app_count = 0
    for member in source.infolist():
        if member.is_dir():
            continue
        install(private / 'app', member.filename, source.read(member))
        app_count += 1
image = (ROOT.parent / 'timeshift-recomp/dump360/default.xex').read_bytes()
assert hashlib.sha256(image).hexdigest() == '0eb9b13de4b44b14f89907b91973209112bca8f229cf7b55e26cc5daa0841050'
install(private / 'dump360', 'default.xex', image)
print(f'PASS: independent SDK {count} files; independent app {app_count} files; owned XEX identity; original trees untouched')
