#include "source_runtime.h"
#include <algorithm>
#include <rex/system/xmemory.h>
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/guest/callbacks.h"
#include "renut_engine/gpu/banjo_renut_checks.h"

namespace timeshift::renderer {
uint32_t SdkSourceRuntime::Allocate(uint32_t bytes){
  if(!bytes||bytes%65536)return 0;
  auto *heap=REX_KERNEL_MEMORY()->LookupHeapByType(true,65536);uint32_t address=0;
  if(!heap||!heap->Alloc(bytes,65536,
     rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
     rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite,true,&address))return 0;
  return address;
}
bool SdkSourceRuntime::Release(uint32_t address){
  auto *heap=REX_KERNEL_MEMORY()->LookupHeap(address);return heap&&heap->Release(address);
}
std::span<uint8_t> SdkSourceRuntime::Map(uint32_t address,uint32_t bytes){
  return MapChecked(address,bytes,true);
}
std::span<const uint8_t> SdkSourceRuntime::Read(uint32_t address,uint32_t bytes){
  return MapChecked(address,bytes,false);
}
std::span<uint8_t> SdkSourceRuntime::MapChecked(uint32_t address,uint32_t bytes,bool writable){
  if(!address||!bytes||uint64_t(address)+bytes>0x100000000ull)return {};
  auto *memory=REX_KERNEL_MEMORY();uint64_t at=address,end=at+bytes;
  auto *start=memory->TranslateVirtual<uint8_t*>(address);
  if(!start)return {};
  while(at<end){
    if(reinterpret_cast<uintptr_t>(memory->TranslateVirtual<uint8_t*>(uint32_t(at)))!=
       reinterpret_cast<uintptr_t>(start)+(at-address))return {};
    auto *heap=memory->LookupHeap(uint32_t(at));rex::memory::HeapAllocationInfo info{};
    if(!heap||!heap->page_size())return {};
    const auto aligned=heap->heap_base()+uint32_t((at-heap->heap_base())/heap->page_size())*heap->page_size();
    const auto required=rex::memory::kMemoryProtectRead|(writable?rex::memory::kMemoryProtectWrite:0);
    if(!heap->QueryRegionInfo(aligned,&info)||!(info.state&rex::memory::kMemoryAllocationCommit)||
       (info.protect&required)!=required)return {};
    const uint64_t next=uint64_t(info.base_address)+info.region_size;if(next<=at)return {};
    at=std::min(next,end);
  }
  return {start,bytes};
}
void SdkSourceRuntime::FlushSources(uint32_t device){
  renut::gpu::RunReadyCallbacks();renut::gpu::FlushDeferredVertices(device);
  banjo::renut_gpu::checks::QueueKickFlush();
}
uint64_t SdkSourceRuntime::LastCallback(){return renut::gpu::LastCallback();}
void SdkSourceRuntime::QueueCompletion(uint32_t address,uint32_t fence,uint64_t callback){
  renut::gpu::render_thread::QueueFenceCompletion(address,fence,callback);
}
uint32_t SdkSourceRuntime::Completed(uint32_t address){
  return renut::gpu::render_thread::ReadFenceCompletion(address);
}
void SdkSourceRuntime::RunCallbacks(){renut::gpu::RunReadyCallbacks();}
bool SdkSourceRuntime::WaitProgress(std::chrono::milliseconds timeout){
  return renut::gpu::render_thread::WaitForFenceProgress(timeout);
}
}
