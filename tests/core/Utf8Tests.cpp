#include <arcbit/core/Utf8.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace Arcbit;

namespace
{
// "e" with an acute accent (2 bytes), the euro sign (3), a Japanese "a" (3)
// and an emoji (4). Escaped: the source stays ASCII.
constexpr std::string_view EAcute = "\xC3\xA9";
constexpr std::string_view Euro   = "\xE2\x82\xAC";
constexpr std::string_view JaA    = "\xE3\x81\x82";
constexpr std::string_view Emoji  = "\xF0\x9F\x98\x80";

std::vector<u32> All(const std::string_view text)
{
    std::vector<u32> out;
    for (const u32 cp : Utf8::Codepoints(text)) out.push_back(cp);
    return out;
}
} // namespace

TEST_CASE("Utf8 decodes one to four byte codepoints", "[core][utf8]")
{
    const std::string text = std::string("a") + std::string(EAcute) + std::string(Euro) + std::string(JaA) + std::string(Emoji);
    CHECK(All(text) == std::vector<u32>{ 'a', 0xE9, 0x20AC, 0x3042, 0x1F600 });
    CHECK(Utf8::Length(text) == 5);
    CHECK(Utf8::Length("") == 0);
}

TEST_CASE("Utf8 replaces invalid bytes one at a time", "[core][utf8]")
{
    CHECK(All("\xFF" "a") == std::vector<u32>{ Utf8::Replacement, 'a' });  // not a lead byte
    CHECK(All("\xC3") == std::vector<u32>{ Utf8::Replacement });           // cut off
    CHECK(All("\xC3" "a") == std::vector<u32>{ Utf8::Replacement, 'a' });  // continuation missing
    CHECK(All("\xC0\xAF") == std::vector<u32>{ Utf8::Replacement, Utf8::Replacement }); // overlong "/"
    CHECK(All("\xED\xA0\x80") == std::vector<u32>{ Utf8::Replacement, Utf8::Replacement, Utf8::Replacement }); // surrogate
}

TEST_CASE("Utf8 Next and Prev step over whole codepoints", "[core][utf8]")
{
    const std::string text = std::string("a") + std::string(Euro) + "b";
    CHECK(Utf8::Next(text, 0) == 1);
    CHECK(Utf8::Next(text, 1) == 4);
    CHECK(Utf8::Next(text, 4) == 5);
    CHECK(Utf8::Next(text, 5) == 5);
    CHECK(Utf8::Prev(text, 5) == 4);
    CHECK(Utf8::Prev(text, 4) == 1);
    CHECK(Utf8::Prev(text, 1) == 0);
    CHECK(Utf8::Prev(text, 0) == 0);
    CHECK(Utf8::Prev("a\x82", 2) == 1); // a stray continuation byte is one step
}

TEST_CASE("Utf8 Append encodes what Decode reads", "[core][utf8]")
{
    std::string out;
    for (const u32 cp : { 0x41u, 0xE9u, 0x20ACu, 0x3042u, 0x1F600u }) Utf8::Append(out, cp);
    CHECK(out == std::string("A") + std::string(EAcute) + std::string(Euro) + std::string(JaA) + std::string(Emoji));

    std::string bad;
    Utf8::Append(bad, 0xD800);
    CHECK(All(bad) == std::vector<u32>{ Utf8::Replacement });
}
