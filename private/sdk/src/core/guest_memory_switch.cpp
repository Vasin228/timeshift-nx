// Implementation of the Horizon guest memory layer. See the header for the why.

#if defined(__SWITCH__)

#include "rex/memory/guest_memory_switch.h"

#include <malloc.h>
#include <string.h>
#include <switch.h>

#include <map>
#include <mutex>
#include <vector>

#include <rex/logging.h>

namespace rex::memory::nx {
namespace {

static Handle GetProcHandle() {
  Handle h = envGetOwnProcessHandle();
  return h != 0 ? h : CUR_PROCESS_HANDLE;
}

struct View {
  uint8_t* base = nullptr;
  size_t offset = 0;
  size_t length = 0;
  std::vector<bool> mapped;  // per granule of the mapping
};

struct Chunk {
  void* backing = nullptr;  // heap pointer; INVALID once turned into a code alias
  uint8_t* alias = nullptr; // always-accessible shadow (== backing in emulator mode)
  uint32_t access = kAccessNone;
};

std::mutex g_mutex;
uint8_t* g_base = nullptr;
size_t g_size = 0;
VirtmemReservation* g_reservation = nullptr;
std::vector<View> g_views;
std::map<size_t, Chunk> g_chunks;  // key: granule index (offset / kGranule)
size_t g_committed = 0;

constexpr size_t kPage = 0x1000;

// Returns true if the address is inside any view.
bool FindView(uint64_t window_address, size_t* view_index, size_t* offset) {
  for (size_t i = 0; i < g_views.size(); ++i) {
    const View& v = g_views[i];
    const uint64_t start = reinterpret_cast<uint64_t>(v.base);
    if (window_address < start || window_address >= start + v.length) {
      continue;
    }
    if (view_index) {
      *view_index = i;
    }
    if (offset) {
      *offset = v.offset + static_cast<size_t>(window_address - start);
    }
    return true;
  }
  return false;
}

// Turns a heap block into a code alias, which is the only thing Horizon lets us
// map into the window. Returns the alias, or null on failure.
uint8_t* MakeCodeAlias(void* backing, size_t size) {
  virtmemLock();
  void* alias = virtmemFindCodeMemory(size, 0);
  if (!alias) {
    virtmemUnlock();
    REXLOG_ERROR("MakeCodeAlias: virtmemFindCodeMemory failed for size 0x{:X}", size);
    return nullptr;
  }
  const Result rc_map =
      svcMapProcessCodeMemory(GetProcHandle(), reinterpret_cast<u64>(alias),
                              reinterpret_cast<u64>(backing), size);
  virtmemUnlock();
  if (R_FAILED(rc_map)) {
    REXLOG_ERROR("MakeCodeAlias: svcMapProcessCodeMemory failed rc=0x{:X}", rc_map);
    return nullptr;
  }
  const Result rc_perm = svcSetProcessMemoryPermission(
      GetProcHandle(), reinterpret_cast<u64>(alias), size, Perm_R | Perm_W);
  if (R_FAILED(rc_perm)) {
    REXLOG_ERROR("MakeCodeAlias: svcSetProcessMemoryPermission failed rc=0x{:X}", rc_perm);
    svcUnmapProcessCodeMemory(GetProcHandle(), reinterpret_cast<u64>(alias),
                              reinterpret_cast<u64>(backing), size);
    return nullptr;
  }
  return static_cast<uint8_t*>(alias);
}

bool MapIntoView(size_t view_index, size_t granule, Chunk& chunk) {
  View& v = g_views[view_index];
  const size_t granule_start = granule * kGranule;
  if (granule_start < v.offset) {
    return false;  // view does not cover this granule
  }
  const size_t into_view = granule_start - v.offset;
  if (into_view >= v.length) {
    return false;
  }
  const size_t remaining = v.length - into_view;
  size_t length = remaining < kGranule ? remaining : kGranule;
  length &= ~(kPage - 1);  // mapping must be page multiples
  if (length == 0) {
    return false;
  }
  if (v.mapped.size() > granule && v.mapped[granule]) {
    return true;  // already mapped
  }
  void* dst = v.base + into_view;

  const Result rc = svcMapProcessMemory(dst, GetProcHandle(),
                                       reinterpret_cast<u64>(chunk.alias), length);
  if (R_FAILED(rc)) {
    REXLOG_ERROR("MapIntoView failed: view={} dst={:p} alias={:p} len=0x{:X} rc=0x{:X}",
                 view_index, static_cast<void*>(dst), static_cast<void*>(chunk.alias), length, rc);
    return false;
  }
  if (v.mapped.size() <= granule) {
    v.mapped.resize(granule + 1, false);
  }
  v.mapped[granule] = true;
  return true;
}

void UnmapFromView(size_t view_index, size_t granule, Chunk& chunk) {
  View& v = g_views[view_index];
  if (v.mapped.size() <= granule || !v.mapped[granule]) {
    return;
  }
  const size_t into_view = granule * kGranule - v.offset;
  const size_t remaining = v.length - into_view;
  size_t length = remaining < kGranule ? remaining : kGranule;
  length &= ~(kPage - 1);
  if (length) {
    svcUnmapProcessMemory(v.base + into_view, GetProcHandle(),
                          reinterpret_cast<u64>(chunk.alias), length);
  }
  v.mapped[granule] = false;
}

}  // namespace

bool Init(size_t window_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_base) {
    return true;
  }

  virtmemLock();
  void* addr = virtmemFindAslr(window_size, 0);
  if (addr) {
    g_reservation = virtmemAddReservation(addr, window_size);
    if (!g_reservation) {
      addr = nullptr;
    }
  }
  virtmemUnlock();
  if (!addr) {
    REXLOG_ERROR("nx::Init: virtmemFindAslr failed for size 0x{:X}", window_size);
    return false;
  }
  g_base = static_cast<uint8_t*>(addr);
  g_size = window_size;
  g_committed = 0;
  REXLOG_INFO("nx::Init: reserved 0x{:X} bytes at {:p}",
              window_size, static_cast<void*>(g_base));
  return true;
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& [granule, chunk] : g_chunks) {
    for (size_t v = 0; v < g_views.size(); ++v) {
      UnmapFromView(v, granule, chunk);
    }
    svcUnmapProcessCodeMemory(GetProcHandle(),
                              reinterpret_cast<u64>(chunk.alias),
                              reinterpret_cast<u64>(chunk.backing), kGranule);
    free(chunk.backing);
  }
  g_chunks.clear();
  g_views.clear();
  if (g_reservation) {
    virtmemLock();
    virtmemRemoveReservation(g_reservation);
    g_reservation = nullptr;
    virtmemUnlock();
  }
  g_base = nullptr;
  g_size = 0;
  g_committed = 0;
}

uint8_t* Base() { return g_base; }
size_t WindowSize() { return g_size; }

bool AddView(uint8_t* base, size_t offset, size_t length) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_base) {
    return false;
  }
  const uint64_t view_begin = reinterpret_cast<uint64_t>(base);
  const uint64_t window_begin = reinterpret_cast<uint64_t>(g_base);
  const uint64_t window_end = window_begin + g_size;
  if (view_begin < window_begin || view_begin + length > window_end) {
    return false;
  }
  const size_t view_idx = g_views.size();
  g_views.push_back(View{base, offset, length, {}});

  // Map any already-committed granules that fall into this view
  for (auto& [granule, chunk] : g_chunks) {
    MapIntoView(view_idx, granule, chunk);
  }
  return true;
}

bool WindowToOffset(uint64_t window_address, size_t* offset_out) {
  return FindView(window_address, nullptr, offset_out);
}

bool Commit(size_t offset, size_t length, uint32_t access) {
  if (!length) {
    return true;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_base) {
    return false;
  }
  const size_t first = offset / kGranule;
  const size_t last = (offset + length + kGranule - 1) / kGranule;
  if (last * kGranule > g_size) {
    return false;  // outside the window
  }

  for (size_t granule = first; granule < last; ++granule) {
    auto it = g_chunks.find(granule);
    if (it != g_chunks.end()) {
      it->second.access |= access;
      // Ensure mapped in all views covering this granule
      for (size_t v = 0; v < g_views.size(); ++v) {
        MapIntoView(v, granule, it->second);
      }
      continue;
    }
    void* backing = memalign(kGranule, kGranule);
    if (!backing) {
      REXLOG_ERROR("Commit: memalign failed for granule {}", granule, g_committed / (1024 * 1024));
      return false;
    }
    memset(backing, 0, kGranule);
    uint8_t* alias = MakeCodeAlias(backing, kGranule);
    if (!alias) {
      free(backing);
      return false;
    }
    Chunk chunk{backing, alias, access};
    g_chunks[granule] = chunk;
    g_committed += kGranule;

    // Immediately map into all registered views that cover this granule!
    for (size_t v = 0; v < g_views.size(); ++v) {
      MapIntoView(v, granule, chunk);
    }
  }
  return true;
}

bool Decommit(size_t offset, size_t length) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const size_t first = offset / kGranule;
  const size_t last = (offset + length + kGranule - 1) / kGranule;
  for (size_t granule = first; granule < last; ++granule) {
    auto it = g_chunks.find(granule);
    if (it == g_chunks.end()) {
      continue;
    }
    Chunk& chunk = it->second;
    for (size_t v = 0; v < g_views.size(); ++v) {
      UnmapFromView(v, granule, chunk);
    }
    svcUnmapProcessCodeMemory(GetProcHandle(),
                              reinterpret_cast<u64>(chunk.alias),
                              reinterpret_cast<u64>(chunk.backing), kGranule);
    free(chunk.backing);
    g_chunks.erase(it);
    g_committed -= kGranule;
  }
  return true;
}

bool EnsureMapped(uint64_t window_address) {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t view_index = 0;
  size_t offset = 0;
  if (!FindView(window_address, &view_index, &offset)) {
    return false;
  }
  const size_t granule = offset / kGranule;
  View& v = g_views[view_index];
  if (v.mapped.size() > granule && v.mapped[granule]) {
    return true;  // already mapped
  }
  auto it = g_chunks.find(granule);
  if (it == g_chunks.end()) {
    return false;  // not committed
  }
  return MapIntoView(view_index, granule, it->second);
}

bool Protect(uint8_t* address, size_t length, uint32_t access, uint32_t* out_old) {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t offset = 0;
  if (!address || !FindView(reinterpret_cast<uint64_t>(address), nullptr, &offset)) {
    return false;
  }
  const size_t first = offset / kGranule;
  const size_t last = (offset + length + kGranule - 1) / kGranule;
  uint32_t old = kAccessNone;
  for (size_t granule = first; granule < last; ++granule) {
    auto it = g_chunks.find(granule);
    if (it == g_chunks.end()) {
      continue;
    }
    old |= it->second.access;
    if (length >= kGranule) {
      it->second.access = access;
      if (!(access & 1)) {
        // No read access on full granule: unmap from all views
        for (size_t v = 0; v < g_views.size(); ++v) {
          UnmapFromView(v, granule, it->second);
        }
      } else {
        // Restore mapping
        for (size_t v = 0; v < g_views.size(); ++v) {
          MapIntoView(v, granule, it->second);
        }
      }
    } else {
      if (access & 1) {
        it->second.access |= access;
        for (size_t v = 0; v < g_views.size(); ++v) {
          MapIntoView(v, granule, it->second);
        }
      }
      // Sub-granule protect without read access (e.g. 4KB/64KB stack guard page)
      // must NOT unmap the entire 2 MB granule containing active stack/heap memory.
    }
  }
  if (out_old) {
    *out_old = old;
  }
  return true;
}

bool QueryProtect(uint8_t* address, size_t* length, uint32_t* out_access) {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t offset = 0;
  if (!address || !FindView(reinterpret_cast<uint64_t>(address), nullptr, &offset)) {
    return false;
  }
  const size_t granule = offset / kGranule;
  auto it = g_chunks.find(granule);
  if (it == g_chunks.end()) {
    return false;
  }
  if (out_access) {
    *out_access = it->second.access;
  }
  if (length) {
    const size_t granule_end = (granule + 1) * kGranule;
    const size_t remaining = granule_end - offset;
    if (*length > remaining) {
      *length = remaining;
    }
  }
  return true;
}

void* ShadowFor(uint64_t window_address) {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t offset = 0;
  if (!FindView(window_address, nullptr, &offset)) {
    return nullptr;
  }
  const size_t granule = offset / kGranule;
  auto it = g_chunks.find(granule);
  if (it == g_chunks.end()) {
    return nullptr;
  }
  return it->second.alias + (offset - granule * kGranule);
}

size_t CommittedBytes() { return g_committed; }

size_t MappedBytes() {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t mapped = 0;
  for (const View& v : g_views) {
    for (size_t i = 0; i < v.mapped.size(); ++i) {
      if (v.mapped[i]) {
        mapped += kGranule;
      }
    }
  }
  return mapped;
}

}  // namespace rex::memory::nx

#endif  // __SWITCH__
