// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <rex/graphics/pipeline/shader/shader.h>

namespace thps::native {
struct VertexFetchBound {
  bool seen = false;
  bool safe = true;
  uint32_t stride_words = 0;
  uint32_t end_words = 0;
  uint32_t rounded_index_extra = 0;
  uint64_t Bytes(uint32_t max_index) const {
    return ((uint64_t(max_index) + rounded_index_extra) * stride_words + end_words) * 4;
  }
};

// Prove which fetches use the incoming vertex ID. Unknown control flow or a
// modified/indirect fetch index falls back to the descriptor's original range.
inline std::array<VertexFetchBound, 96> AnalyzeVertexFetchBounds(
    std::span<const uint32_t> words, uint32_t cf_pairs) {
  namespace g = rex::graphics;
  namespace u = g::ucode;
  std::array<VertexFetchBound, 96> bounds{};
  const auto reject = []() { return std::array<VertexFetchBound, 96>{}; };
  if (cf_pairs > words.size() / 3) return reject();
  bool index_modified = false;
  bool previous_safe = false;
  bool have_previous_full = false;
  u::VertexFetchInstruction previous_full{};
  auto writes_index = [&](const g::InstructionResult& result) {
    if (result.storage_target == g::InstructionStorageTarget::kRegister &&
        (result.GetUsedWriteMask() & 1) &&
        (result.storage_addressing_mode != g::InstructionStorageAddressingMode::kAbsolute ||
         result.storage_index == 0)) index_modified = true;
  };
  for (uint32_t pair = 0; pair < cf_pairs; ++pair) {
    u::ControlFlowInstruction cf[2];
    u::UnpackControlFlowInstructions(words.data() + pair * 3, cf);
    for (uint32_t side = 0; side < 2; ++side) {
      g::ParsedExecInstruction exec{};
      switch (cf[side].opcode()) {
        case u::ControlFlowOpcode::kNop:
        case u::ControlFlowOpcode::kAlloc:
        case u::ControlFlowOpcode::kMarkVsFetchDone:
          continue;
        case u::ControlFlowOpcode::kExec:
        case u::ControlFlowOpcode::kExecEnd:
          g::ParseControlFlowExec(cf[side].exec, pair * 2 + side, exec); break;
        case u::ControlFlowOpcode::kCondExec:
        case u::ControlFlowOpcode::kCondExecEnd:
        case u::ControlFlowOpcode::kCondExecPredClean:
        case u::ControlFlowOpcode::kCondExecPredCleanEnd:
          g::ParseControlFlowCondExec(cf[side].cond_exec, pair * 2 + side, exec); break;
        case u::ControlFlowOpcode::kCondExecPred:
        case u::ControlFlowOpcode::kCondExecPredEnd:
          g::ParseControlFlowCondExecPred(cf[side].cond_exec_pred, pair * 2 + side, exec); break;
        default: return reject();
      }
      if (uint64_t(exec.instruction_address) + exec.instruction_count > words.size() / 3)
        return reject();
      uint32_t sequence = exec.sequence;
      for (uint32_t i = 0; i < exec.instruction_count; ++i, sequence >>= 2) {
        const uint32_t* instruction = words.data() + (exec.instruction_address + i) * 3;
        if (!(sequence & 1)) {
          g::ParsedAluInstruction alu{};
          g::ParseAluInstruction(*reinterpret_cast<const u::AluInstruction*>(instruction),
                                g::xenos::ShaderType::kVertex, alu);
          writes_index(alu.vector_and_constant_result);
          writes_index(alu.scalar_result);
          continue;
        }
        const auto& fetch = *reinterpret_cast<const u::FetchInstruction*>(instruction);
        if (fetch.opcode() != u::FetchOpcode::kVertexFetch) {
          g::ParsedTextureFetchInstruction texture{};
          g::ParseTextureFetchInstruction(fetch.texture_fetch(), texture);
          writes_index(texture.result);
          continue;
        }
        g::ParsedVertexFetchInstruction parsed{};
        bool full = g::ParseVertexFetchInstruction(fetch.vertex_fetch(), previous_full, parsed);
        if (full) {
          previous_full = fetch.vertex_fetch();
          have_previous_full = true;
          const auto& input = parsed.operands[0];
          previous_safe = !index_modified &&
              input.storage_source == g::InstructionStorageSource::kRegister &&
              input.storage_addressing_mode == g::InstructionStorageAddressingMode::kAbsolute &&
              input.storage_index == 0 && input.GetComponent(0) == g::SwizzleSource::kX &&
              !input.is_negated && !input.is_absolute_value;
        }
        uint32_t used = parsed.result.GetUsedResultComponents();
        if (used) {
          uint32_t slot = parsed.operands[1].storage_index;
          if (slot >= bounds.size()) return reject();
          auto& bound = bounds[slot];
          bound.seen = true;
          bound.safe &= have_previous_full && (!parsed.attributes.stride || previous_safe) &&
                        parsed.attributes.offset >= 0;
          bound.stride_words = std::max(bound.stride_words, parsed.attributes.stride);
          uint32_t needed = g::xenos::GetVertexFormatNeededWords(parsed.attributes.data_format, used);
          if (needed && parsed.attributes.offset >= 0)
            bound.end_words = std::max(bound.end_words,
                uint32_t(parsed.attributes.offset) + uint32_t(std::bit_width(needed)));
          // Above 2^23, rounding a float vertex ID by +0.5 can select its next integer.
          if (parsed.attributes.is_index_rounded) bound.rounded_index_extra = 1;
        }
        // A mini fetch keeps its full fetch's address even if that fetch overwrote r0.
        writes_index(parsed.result);
      }
    }
  }
  return bounds;
}

inline uint32_t RemapVertexIndex(uint32_t index, uint32_t base, uint32_t minimum,
                                 uint32_t maximum) {
  return std::clamp((index + base) & rex::graphics::xenos::kVertexIndexMask, minimum, maximum);
}
} // namespace thps::native
