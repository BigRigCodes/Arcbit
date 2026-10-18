#pragma once

#include <arcbit/core/Types.h>

#include <string>
#include <string_view>

// UTF-8 helpers. Engine strings are UTF-8 throughout; anything that lays out
// or edits text walks it by codepoint with these, never by byte. Offsets stay
// byte offsets, but always land on the start of a codepoint.
namespace Arcbit::Utf8
{
// Stands in for a byte sequence that isn't valid UTF-8.
inline constexpr u32 Replacement = 0xFFFD;

// The codepoint starting at byte pos; pos moves past it. Invalid or cut-off
// sequences decode as Replacement and move on by one byte.
[[nodiscard]] u32 Decode(std::string_view text, usize& pos);

// The byte offset of the next / previous codepoint start (clamped to the text).
[[nodiscard]] usize Next(std::string_view text, usize pos);
[[nodiscard]] usize Prev(std::string_view text, usize pos);

// The number of codepoints in text.
[[nodiscard]] usize Length(std::string_view text);

// Append codepoint cp to out as UTF-8 (Replacement if cp isn't a valid codepoint).
void Append(std::string& out, u32 cp);

// for (const u32 cp : Utf8::Codepoints(text))
class Codepoints
{
public:
    explicit Codepoints(const std::string_view text) : _text(text) {}

    class Iterator
    {
    public:
        Iterator(const std::string_view text, const usize pos) : _text(text), _pos(pos) {}
        [[nodiscard]] u32 operator*() const
        {
            usize p = _pos;
            return Decode(_text, p);
        }
        Iterator& operator++()
        {
            _pos = Next(_text, _pos);
            return *this;
        }
        [[nodiscard]] bool operator!=(const Iterator& other) const { return _pos != other._pos; }

    private:
        std::string_view _text;
        usize            _pos;
    };

    [[nodiscard]] Iterator begin() const { return { _text, 0 }; }
    [[nodiscard]] Iterator end() const { return { _text, _text.size() }; }

private:
    std::string_view _text;
};
} // namespace Arcbit::Utf8
