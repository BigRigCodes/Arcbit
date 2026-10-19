// The pseudo-locale: English made visibly foreign and longer, to find text
// that isn't localized and layouts that can't take a longer translation.

#include <arcbit/core/Loc.h>
#include <arcbit/core/Utf8.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace Arcbit
{
namespace
{
// Letters and their accented stand-ins (escaped: the source stays ASCII).
constexpr std::pair<char, const char*> Accents[] = {
        { 'A', "\xC3\x85" }, { 'C', "\xC3\x87" }, { 'D', "\xC4\x8E" }, { 'E', "\xC3\x89" }, { 'G', "\xC4\x9E" },
        { 'I', "\xC3\x8E" }, { 'L', "\xC4\xBD" }, { 'N', "\xC3\x91" }, { 'O', "\xC3\x98" }, { 'R', "\xC5\x98" },
        { 'S', "\xC5\xA0" }, { 'T', "\xC5\xA4" }, { 'U', "\xC3\x9C" }, { 'Y', "\xC3\x9D" }, { 'Z', "\xC5\xBD" },
        { 'a', "\xC3\xA1" }, { 'c', "\xC3\xA7" }, { 'd', "\xC4\x8F" }, { 'e', "\xC3\xA9" }, { 'g', "\xC4\x9F" },
        { 'i', "\xC3\xAD" }, { 'l', "\xC4\xBE" }, { 'n', "\xC3\xB1" }, { 'o', "\xC3\xB3" }, { 'r', "\xC5\x99" },
        { 's', "\xC5\xA1" }, { 't', "\xC5\xA5" }, { 'u', "\xC3\xBA" }, { 'y', "\xC3\xBD" }, { 'z', "\xC5\xBE" },
};

constexpr f32 Growth = 0.35f; // translations often run this much longer than English
} // namespace

// "[Pause ~~]" -> "[Páúšé ~~]": letters accented outside {placeholders} and <tags>,
// padded about 35%, in brackets so cut-off text shows.
std::string Loc::Pseudo(const std::string_view english)
{
    std::string out = "[";
    i32         depth = 0; // inside a {placeholder} or a <tag>
    for (usize i = 0; i < english.size(); ++i) {
        const char c = english[i];
        const bool tag = c == '<' && i + 1 < english.size() && (std::isalpha(static_cast<unsigned char>(english[i + 1])) || english[i + 1] == '/');
        if (c == '{' || tag) ++depth;
        if (c == '}' || (c == '>' && depth > 0)) depth = std::max(0, depth - 1);
        const auto it = std::ranges::find(Accents, c, &std::pair<char, const char*>::first);
        if (depth == 0 && it != std::end(Accents)) out += it->second;
        else out += c;
    }
    const usize pad = static_cast<usize>(std::ceil(static_cast<f32>(Utf8::Length(english)) * Growth));
    if (pad > 0) out += " " + std::string(pad, '~');
    return out + "]";
}
} // namespace Arcbit
