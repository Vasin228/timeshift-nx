/**
 * @file        core/mapped_memory_switch.cpp
 * @brief       Switch (Horizon OS) MappedMemory implementation using memory buffers
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <rex/filesystem.h>
#include <rex/memory/mapped_memory.h>
#include <rex/platform.h>

namespace rex::memory {

class SwitchMappedMemory : public MappedMemory {
 public:
  SwitchMappedMemory(void* data, size_t size, FILE* file, bool owns_data)
      : MappedMemory(data, size), file_(file), owns_data_(owns_data) {}

  ~SwitchMappedMemory() override {
    Close(0);
  }

  void Close(uint64_t /*truncate_size*/) override {
    if (data_ && owns_data_) {
      std::free(data_);
      data_ = nullptr;
    }
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  void Flush() override {
    if (file_ && data_) {
      std::fflush(file_);
    }
  }

 private:
  FILE* file_ = nullptr;
  bool owns_data_ = false;
};

std::unique_ptr<MappedMemory> MappedMemory::Open(const std::filesystem::path& path, Mode mode,
                                                 size_t offset, size_t length) {
  const char* fmode = (mode == Mode::kReadWrite) ? "r+b" : "rb";
  FILE* f = std::fopen(path.c_str(), fmode);
  if (!f) {
    return nullptr;
  }

  std::fseek(f, 0, SEEK_END);
  size_t file_size = static_cast<size_t>(std::ftell(f));
  std::fseek(f, static_cast<long>(offset), SEEK_SET);

  size_t map_len = length ? length : (file_size - offset);
  void* buf = std::malloc(map_len);
  if (!buf) {
    std::fclose(f);
    return nullptr;
  }

  size_t bytes_read = std::fread(buf, 1, map_len, f);
  (void)bytes_read;

  return std::make_unique<SwitchMappedMemory>(buf, map_len, f, true);
}

std::unique_ptr<ChunkedMappedMemoryWriter> ChunkedMappedMemoryWriter::Open(
    const std::filesystem::path& /*path*/, size_t /*chunk_size*/, bool /*low_address_space*/) {
  return nullptr;
}

}  // namespace rex::memory
