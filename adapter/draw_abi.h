#pragma once
#include <cstdint>

namespace timeshift::renderer::abi {
// Caller-SP offsets, including ABI padding around the floating-point slot.
// Independently decoded from the owned image by tools/check_draw_layout.py.
inline constexpr uint32_t inline_stack_argument = 84;
inline constexpr uint32_t resolve_stencil = 92;
inline constexpr uint32_t resolve_parameters = 100;
}
