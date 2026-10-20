#pragma once

// Where a line of text may break, shared by WrapText and the rich text
// layout: at spaces, and between Chinese / Japanese characters - never
// before closing punctuation.

#include <arcbit/core/Types.h>

#include <algorithm>
#include <iterator>

namespace Arcbit
{
// Scripts written without spaces between words: CJK punctuation, kana and
// ideographs, and full-width forms. (Korean uses spaces, so it isn't here.)
inline bool IsCjk(const u32 cp)
{
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Closing punctuation and small kana stay on the line they end (kinsoku).
inline bool NoBreakBefore(const u32 cp)
{
    static constexpr u32 Closers[] = {
        0x3001, 0x3002, 0x30FB, 0x30FC, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, // punctuation, brackets
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, // small hiragana
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, // small katakana
        0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F,                         // full-width ! ) , . : ; ?
    };
    return std::ranges::find(Closers, cp) != std::end(Closers);
}
} // namespace Arcbit
