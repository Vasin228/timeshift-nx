#pragma once
#include "source_memory.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
struct PPCContext;

namespace timeshift::renderer {
class DeviceRegistry {
 public:
  enum class Phase{Creating,Active,Closing,Quarantined,Retired};
  struct Entry {
    explicit Entry(SourceRuntime &r,SourceConfig c):sources(r,c),creator(std::this_thread::get_id()){}
    DeviceSources sources;
    std::recursive_mutex serial;
    std::thread::id creator;
    uint32_t address=0,global=0;
    bool title=false;
    bool retire_continuations=false,cleanup_faulted=false;
    std::optional<uint32_t> cleanup_fence;
    uint64_t cleanup_callback=0;
    std::unique_ptr<DeviceCleanup> cleanup;
    uint32_t leases=0;
    Phase phase=Phase::Creating;
    void FlushSources(uint32_t device=0){sources.FlushSources(device?device:address);}
  };
  // Keep this lease for the WHOLE guest/native operation. Never cache a bare
  // Entry/DeviceSources pointer past it. No registry lock is held in guest code.
  class Lease {
   public:
    Lease()=default;
    ~Lease(){Drop();}
    Lease(Lease&&other)noexcept:entry_(std::move(other.entry_)),raw_entry_(std::exchange(other.raw_entry_,nullptr)),lock_(std::move(other.lock_)),counted_(std::exchange(other.counted_,false)){}
    Lease& operator=(Lease&&other)noexcept{
      if(this!=&other){Drop();entry_=std::move(other.entry_);raw_entry_=std::exchange(other.raw_entry_,nullptr);lock_=std::move(other.lock_);counted_=std::exchange(other.counted_,false);}return *this;
    }
    explicit operator bool()const{return raw_entry_!=nullptr;}
    Entry *operator->()const{return raw_entry_;}
    Entry &operator*()const{return *raw_entry_;}
    void FlushSources(uint32_t device=0){if(raw_entry_)raw_entry_->FlushSources(device);}
   private:
    friend class DeviceRegistry;
    explicit Lease(std::shared_ptr<Entry> p):entry_(std::move(p)),raw_entry_(entry_.get()),lock_(raw_entry_->serial){}
    Lease(Entry *p,std::unique_lock<std::recursive_mutex> lk,bool counted):raw_entry_(p),lock_(std::move(lk)),counted_(counted){}
    std::shared_ptr<Entry> entry_;
    Entry *raw_entry_=nullptr;
    std::unique_lock<std::recursive_mutex> lock_;
    bool counted_=false;
    void Drop()noexcept{
      if(raw_entry_&&counted_)--raw_entry_->leases;
      counted_=false;raw_entry_=nullptr;if(lock_.owns_lock())lock_.unlock();entry_.reset();
    }
  };
  explicit DeviceRegistry(SourceRuntime &runtime,SourceConfig config={}):runtime_(runtime),config_(config){}
  uint32_t Create(PPCContext &context,uint8_t *base);
  uint32_t Release(PPCContext &context,uint8_t *base,uint32_t device,
                   std::chrono::milliseconds budget=std::chrono::milliseconds(1000));
  void RetryPending(PPCContext &context,uint8_t *base);
  Lease Acquire(uint32_t device,bool continuation=false);
  Lease AcquireIfOwned(uint32_t device,bool continuation,bool &owned);
  SourceRuntime &Memory(){return runtime_;}
  bool Contains(uint32_t device);
  uint32_t AllocateInline(uint32_t device,uint32_t words,uint32_t alignment);
  uint32_t Kick(uint32_t device);
  uint32_t PacketSpace(uint32_t device,uint32_t words);
  bool Wait(uint32_t device,uint32_t fence,std::chrono::milliseconds budget);
  // The public TimeShift entry receives ONLY a fence, and uses VdGlobalDevice.
  // nullopt retains the original behavior for a genuinely unowned device.
  std::optional<bool> QueryGlobalFence(uint32_t fence);
  std::optional<uint32_t> ReadOwnedCompletion(uint32_t device,uint32_t completion);
 private:
  SourceRuntime &runtime_;SourceConfig config_;
  std::mutex mutex_;
  std::vector<std::pair<uint32_t,std::shared_ptr<Entry>>> entries_;
  std::atomic<uint32_t> fast_addr_{0};
  std::atomic<Entry*> fast_entry_{nullptr};
  std::shared_ptr<Entry> Find(uint32_t device);
  bool Bind(const std::shared_ptr<Entry>&entry,uint32_t device);
  bool Retire(PPCContext &context,uint8_t *base,const std::shared_ptr<Entry>&entry,std::chrono::milliseconds budget);
  void Erase(const std::shared_ptr<Entry>&entry);
};
bool DeviceLifecycleHooksEnabled();
DeviceRegistry &DeviceRegistryRuntime();
}
