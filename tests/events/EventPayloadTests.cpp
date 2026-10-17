// EventPayload: named fields of a closed set of types, read back with a
// fallback.

#include "TestSupport.h"

#include <arcbit/events/EventPayload.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Arcbit;

TEST_CASE("Literals land in the right kind of field", "[events]")
{
    const Entity       player{ 3, 1 };
    const EventPayload p{ { "amount", 5 }, { "scale", 1.5 }, { "label", "gold" }, { "ok", true },
                          { "by", player }, { "at", Vec2{ 2, 4 } } };

    CHECK(std::holds_alternative<i64>(*p.Find("amount")));
    CHECK(std::holds_alternative<f64>(*p.Find("scale")));
    CHECK(std::holds_alternative<std::string>(*p.Find("label"))); // not bool, despite const char*
    CHECK(p.Get<bool>("ok", false));
    CHECK(p.Get<Entity>("by", Entity::Invalid()) == player);
    CHECK(p.Get<Vec2>("at", Vec2{}) == Vec2{ 2, 4 });
    CHECK(p.Get<std::string>("label", "") == "gold");
}

TEST_CASE("Numbers convert between integer and decimal on read", "[events]")
{
    const EventPayload p{ { "amount", 5 }, { "scale", 1.5 } };
    CHECK(p.Get<f32>("amount", 0.0f) == 5.0f);
    CHECK(p.Get<i32>("scale", 0) == 1);
    CHECK(p.Get<u32>("amount", 0u) == 5u);
}

TEST_CASE("A missing field or one of another kind gives the fallback", "[events]")
{
    const EventPayload p{ { "label", "gold" }, { "amount", 5 } };
    CHECK(p.Get<i64>("missing", -1) == -1);
    CHECK(p.Get<i64>("label", -1) == -1);           // text isn't a number
    CHECK(p.Get<bool>("amount", false) == false);   // a number isn't a bool
    CHECK(p.Get<std::string>("amount", "x") == "x");
    CHECK_FALSE(p.Has("missing"));
}

TEST_CASE("Set replaces a field with the same name", "[events]")
{
    EventPayload p{ { "amount", 5 } };
    p.Set("amount", 7);
    p.Set("extra", true);
    CHECK(p.Fields().size() == 2);
    CHECK(p.Get<i64>("amount", 0) == 7);
}

TEST_CASE("A payload prints as name=value pairs for logs", "[events]")
{
    const EventPayload p{ { "amount", 5 }, { "label", "gold" }, { "by", Entity{ 3, 1 } } };
    CHECK(p.ToString() == R"(amount=5 label="gold" by=entity(3:1))");
    CHECK(EventPayload{}.ToString().empty());
}
