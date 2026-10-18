// CLDR cardinal plural rules (unicode.org/cldr, "Language Plural Rules") for
// the languages the engine knows; any other language counts like English.

#include <arcbit/core/Loc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace Arcbit
{
namespace
{
bool OneOf(const std::string_view code, const std::initializer_list<std::string_view> codes)
{
    return std::ranges::find(codes, code) != codes.end();
}

// 2-4, but not 12-14: Slavic "few".
bool SlavicFew(const u64 i) { return i % 10 >= 2 && i % 10 <= 4 && !(i % 100 >= 12 && i % 100 <= 14); }

std::string_view EastSlavic(const u64 i, const bool whole) // Russian, Ukrainian, Belarusian
{
    if (!whole) return "other";
    if (i % 10 == 1 && i % 100 != 11) return "one";
    return SlavicFew(i) ? "few" : "many";
}

std::string_view Polish(const u64 i, const bool whole)
{
    if (!whole) return "other";
    if (i == 1) return "one";
    return SlavicFew(i) ? "few" : "many";
}

std::string_view Czech(const u64 i, const bool whole) // and Slovak
{
    if (!whole) return "many";
    if (i == 1) return "one";
    return i >= 2 && i <= 4 ? "few" : "other";
}

std::string_view Arabic(const u64 i, const bool whole)
{
    if (!whole) return "other";
    if (i <= 2) return i == 0 ? "zero" : i == 1 ? "one" : "two";
    if (i % 100 >= 3 && i % 100 <= 10) return "few";
    return i % 100 >= 11 ? "many" : "other";
}
} // namespace

std::string_view Loc::PluralForm(const std::string_view language, const f64 n)
{
    std::string code(language);
    for (char& c : code) c = c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string_view base(std::string_view(code).substr(0, code.find('-')));

    const f64  a     = std::abs(n);
    const bool whole = a == std::floor(a);
    const u64  i     = static_cast<u64>(std::floor(a)); // the integer digits

    if (OneOf(base, { "ja", "zh", "ko", "th", "vi", "id", "ms", "lo", "km", "my" })) return "other";
    if (base == "fr" || (base == "pt" && code != "pt-pt")) return i <= 1 ? "one" : "other"; // 0 and 1.5 are singular too
    if (OneOf(base, { "ru", "uk", "be" })) return EastSlavic(i, whole);
    if (base == "pl") return Polish(i, whole);
    if (OneOf(base, { "cs", "sk" })) return Czech(i, whole);
    if (base == "ar") return Arabic(i, whole);
    return whole && i == 1 ? "one" : "other"; // English, German, Spanish, Italian, Dutch, the Nordics...
}
} // namespace Arcbit
