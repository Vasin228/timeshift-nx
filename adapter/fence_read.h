#pragma once
#include <cstdint>
#include <optional>

namespace timeshift::renderer {
std::optional<uint32_t> ReadOwnedFenceCompletion(uint32_t device,uint32_t completion);

// Keep the retained generated load lazy: value_or(legacy()) would still race
// the render-thread writer even when an owned locked result is available.
template<class LegacyRead>
uint32_t ReadCompletionOr(uint32_t device,uint32_t completion,LegacyRead legacy){
  if(auto value=ReadOwnedFenceCompletion(device,completion))return *value;
  return legacy();
}
}
