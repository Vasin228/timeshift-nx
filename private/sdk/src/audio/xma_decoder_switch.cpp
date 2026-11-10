/**
 * @file        audio/xma_decoder_switch.cpp
 * @brief       Switch (Horizon OS) real implementation of XmaDecoder and XmaContext using devkitPro libavcodec
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

// If compiled directly as part of a target that specifies xma_decoder_switch.cpp,
// wire in the real XmaContext and XmaDecoder implementations.
#if !defined(XMA_SEPARATE_TRANSLATION_UNITS)
#include "xma_context.cpp"
#include "xma_decoder.cpp"
#endif
