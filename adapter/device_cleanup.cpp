#include "device_cleanup.h"
#include "device_bootstrap.h"
#include <rex/ppc.h>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>

REX_EXTERN(__imp__sub_826477C8);
REX_EXTERN(__imp__sub_8263D140);
REX_EXTERN(__imp__sub_8263D4A8);
REX_EXTERN(__imp__sub_8264DCF0);
namespace timeshift::renderer {
namespace {
uint32_t Word(std::span<const uint8_t> bytes,uint32_t at=0){
 if(uint64_t(at)+4>bytes.size())throw std::out_of_range("device cleanup word");
 return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
void Put(std::span<uint8_t> bytes,uint32_t at,uint32_t value){
 if(uint64_t(at)+4>bytes.size())throw std::out_of_range("device cleanup word");
 for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(24-i*8));
}
}
bool ReleaseGuestDeviceBindings(PPCContext &parent,uint8_t *base,SourceRuntime &memory,uint32_t device){
 auto bytes=memory.Map(device,22400);if(bytes.size()!=22400)throw std::out_of_range("device cleanup storage");
 if(!(bytes[10940]&0x80))throw std::logic_error("native cleanup requires retained no-hardware destructor");
 const std::array<uint32_t,3>fields{14812,14816,14808};
 std::array<uint32_t,3>resources{};std::unordered_map<uint32_t,uint32_t>decrements;
 for(unsigned i=0;i<fields.size();++i)if((resources[i]=Word(bytes,fields[i])))++decrements[resources[i]];
 // Aliased automatic fields each own a reference, as the original teardown's
 // three Release calls require. Do not decrement anything on invalid input.
 for(auto [resource,count]:decrements){
  auto header=memory.Map(resource,16);
  if(header.size()!=16||Word(header,4)<count)throw std::logic_error("automatic resource reference contract");
 }
 const auto import=memory.Read(d3d6274::kGlobalDeviceImport,4);
 const auto global=memory.Read(Word(import),4);
 if(Word(global)!=device){
  // A failed Create already restored the previous global. The retained
  // resource destructor uses that global for busy waits. Never hijack it or
  // clear busy tags to manufacture completion for an ambiguous failed device.
  for(auto resource:resources)if(resource&&Word(memory.Read(resource,12),8))return false;
 }
 if(parent.r1.u32<4096||memory.Map(parent.r1.u32-4096,4096).size()!=4096)
  throw std::out_of_range("device cleanup guest stack");
 auto call=[&](PPCFunc *function,std::initializer_list<uint32_t>words){
  auto context=parent;PPCRegister *arguments[]={&context.r3,&context.r4,&context.r5};
  if(words.size()>3)throw std::logic_error("device cleanup arguments");
  unsigned i=0;for(auto word:words)arguments[i++]->u64=word;
  function(context,base);parent.fpscr=context.fpscr;
 };
 call(__imp__sub_826477C8,{device});
 // UnbindAll preserves the automatic targets; detach them explicitly using
 // the original setters. Binding pointers do not own resource references.
 call(__imp__sub_8263D4A8,{device,0});
 for(uint32_t slot=0;slot<4;++slot)call(__imp__sub_8263D140,{device,slot,0});
 for(unsigned i=0;i<fields.size();++i)if(resources[i]){
  call(__imp__sub_8264DCF0,{resources[i]});
  Put(bytes,fields[i],0);
 }
 // Do not free side buffer16712/helper21552 here: the retained no-hardware
 // destructor owns both, and must run only after source ownership closes.
 return true;
}
}
