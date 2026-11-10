// Guest memory on Horizon (Nintendo Switch).
//
// The Xbox 360 memory map needs a 4.5 GB contiguous window with several views
// that alias the same physical memory. Horizon cannot do that directly:
//   - there is no sparse reservation: the process gets ~3189 MB of RAM in total;
//   - svcMapProcessMemory refuses heap memory as its source (0xD401);
//   - page permissions on such mappings cannot be changed later.
//
// So: the window is reserved as address space only, memory is committed in 2 MB
// granules, every granule is turned into a code alias (which is what makes it
// mappable) and is mapped into a view the first time that view is touched.
//
// See port-switch/SWITCH_GUEST_MEMORY.md for the picture, and
// port-switch/SWITCH_PLATFORM_NOTES.md for the measurements this is based on.

#pragma once

#if defined(__SWITCH__)

#include <cstddef>
#include <cstdint>

namespace rex::memory::nx {

// Horizon accounts kernel memory per mapping, so granules are large: 64 KB
// commits ran the kernel out of blocks (2001-0103) at ~329 MB of backing.
inline constexpr size_t kGranule = 2u * 1024u * 1024u;

// Same values as rex::memory::PageAccess, so conversion is the identity.
inline constexpr uint32_t kAccessNone = 0;
inline constexpr uint32_t kAccessRead = 1;
inline constexpr uint32_t kAccessWrite = 1 | 2;

// Reserves the window. Default matches the guest memory map (0x120000000).
bool Init(size_t window_size = 0x120000000ull);
void Shutdown();

uint8_t* Base();
size_t WindowSize();

// Registers a view: `length` bytes at `base` reflect the mapping from `offset`.
// Several views may share the same offset — that is how the 360 mirrors are made.
bool AddView(uint8_t* base, size_t offset, size_t length);

// Window address -> mapping offset, according to the view that contains it.
bool WindowToOffset(uint64_t window_address, size_t* offset_out);

// Commits real memory for [offset, offset + length). Idempotent.
bool Commit(size_t offset, size_t length, uint32_t access);
bool Decommit(size_t offset, size_t length);

// Maps the granule that covers `window_address` into the view that contains it,
// if it is committed and not mapped yet. Cheap after the first call: one lookup
// and one flag test. Called from the guest memory access path.
bool EnsureMapped(uint64_t window_address);

// Logical permissions (the window itself cannot be re-protected on Horizon).
bool Protect(uint8_t* address, size_t length, uint32_t access, uint32_t* out_old);
bool QueryProtect(uint8_t* address, size_t* length, uint32_t* out_access);

// Shadow address of the committed granule, always accessible. Null if the
// address is not committed.
void* ShadowFor(uint64_t window_address);

size_t CommittedBytes();
size_t MappedBytes();

}  // namespace rex::memory::nx

#endif  // __SWITCH__
