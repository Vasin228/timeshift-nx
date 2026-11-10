/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/assert.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/memory.h>

namespace rex::graphics {

struct RegisterInfo {
  enum class Type {
    kDword,
    kFloat,
  };
  Type type;
  const char* name;
};

class RegisterFile {
 public:
  RegisterFile();

  static const RegisterInfo* GetRegisterInfo(uint32_t index);

  static constexpr size_t kRegisterCount = 0x5003;
  uint32_t values[kRegisterCount];

  const uint32_t& operator[](uint32_t reg) const { return values[reg]; }
  uint32_t& operator[](uint32_t reg) { return values[reg]; }

  template <typename T>
  T Get(uint32_t reg) const {
    return rex::memory::Reinterpret<T>(values[reg]);
  }
  template <typename T>
  T Get(Register reg) const {
    return Get<T>(static_cast<uint32_t>(reg));
  }
  template <typename T>
  T Get() const {
    return Get<T>(T::register_index);
  }

  xenos::xe_gpu_vertex_fetch_t GetVertexFetch(uint32_t index) const {
    // There are 96 vertex fetch constants (kVertexFetchConstantCount), packed
    // three per six-dword texture fetch slot: slot s holds vertex fetches 3s,
    // 3s + 1 and 3s + 2, two dwords each (type + address, endian + size).
    //
    // Using the six-dword slot as the stride was wrong: the shader of a scene
    // draw addressed fetch constants 93, 94 and 95 (the three pairs inside slot
    // 31, visible in the dumped SPIR-V as dwords 186..191 of the fetch block),
    // while the accessor handed the backend the pair at slot 31, dword 0 - the
    // data was uploaded from, and read at, addresses that do not belong to those
    // constants, so every vertex came from unrelated memory.
    assert_true(index < xenos::kVertexFetchConstantCount);
    xenos::xe_gpu_vertex_fetch_t fetch;
    std::memcpy(&fetch, &values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 2], sizeof(fetch));
    return fetch;
  }

  xenos::xe_gpu_texture_fetch_t GetTextureFetch(uint32_t index) const {
    assert_true(index < 32);
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(
        &fetch,
        &values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (sizeof(fetch) / sizeof(uint32_t)) * index],
        sizeof(fetch));
    return fetch;
  }

  xenos::xe_gpu_memexport_stream_t GetMemExportStream(uint32_t float_constant_index) const {
    assert_true(float_constant_index < 512);
    xenos::xe_gpu_memexport_stream_t stream;
    std::memcpy(&stream, &values[XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * float_constant_index],
                sizeof(stream));
    return stream;
  }
};

}  // namespace rex::graphics
