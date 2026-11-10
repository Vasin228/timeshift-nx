"""Create ReNut's BDKG v1 package from verified TimeShift GLSL (private data).

The pinned consumer explicitly supports dictionary-free zstd frames. Every
entry is decoded and byte-compared before the package is written atomically.
"""
from pathlib import Path
import ctypes as c
import ctypes.util
import hashlib
import json
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
report_path = ROOT/'analysis/deko-combined/report.json' if (ROOT/'analysis/deko-combined/report.json').exists() else ROOT/'analysis/deko-all/report.json'
glsl_dir = ROOT/'analysis/deko-combined/glsl' if (ROOT/'analysis/deko-combined/glsl').exists() else ROOT/'analysis/deko-all/glsl'
report=json.loads(report_path.read_text())
if not report['success'] or report['passed'] != report['inputs']:
    raise ValueError('Complete native compiler validation is required first')
library=ctypes.util.find_library('zstd')
if not library:raise RuntimeError('Run this tool inside WSL with libzstd available')
zstd=c.CDLL(library)
def bind(name,restype,argtypes):
    function=getattr(zstd,name);function.restype=restype;function.argtypes=argtypes;return function
create_c=bind('ZSTD_createCCtx',c.c_void_p,[])
create_d=bind('ZSTD_createDCtx',c.c_void_p,[])
free_c=bind('ZSTD_freeCCtx',c.c_size_t,[c.c_void_p])
free_d=bind('ZSTD_freeDCtx',c.c_size_t,[c.c_void_p])
bound=bind('ZSTD_compressBound',c.c_size_t,[c.c_size_t])
compress=bind('ZSTD_compressCCtx',c.c_size_t,[c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.c_int])
decompress=bind('ZSTD_decompressDCtx',c.c_size_t,[c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t])
is_error=bind('ZSTD_isError',c.c_uint,[c.c_size_t])
error_name=bind('ZSTD_getErrorName',c.c_char_p,[c.c_size_t])
version=bind('ZSTD_versionNumber',c.c_uint,[])
encoder,decoder=create_c(),create_d()
if not encoder or not decoder:raise MemoryError('zstd context allocation failed')
entries=[];payload=bytearray();raw_bytes=0
sources=sorted(glsl_dir.glob('*.glsl'),key=lambda p:int(p.stem[3:],16))
if len(sources)!=report['inputs']:raise ValueError('Unexpected package input count')
try:
    for path in sources:
        if not re.fullmatch(r'(vs|ps)_[0-9A-F]{16}',path.stem):raise ValueError('Invalid cache key name')
        data=path.read_bytes();raw_bytes+=len(data)
        target=c.create_string_buffer(bound(len(data)))
        size=compress(encoder,target,len(target),data,len(data),3)
        if is_error(size):raise ValueError(error_name(size).decode())
        frame=target.raw[:size]
        decoded=c.create_string_buffer(len(data))
        result=decompress(decoder,decoded,len(data),frame,len(frame))
        if is_error(result) or result!=len(data) or decoded.raw!=data:
            raise ValueError('Package round trip failed')
        mask=[0]*8
        for match in re.finditer(rb'#define SPIRV_CROSS_CONSTANT_ID_(\d+) ',data):
            identifier=int(match[1])
            if 1000<=identifier<1256:mask[(identifier-1000)//32]|=1<<((identifier-1000)%32)
        entries.append((int(path.stem[3:],16),len(payload),len(frame),len(data),0 if path.stem.startswith('vs_') else 4,*mask))
        payload+=frame;payload+=bytes(-len(payload)%8)
finally:
    free_c(encoder);free_d(decoder)
if any(a[0]>=b[0] for a,b in zip(entries,entries[1:])):raise ValueError('Duplicate or unsorted shader hash')
table_offset=32;data_offset=table_offset+56*len(entries)
package=bytearray(struct.pack('<4s7I',b'BDKG',1,len(entries),0,table_offset,data_offset,0,0))
for entry in entries:package+=struct.pack('<Q12I',*entry)
package+=payload
out=ROOT/'build/renut-switch';out.mkdir(parents=True,exist_ok=True)
path=out/'deko3d_glsl.bin'
temporary=path.with_suffix('.bin.tmp');temporary.write_bytes(package);temporary.replace(path)
# A separate Switch object can embed this verified package without changing the old app.
cpp='''// Generated from owned TimeShift shaders. Private build data; do not publish.
#include <cstddef>
__asm__(".section .rodata.timeshift_deko3d_glsl,\\\"a\\\"\\n"
        ".global g_deko3dGlslPackage\\n"
        ".type g_deko3dGlslPackage, %object\\n"
        ".balign 16\\n"
        "g_deko3dGlslPackage:\\n"
        ".incbin \\\"@PATH@\\\"\\n"
        ".size g_deko3dGlslPackage, .-g_deko3dGlslPackage\\n"
        ".previous\\n");
extern const size_t g_deko3dGlslPackageSize = @SIZE@;
'''.replace('@PATH@',path.as_posix()).replace('@SIZE@',str(len(package)))
(out/'deko3d_glsl_switch.cpp').write_text(cpp)
summary=dict(success=True,format='BDKG version 1',entries=len(entries),dictionary_bytes=0,zstd_version=version(),
             raw_glsl_bytes=raw_bytes,package_bytes=len(package),sha256=hashlib.sha256(package).hexdigest(),
             all_entries_round_trip_verified=True,compression_level=3,source_native_report='analysis/deko-all/report.json',
             limitation='Shader data package only; no NRO or renderer linked')
(ROOT/'analysis/shader-package.json').write_text(json.dumps(summary,indent=2)+'\n')
print('Package:',len(entries),'entries;',raw_bytes,'raw bytes ->',len(package),'bytes; all entries round-trip verified')
print('SHA256:',summary['sha256'])
