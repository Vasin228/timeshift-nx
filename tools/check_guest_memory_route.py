"""Route test of the actual ReNut header, with a deliberately nonflat fake SDK.

The fake translator returns explicit registered host pointers, not a copied
translation formula. The original pinned header is a required negative control.
This tests dispatch, not real heap allocation or kernel execution.
"""
from pathlib import Path
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'analysis/guest-memory-route';out.mkdir(exist_ok=True)
sdk=out/'sdk/rex/system';sdk.mkdir(parents=True,exist_ok=True)
(sdk.parent/'types.h').write_text('#pragma once\n#include <cstdint>\nusing u8=uint8_t;using u32=uint32_t;\n')
(sdk/'kernel_state.h').write_text('''#pragma once
#include <rex/types.h>
#include <array>
#include <stdexcept>
namespace rex::memory {
struct Memory {
  unsigned translations=0;
  uint8_t *cache_base=nullptr;
  std::array<uint8_t,64> bytes{};
  uint8_t *virtual_membase(){return cache_base;}
  uint8_t *physical_membase(){return cache_base;}
  template<class T>T TranslateVirtual(uint32_t va){
    ++translations;
    switch(va){
      case 0x10000:return reinterpret_cast<T>(bytes.data()+1);
      case 0xA0010000:return reinterpret_cast<T>(bytes.data()+7);
      case 0xC0010000:return reinterpret_cast<T>(bytes.data()+13);
      case 0xE0010000:return reinterpret_cast<T>(bytes.data()+29);
      case 0xFFFF0000:return reinterpret_cast<T>(bytes.data()+43);
      default:throw std::runtime_error("unexpected SDK translation");
    }
  }
};inline Memory memory;
}
#define REX_KERNEL_MEMORY() (&::rex::memory::memory)
''')
source=out/'route.cpp'
source.write_text('''#include "guest_memory.h"
#include <cstdio>
#include <sys/mman.h>
int main(){
  auto &m=rex::memory::memory;
  m.cache_base=static_cast<uint8_t*>(mmap(nullptr,1ull<<32,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));
  if(m.cache_base==MAP_FAILED)return 5;
  renut::gpu::detail::g_guest_vbase.store(m.cache_base);
  renut::gpu::detail::g_guest_pbase.store(m.cache_base);
  const uint32_t va[]={0x10000,0xA0010000,0xC0010000,0xE0010000,0xFFFF0000};
  const unsigned offsets[]={1,7,13,29,43};
  if(renut::gpu::GuestPtr<uint8_t>(0)!=nullptr)return 1;
  for(unsigned i=0;i<5;++i)
    if(renut::gpu::GuestPtr<uint8_t>(va[i])!=m.bytes.data()+offsets[i])return 2;
  if(m.translations!=5)return 3;
  if(renut::gpu::GuestPhysicalPtr<uint8_t>(0xA0000007)!=m.cache_base+7)return 4;
  munmap(m.cache_base,1ull<<32);
  std::puts("PASS: actual header routes five guest VAs through SDK; null and raw physical routes preserved");
}
''')
up=ROOT/'upstream/sa2-nx'
current=up/'app/src/renut_engine/gpu/guest'
old=out/'old';old.mkdir(exist_ok=True)
prior=subprocess.run(['git','show','HEAD:app/src/renut_engine/gpu/guest/guest_memory.h'],cwd=up,capture_output=True,check=True).stdout
(old/'guest_memory.h').write_bytes(prior)
for label,header,expected in [('ported',current,0),('negative-control',old,2)]:
    exe=out/label
    subprocess.run(['clang++','-std=c++20','-O0','-DTIMESHIFT_RECOMP','-DREX_PLATFORM_SWITCH=1',
                    '-I'+str(out/'sdk'),'-I'+str(header),str(source),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    assert result.returncode==expected,(label,result.returncode,result.stdout,result.stderr)
report=dict(success=True,ported_exit=0,original_pinned_negative_exit=2,
            limitation='Actual-header routing with fake SDK; not real virtual/physical allocation proof')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: TimeShift Switch SDK route; original pinned flat path fails negative control')
