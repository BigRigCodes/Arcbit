#include <arcbit/physics/ContactTracker.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Arcbit;

namespace
{
bool Has(const std::vector<EntityPair>& pairs, const EntityPair& p)
{
    return std::ranges::find(pairs, p) != pairs.end();
}
} // namespace

TEST_CASE("ContactTracker diffs pair sets into Enter / Stay / Exit", "[physics][contacts]")
{
    const Entity     a{ .Index = 1 };
    const Entity     b{ .Index = 2 };
    const Entity     c{ .Index = 3 };
    const EntityPair ab{ a, b };
    const EntityPair ac{ a, c };
    const EntityPair ba{ b, a };

    ContactTracker         tracker;
    ContactTracker::Events ev;

    tracker.Update({ ab, ab }, ev);
    CHECK(ev.Entered.size() == 1); // duplicate reports collapse
    CHECK(Has(ev.Entered, ab));

    tracker.Update({ ab, ac }, ev);
    CHECK(Has(ev.Stayed, ab));
    CHECK(Has(ev.Entered, ac));
    CHECK(ev.Exited.empty());

    tracker.Update({ ac }, ev);
    CHECK(Has(ev.Exited, ab));
    CHECK(Has(ev.Stayed, ac));

    tracker.Update({ ab, ba }, ev);
    CHECK(Has(ev.Entered, ab)); // re-entry after exit
    CHECK(Has(ev.Entered, ba)); // (A,B) and (B,A) are distinct pairs
    CHECK(Has(ev.Exited, ac));

    tracker.Update({}, ev);
    CHECK(ev.Exited.size() == 2);
    CHECK(tracker.Active().empty());
}
