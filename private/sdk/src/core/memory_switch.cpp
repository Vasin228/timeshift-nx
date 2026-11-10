// rex::memory platform layer for Horizon (Nintendo Switch).
//
// Mirrors src/core/memory_posix.cpp, but backed by the Switch guest memory layer
// (guest_memory_switch.{h,cpp}): a reserved 4.5 GB window whose granules are
// committed and mapped on demand.
//
// Two kinds of allocations arrive here:
//   1. guest addresses (inside the window)  -> Commit/Decommit in the window;
//   2. host allocations (base == nullptr)   -> plain aligned_alloc.
//
// There is no file mmap on Horizon, so the guest window is not backed by a file;
// CreateFileMappingHandle only records the size and returns a handle.

#if defined(__SWITCH__)

#include <malloc.h>
#include <string.h>

#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/utils.h>

#include "rex/memory/guest_memory_switch.h"

namespace rex::memory {
namespace {

constexpr size_t kSwitchPageSize = 0x1000;
constexpr size_t kSwitchGranularity = 0x10000;

uint32_t ToRexGmAccess(PageAccess access) {
  switch (access) {
    case PageAccess::kNoAccess:
      return nx::kAccessNone;
    case PageAccess::kReadOnly:
    case PageAccess::kExecuteReadOnly:
      return nx::kAccessRead;
    case PageAccess::kReadWrite:
    case PageAccess::kExecuteReadWrite:
    default:
      return nx::kAccessWrite;
  }
}

PageAccess FromRexGmAccess(uint32_t access) {
  if (access == nx::kAccessNone) {
    return PageAccess::kNoAccess;
  }
  if (access == nx::kAccessRead) {
    return PageAccess::kReadOnly;
  }
  return PageAccess::kReadWrite;
}

bool InWindow(void* address, size_t* offset_out) {
  if (!address) {
    return false;
  }
  return nx::WindowToOffset(reinterpret_cast<uint64_t>(address), offset_out);
}

// File mapping handles on Switch are bookkeeping only (see the file comment).
struct SwitchMapping {
  size_t size = 0;
};
SwitchMapping g_mapping;

}  // namespace

size_t page_size() { return kSwitchPageSize; }
size_t allocation_granularity() { return kSwitchGranularity; }

bool IsWritableExecutableMemorySupported() {
  // Guest code is compiled ahead of time, never JIT'ed, so the runtime never
  // needs W+X data pages on the console.
  return false;
}


void* AllocFixed(void* base_address, size_t length, AllocationType allocation_type,
                 PageAccess access) {
  size_t offset = 0;
  if (InWindow(base_address, &offset)) {
    const bool commit =
        (static_cast<uint32_t>(allocation_type) & static_cast<uint32_t>(AllocationType::kCommit)) != 0;
    if (commit) {
      if (!nx::Commit(offset, length, ToRexGmAccess(access))) {
        REXLOG_ERROR("Switch: guest commit failed (offset 0x{:X}, length 0x{:X})", offset, length);
        return nullptr;
      }
    }
    return base_address;
  }
  if (base_address) {
    // A fixed host address cannot be honoured without the window machinery.
    return nullptr;
  }
  const size_t aligned = rex::round_up<size_t>(length ? length : 1, kSwitchGranularity);
  void* result = memalign(kSwitchGranularity, aligned);
  return result;
}

bool DeallocFixed(void* base_address, size_t length, DeallocationType deallocation_type) {
  size_t offset = 0;
  if (InWindow(base_address, &offset)) {
    if ((static_cast<uint32_t>(deallocation_type) &
         static_cast<uint32_t>(DeallocationType::kDecommit)) != 0) {
      return nx::Decommit(offset, length);
    }
    return true;
  }
  free(base_address);
  return true;
}

bool Protect(void* base_address, size_t length, PageAccess access, PageAccess* out_old_access) {
  size_t offset = 0;
  if (InWindow(base_address, &offset)) {
    uint32_t old = nx::kAccessNone;
    if (!nx::Protect(static_cast<uint8_t*>(base_address), length, ToRexGmAccess(access), &old)) {
      return false;
    }
    if (out_old_access) {
      *out_old_access = FromRexGmAccess(old);
    }
    return true;
  }
  // Host allocations are plain newlib memory; Horizon cannot re-protect them.
  return true;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
  size_t offset = 0;
  if (InWindow(base_address, &offset)) {
    uint32_t access = nx::kAccessNone;
    if (!nx::QueryProtect(static_cast<uint8_t*>(base_address), &length, &access)) {
      return false;
    }
    access_out = FromRexGmAccess(access);
    return true;
  }
  access_out = PageAccess::kReadWrite;
  return true;
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path, size_t length,
                                          PageAccess access, bool commit) {
  (void)path;
  (void)commit;  // nothing is backed at creation time
  g_mapping.size = length;
  return 1;
}

void CloseFileMappingHandle(FileMappingHandle handle, const std::filesystem::path& path) {
  (void)handle;
  (void)path;
  g_mapping.size = 0;
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length, PageAccess access,
                  size_t file_offset) {
  (void)handle;
  (void)access;
  // The Switch runtime does not go through this path: xmemory's Switch branch
  // reserves the window with nx::Init() and registers views with nx::AddView().
  // Kept for API completeness.
  if (!base_address) {
    if (!nx::Base()) {
      return nullptr;
    }
    return nx::Base() + file_offset;
  }
  return base_address;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address, size_t length) {
  (void)handle;
  (void)base_address;
  (void)length;
  return true;
}

}  // namespace rex::memory

#endif  // __SWITCH__
