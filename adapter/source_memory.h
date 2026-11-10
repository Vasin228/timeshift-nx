#pragma once
#include "device_bootstrap.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <vector>
struct PPCContext;

namespace timeshift::renderer {
class SourceRuntime;
class DeviceCleanup {
 public:
  virtual ~DeviceCleanup()=default;
  // Poll on a quiescent guest thread. False retains the operation/device for
  // retry; exceptions prohibit replay of potentially partial CPU destruction.
  virtual bool Poll(PPCContext &context,uint8_t *base,SourceRuntime &runtime,
                    std::chrono::milliseconds budget)=0;
};
// The SDK implementation uses real physical heaps and ReNut's existing FIFO.
class SourceRuntime {
 public:
  virtual ~SourceRuntime()=default;
  virtual uint32_t Allocate(uint32_t bytes)=0;
  virtual bool Release(uint32_t address)=0;
  virtual std::span<uint8_t> Map(uint32_t address,uint32_t bytes)=0;
  virtual std::span<const uint8_t> Read(uint32_t address,uint32_t bytes){return Map(address,bytes);}
  virtual void FlushSources(uint32_t device)=0;
  virtual uint64_t LastCallback()=0;
  virtual void QueueCompletion(uint32_t address,uint32_t fence,uint64_t after_callback)=0;
  virtual uint32_t Completed(uint32_t address)=0;
  virtual void RunCallbacks()=0;
  virtual bool WaitProgress(std::chrono::milliseconds timeout)=0;
  virtual std::unique_ptr<DeviceCleanup> BeginDeviceCleanup(uint32_t){return {};}
  virtual void DeviceActivated(uint32_t,bool){}
  virtual void DeviceRetired(uint32_t,bool){}
};
struct SourceConfig {
  uint32_t packet_bytes=4u<<20,guarantee_margin=256u<<10;
  uint32_t chunk_bytes=1u<<20,source_budget=16u<<20;
  std::chrono::milliseconds allocation_wait{1000};
};
// One instance per device. Calls for one device must be serialized by its
// lifecycle owner. Stop new work before Close; retain the guest device until
// Close succeeds. Destruction never frees attached/unretired guest memory.
class DeviceSources {
 public:
  explicit DeviceSources(SourceRuntime &runtime,SourceConfig config={});
  ~DeviceSources();
  DeviceSources(const DeviceSources&)=delete;
  DeviceSources& operator=(const DeviceSources&)=delete;
  bool Ready()const{return packet_&&fence_;}
  d3d6274::BootstrapMemory BootstrapMemory()const;
  bool Attach(uint32_t device);
  uint32_t AllocateInline(uint32_t words,uint32_t alignment);
  std::optional<uint32_t> Submit();
  bool Wait(uint32_t fence,std::chrono::milliseconds budget);
  bool BeginSegment();
  bool EndSegment(); // Original out-of-memory result, not completion status.
  bool Close(std::chrono::milliseconds budget);
  bool Pending(uint32_t fence);
  void RewindPackets();
  void FlushSources(uint32_t device=0){runtime_.FlushSources(device?device:device_);}
  uint32_t SourceBytes()const{return source_bytes_;}
  bool InSegment()const{return segment_;}
 private:
  enum class Status{Free,Open,Sealed};
  struct Chunk{uint32_t address,bytes,used=0,tag=0;Status status=Status::Free;};
  SourceRuntime &runtime_;SourceConfig config_;
  uint32_t packet_=0,fence_=0,device_=0,next_tag_=0,source_bytes_=0;
  std::vector<Chunk> chunks_;
  bool segment_=false,closing_=false,faulted_=false;
  std::optional<uint32_t> close_fence_;
  std::span<uint8_t> Device();
  void Collect();
  std::optional<uint32_t> Seal();
  uint32_t FailAllocation();
  bool ReleaseAll();
};
}
