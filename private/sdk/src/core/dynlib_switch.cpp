/**
 * @file        core/dynlib_switch.cpp
 * @brief       Switch (Horizon OS) DynamicLibrary stub implementation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/platform.h>
#include <rex/platform/dynlib.h>

namespace rex::platform {

DynamicLibrary::~DynamicLibrary() {
  Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& /*path*/, SymbolResolution /*mode*/) {
  return false;
}

void DynamicLibrary::Close() {
  handle_ = nullptr;
}

void* DynamicLibrary::GetRawSymbol(const char* /*name*/) const {
  return nullptr;
}

}  // namespace rex::platform
