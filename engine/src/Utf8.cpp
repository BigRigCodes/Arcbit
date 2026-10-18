#include <arcbit/core/Utf8.h>

#include <algorithm>

namespace Arcbit::Utf8
{
namespace
{
bool IsContinuation(const u8 byte) { return (byte & 0xC0) == 0x80; }
} // namespace

u32 Decode(const std::string_view text, usize& pos)
{
    const u8 lead = static_cast<u8>(text[pos]);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }

    // The lead byte gives the length and the first bits; the smallest
    // codepoint each length may carry rejects overlong encodings.
    u32 length = 0, cp = 0, smallest = 0;
    if ((lead & 0xE0) == 0xC0) { length = 2; cp = lead & 0x1F; smallest = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { length = 3; cp = lead & 0x0F; smallest = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { length = 4; cp = lead & 0x07; smallest = 0x10000; }
    else {
        ++pos;
        return Replacement;
    }

    if (pos + length > text.size()) {
        ++pos;
        return Replacement;
    }
    for (u32 i = 1; i < length; ++i) {
        const u8 byte = static_cast<u8>(text[pos + i]);
        if (!IsContinuation(byte)) {
            ++pos;
            return Replacement;
        }
        cp = (cp << 6) | (byte & 0x3F);
    }
    if (cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;
        return Replacement;
    }
    pos += length;
    return cp;
}

usize Next(const std::string_view text, usize pos)
{
    if (pos >= text.size()) return text.size();
    (void)Decode(text, pos);
    return pos;
}

usize Prev(const std::string_view text, usize pos)
{
    if (pos == 0) return 0;
    pos = std::min(pos, text.size());
    // Step back over continuation bytes (at most three) to the lead byte.
    usize start = pos - 1;
    while (start > 0 && pos - start < 4 && IsContinuation(static_cast<u8>(text[start]))) --start;
    // Only a lead whose sequence ends exactly at pos counts; else one byte.
    usize end = start;
    (void)Decode(text, end);
    return end == pos ? start : pos - 1;
}

usize Length(const std::string_view text)
{
    usize count = 0;
    for (usize pos = 0; pos < text.size(); ++count) (void)Decode(text, pos);
    return count;
}

void Append(std::string& out, u32 cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = Replacement;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
} // namespace Arcbit::Utf8
