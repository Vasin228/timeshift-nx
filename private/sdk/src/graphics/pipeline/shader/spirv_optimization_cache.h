// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <fmt/format.h>
#include <xxhash.h>
#include <rex/filesystem.h>
#include <spirv-tools/optimizer.hpp>

namespace rex::graphics::optimization_cache {
// Input contains all generated code, feature and modification decisions.
// Version policy when changing the optimizer environment, passes or options.
// Policy 1 used the complete performance pipeline; policy 2 uses a bounded
// single sequence suitable for first-use compilation on the Switch CPU.
inline constexpr uint64_t kPolicy = 2;
inline void RegisterInteractivePasses(spvtools::Optimizer& optimizer) {
  optimizer.RegisterPass(spvtools::CreateWrapOpKillPass());
  optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
  optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
  optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
  optimizer.RegisterPass(spvtools::CreateEliminateDeadFunctionsPass());
  optimizer.RegisterPass(spvtools::CreatePrivateToLocalPass());
  optimizer.RegisterPass(spvtools::CreateScalarReplacementPass(100));
  optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
  optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
  optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
  optimizer.RegisterPass(spvtools::CreateSSARewritePass());
  optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
  optimizer.RegisterPass(spvtools::CreateCCPPass());
  optimizer.RegisterPass(spvtools::CreateSimplificationPass());
  optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
  optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
  optimizer.RegisterPass(spvtools::CreateBlockMergePass());
  optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
  optimizer.RegisterPass(spvtools::CreateCombineAccessChainsPass());
  optimizer.RegisterPass(spvtools::CreateSimplificationPass());
  optimizer.RegisterPass(spvtools::CreateVectorDCEPass());
  optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
}
inline constexpr uint64_t kMagic = 0x3148434143565053ull;
inline constexpr uint64_t kMaxBytes = 16ull << 20;
struct Key { uint64_t low, high; };
inline Key MakeKey(const std::vector<uint32_t>& input, const char* optimizer_version, uint64_t policy = kPolicy) {
  uint64_t seed = XXH3_64bits_withSeed(optimizer_version,
      std::char_traits<char>::length(optimizer_version), policy);
  auto h = XXH3_128bits_withSeed(input.data(), input.size() * sizeof(uint32_t), seed);
  return {h.low64, h.high64};
}
inline std::filesystem::path Path(const std::filesystem::path& directory, Key key) {
  return directory / fmt::format("{:016X}{:016X}.bin", key.high, key.low);
}
inline bool Load(const std::filesystem::path& directory, Key key,
                 std::vector<uint32_t>& output) {
  if (directory.empty()) return false;
  FILE* f = rex::filesystem::OpenFile(Path(directory, key), "rb");
  if (!f) return false;
  uint64_t h[5] = {};
  bool ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == kMagic &&
      h[1] == key.low && h[2] == key.high && h[3] >= 20 &&
      h[3] <= kMaxBytes && !(h[3] % 4);
  std::vector<uint32_t> data;
  if (ok) {
    data.resize(size_t(h[3] / 4));
    ok = fread(data.data(), size_t(h[3]), 1, f) == 1 && fgetc(f) == EOF &&
        data[0] == 0x07230203 && XXH3_64bits(data.data(), size_t(h[3])) == h[4];
  }
  fclose(f);
  if (ok) output = std::move(data);
  return ok;
}
inline bool Store(const std::filesystem::path& directory, Key key,
                  const std::vector<uint32_t>& data) {
  const size_t bytes = data.size() * sizeof(uint32_t);
  if (directory.empty() || bytes < 20 || bytes > kMaxBytes || data[0] != 0x07230203)
    return false;
  auto path = Path(directory, key);
  auto temporary = path; temporary += ".tmp";
  FILE* f = rex::filesystem::OpenFile(temporary, "wb");
  if (!f) return false;
  const uint64_t h[] = {kMagic, key.low, key.high, uint64_t(bytes),
      XXH3_64bits(data.data(), bytes)};
  bool ok = fwrite(h, sizeof(h), 1, f) == 1 && fwrite(data.data(), bytes, 1, f) == 1;
  if (fclose(f) != 0) ok = false;
  if (ok) {
    std::error_code error;
    // Horizon rename cannot replace a destination. A failed replacement only
    // loses cache data; partial files are never accepted by Load.
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    ok = !error;
  }
  return ok;
}
}  // namespace rex::graphics::optimization_cache
