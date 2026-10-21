// Bindings across game updates (MergeBindings): the player's keys win, new
// actions take free keys or the ones an update freed, a forced reset goes
// through the same rules - and Settings records the defaults to tell.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/BindingUpdate.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/settings/Settings.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
Binding K(const Key key) { return Binding::FromKey(key); }
Binding Pad(const GamepadButton button) { return Binding::FromGamepadButton(button); }

using List = std::vector<Binding>;

// The release: Interact on E. The update: Interact's default moves to F
// and a new Melee takes E.
BindingMergeInput InteractMelee()
{
    return { .Defaults      = { { "Interact", { K(Key::F) } }, { "Melee", { K(Key::E) } } },
             .Saved         = { { "Interact", { K(Key::E) } } },
             .SavedDefaults = ActionBindingMap{ { "Interact", { K(Key::E) } } } };
}
} // namespace

TEST_CASE("An update's new action takes the key the update freed", "[input][bindings]")
{
    const BindingMergeResult r = MergeBindings(InteractMelee());
    CHECK(r.Bindings.at("Interact") == List{ K(Key::E) }); // what the player is used to
    CHECK(r.Bindings.at("Melee") == List{ K(Key::F) });
    REQUIRE(r.Changes.size() == 1);
    CHECK(r.Changes[0].Action == "Melee");
    CHECK(r.Changes[0].What == BindingChange::Kind::New);
    CHECK(r.Changes[0].IsNotable());
    CHECK(r.Changes[0].Got == List{ K(Key::F) });
    REQUIRE(r.Changes[0].Taken.size() == 1);
    CHECK(r.Changes[0].Taken[0] == std::pair{ K(Key::E), std::string("Interact") });
}

TEST_CASE("Kept actions keep their keys; a new action takes a free default", "[input][bindings]")
{
    const BindingMergeResult r = MergeBindings({
        .Defaults      = { { "Jump", { K(Key::Space), Pad(GamepadButton::South) } }, { "Dash", { K(Key::LeftShift) } } },
        .Saved         = { { "Jump", { K(Key::Q) } } }, // rebound, and the pad binding removed
        .SavedDefaults = ActionBindingMap{ { "Jump", { K(Key::W) } } } });
    CHECK(r.Bindings.at("Jump") == List{ K(Key::Q) }); // not the new default
    CHECK(r.Bindings.at("Dash") == List{ K(Key::LeftShift) });
    REQUIRE(r.Changes.size() == 1);
    CHECK(r.Changes[0].Taken.empty());
    CHECK_FALSE(r.Changes[0].IsNotable()); // on its own default: not worth a notice by default
}

TEST_CASE("A new action whose key is taken, with nothing freed, stays unbound", "[input][bindings]")
{
    SECTION("the holder's default didn't move")
    {
        const BindingMergeResult r = MergeBindings({ .Defaults = { { "Interact", { K(Key::E) } }, { "Melee", { K(Key::E) } } },
                                                     .Saved    = { { "Interact", { K(Key::E) } } },
                                                     .SavedDefaults = ActionBindingMap{ { "Interact", { K(Key::E) } } } });
        CHECK(r.Bindings.at("Melee").empty());
        CHECK(r.Changes[0].Got.empty());
    }
    SECTION("the freed key is taken too")
    {
        BindingMergeInput in = InteractMelee();
        in.Defaults.emplace_back("Jump", List{ K(Key::Space) });
        in.Saved["Jump"]     = { K(Key::F) }; // the player put Jump on F
        const BindingMergeResult r = MergeBindings(in);
        CHECK(r.Bindings.at("Melee").empty());
    }
}

TEST_CASE("A new action takes the default a rebound action left unused", "[input][bindings]")
{
    // The player moved Jump from Space to F; an update adds Melee on F.
    const BindingMergeResult r = MergeBindings({ .Defaults = { { "Jump", { K(Key::Space) } }, { "Melee", { K(Key::F) } } },
                                                 .Saved    = { { "Jump", { K(Key::F) } } },
                                                 .SavedDefaults = ActionBindingMap{ { "Jump", { K(Key::Space) } } } });
    CHECK(r.Bindings.at("Jump") == List{ K(Key::F) });
    CHECK(r.Bindings.at("Melee") == List{ K(Key::Space) });
}

TEST_CASE("Keys and gamepad buttons are placed separately", "[input][bindings]")
{
    BindingMergeInput in            = InteractMelee();
    in.Defaults[0].second           = { K(Key::F), Pad(GamepadButton::West) };
    in.Defaults[1].second           = { K(Key::E), Pad(GamepadButton::South) };
    in.Saved["Interact"]            = { K(Key::E), Pad(GamepadButton::South) };
    (*in.SavedDefaults)["Interact"] = { K(Key::E), Pad(GamepadButton::South) };
    const BindingMergeResult r      = MergeBindings(in);
    CHECK(r.Bindings.at("Melee") == List{ K(Key::F), Pad(GamepadButton::West) }); // each from its own device
}

TEST_CASE("An older settings file: bindings are the player's, resets wait", "[input][bindings]")
{
    BindingMergeInput in = InteractMelee();
    in.SavedDefaults     = std::nullopt; // no defaults recorded
    in.Resets            = { "Interact" };
    const BindingMergeResult r = MergeBindings(in);
    CHECK(r.Bindings.at("Interact") == List{ K(Key::E) });
    CHECK(r.Bindings.at("Melee") == List{ K(Key::F) }); // Interact isn't on its default: F counts as freed
}

TEST_CASE("A forced reset moves an action to its new default once", "[input][bindings]")
{
    BindingMergeInput in = InteractMelee();
    in.Defaults.pop_back(); // no Melee this time
    in.Resets = { "Interact" };
    BindingMergeResult r = MergeBindings(in);
    CHECK(r.Bindings.at("Interact") == List{ K(Key::F) });
    REQUIRE(r.Changes.size() == 1);
    CHECK(r.Changes[0].What == BindingChange::Kind::Reset);

    // The next launch: the file records F as the default. The player moved
    // it back to E since - that stays.
    in.Saved         = { { "Interact", { K(Key::E) } } };
    in.SavedDefaults = ActionBindingMap{ { "Interact", { K(Key::F) } } };
    r                = MergeBindings(in);
    CHECK(r.Bindings.at("Interact") == List{ K(Key::E) });
    CHECK(r.Changes.empty());

    SECTION("with its new key taken, it keeps its old one")
    {
        in.Defaults.emplace_back("Jump", List{ K(Key::F) }); // F is Jump's own default: nothing left over
        in.Saved         = { { "Interact", { K(Key::E) } }, { "Jump", { K(Key::F) } } };
        in.SavedDefaults = ActionBindingMap{ { "Interact", { K(Key::E) } }, { "Jump", { K(Key::F) } } };
        r                = MergeBindings(in);
        CHECK(r.Bindings.at("Interact") == List{ K(Key::E) });
        REQUIRE(r.Changes.size() == 1);
        CHECK(r.Changes[0].Taken[0].second == "Jump");
    }
}

TEST_CASE("Settings records the defaults and applies an update once", "[input][bindings][settings]")
{
    const ArcbitTests::TempDir dir;
    const std::string          path     = dir.File("settings.json").string();
    const ActionID             Interact = MakeAction("Interact"), Melee = MakeAction("Melee");
    const auto                 launch   = [&](const bool update) {
        Settings::Init(path);
        InputManager input;
        input.RegisterAction(Interact, "Interact");
        input.BindKey(Interact, update ? Key::F : Key::E);
        if (update) {
            input.RegisterAction(Melee, "Melee");
            input.BindKey(Melee, Key::E);
        }
        (void)Settings::LoadInputBindings(input);
        const auto changes = Settings::GetBindingChanges().size();
        Settings::SaveInputBindings(input);
        Settings::Shutdown();
        return std::pair{ input.GetBindings(update ? Melee : Interact), changes };
    };

    (void)launch(false); // the release: the file records E as Interact's default
    (void)launch(false);
    const auto [melee, told] = launch(true);
    CHECK(melee == List{ K(Key::F) });
    CHECK(told == 1);
    CHECK(launch(true).second == 0); // told once
}

TEST_CASE("The notice says where each new action landed", "[input][bindings]")
{
    Loc::Clear();
    REQUIRE(Loc::Load(ArcbitTests::SourcePath("engine/assets/locale/en.json").string()));
    InputManager input;
    input.RegisterAction(MakeAction("Interact"), "Interact");
    input.RegisterAction(MakeAction("Melee"), "Melee", "Sword");

    const BindingMergeResult r = MergeBindings(InteractMelee());
    CHECK(DescribeBindingChange(r.Changes[0], input) == "New: Sword is F (E is your Interact)");
    CHECK(DescribeBindingChange({ .Action = "Melee" }, input) == "Sword has no key yet - set it in Controls");
    CHECK(DescribeBindingChange({ .Action = "Interact", .What = BindingChange::Kind::Reset, .Got = { K(Key::F) } }, input) ==
          "Interact is now F");
    Loc::Clear();
}
