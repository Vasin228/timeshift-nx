"""Generate a private fixture around the actual pinned ReNut package parser."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/banjo_renut_deko3d.cpp').read_text()
start=re.search(r'#pragma pack\(push,\s*1\)',source).start()
structs=source[start:source.index('// Index 0:',start)]
start=source.index('bool Attach(')
attach=source[start:source.index('Packages &Open()',start)]
header='''#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>
#include <mutex>
#include <zstd.h>
#define REXLOG_ERROR(...) ((void)0)
'''
main=r'''
int main() {
  std::ifstream file("build/renut-switch/deko3d_glsl.bin",std::ios::binary|std::ios::ate);
  if(!file)return 1;
  std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
  file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
  if(!file)return 2;
  Package valid;
  if(!Attach(valid,bytes.data(),bytes.size(),"fixture") || valid.header->count!=2720 || valid.header->dictSize!=0)return 3;
  ZSTD_freeDCtx(valid.dctx);
  unsigned negative=0;
  {auto bad=bytes;bad[0]^=1;Package p;if(Attach(p,bad.data(),bad.size(),"bad magic"))return 4;++negative;}
  {auto bad=bytes;auto* h=reinterpret_cast<PackageHeader*>(bad.data());
   auto* e=reinterpret_cast<PackageEntry*>(bad.data()+h->tableOffset);e[1].hash=e[0].hash;
   Package p;if(Attach(p,bad.data(),bad.size(),"duplicate hash"))return 5;++negative;}
  {auto bad=bytes;auto* h=reinterpret_cast<PackageHeader*>(bad.data());
   auto* e=reinterpret_cast<PackageEntry*>(bad.data()+h->tableOffset);e[0].compressedSize=0xFFFFFFFFu;
   Package p;if(Attach(p,bad.data(),bad.size(),"bounds"))return 6;++negative;}
  std::printf("PASS: actual pinned ReNut Attach accepts 2720-entry package and rejects %u corruptions\n",negative);
}
'''
(ROOT/'analysis/fixture/package_attach.cpp').write_text(header+structs+attach+main)
print('Generated private fixture using actual ReNut Attach')
