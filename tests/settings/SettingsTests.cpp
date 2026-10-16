#include "TestSupport.h"

#include <arcbit/input/InputManager.h>
#include <arcbit/settings/Settings.h>

#include <catch2/catch_test_macros.hpp>

using namespace Arcbit;

namespace
{
// Settings is a global store. Each test gets its own file and restores the
// well-known sections afterwards so tests stay independent.
class SettingsScope
{
public:
    SettingsScope() : _graphics(Settings::Graphics), _audio(Settings::Audio) {}
    ~SettingsScope()
    {
        Settings::Graphics = _graphics;
        Settings::Audio    = _audio;
        Settings::Init(_dir.File("unused.json").string()); // clear the custom store; the file never exists
    }
    SettingsScope(const SettingsScope&)            = delete;
    SettingsScope& operator=(const SettingsScope&) = delete;

    [[nodiscard]] std::string Path() const { return _dir.File("settings.json").string(); }
    [[nodiscard]] const ArcbitTests::TempDir& Dir() const { return _dir; }

private:
    ArcbitTests::TempDir _dir;
    GraphicsSettings     _graphics;
    AudioSettings        _audio;
};

constexpr ActionID Jump = MakeAction("Jump");
constexpr ActionID Fire = MakeAction("Fire");
} // namespace

TEST_CASE("A missing settings file leaves defaults in place", "[settings]")
{
    SettingsScope scope;
    Settings::Graphics = GraphicsSettings{};
    Settings::Init(scope.Path());
    CHECK_FALSE(Settings::IsDirty());
    CHECK(Settings::Graphics.ResolutionWidth == GraphicsSettings{}.ResolutionWidth);
    CHECK(Settings::Get<i32>("anything", 7) == 7);
}

TEST_CASE("Well-known sections round-trip through the file", "[settings]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    Settings::Graphics.VSync           = false;
    Settings::Graphics.ResolutionWidth = 2560;
    Settings::Graphics.FpsLimit        = 144;
    Settings::Graphics.ShowDebugInfo   = true;
    Settings::Audio.MusicVolume        = 0.25f;
    Settings::MarkDirty();
    Settings::Flush();
    CHECK_FALSE(Settings::IsDirty());

    Settings::Graphics = GraphicsSettings{};
    Settings::Audio    = AudioSettings{};
    Settings::Init(scope.Path());
    CHECK_FALSE(Settings::Graphics.VSync);
    CHECK(Settings::Graphics.ResolutionWidth == 2560);
    CHECK(Settings::Graphics.FpsLimit == 144);
    CHECK(Settings::Graphics.ShowDebugInfo);
    CHECK(Settings::Audio.MusicVolume == 0.25f);
}

TEST_CASE("Custom values round-trip with their types", "[settings]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    Settings::Set<bool>("cheats", true);
    Settings::Set<i32>("offset", -12);
    Settings::Set<u32>("seed", 4000000000u);
    Settings::Set<u32>("level", 5u);
    Settings::Set<f32>("gamma", 2.2f);
    Settings::Set<std::string>("name", "Arc");
    CHECK(Settings::IsDirty());
    Settings::Flush();

    Settings::Init(scope.Path());
    CHECK(Settings::Get<bool>("cheats"));
    CHECK(Settings::Get<i32>("offset") == -12);
    CHECK(Settings::Get<u32>("seed") == 4000000000u);
    // Regression: small u32 values came back from the file as i32 and Get<u32>
    // returned the default.
    CHECK(Settings::Get<u32>("level") == 5u);
    CHECK(Settings::Get<f32>("gamma") == 2.2f);
    CHECK(Settings::Get<std::string>("name") == "Arc");
}

TEST_CASE("Get converts between numeric types but not to or from bool / string", "[settings]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    Settings::Set<i32>("count", 3);
    Settings::Set<i32>("negative", -1);
    Settings::Set<std::string>("text", "5");
    Settings::Set<bool>("flag", true);

    CHECK(Settings::Get<u32>("count") == 3u);
    CHECK(Settings::Get<f32>("count") == 3.0f);
    CHECK(Settings::Get<u32>("negative", 99u) == 99u); // can't represent -1
    Settings::Set<u32>("huge", 4000000000u);
    CHECK(Settings::Get<i32>("huge", -7) == -7);        // doesn't fit in i32: no wraparound
    CHECK(Settings::Get<i32>("text", -7) == -7);
    CHECK(Settings::Get<i32>("flag", -7) == -7);
    CHECK(Settings::Get<bool>("count", false) == false);
}

TEST_CASE("A malformed settings file falls back to defaults", "[settings]")
{
    SettingsScope scope;
    Settings::Graphics = GraphicsSettings{};
    const std::string path = scope.Dir().Write("settings.json", "{ \"graphics\": { \"vsync\": fal");
    Settings::Init(path);
    CHECK(Settings::Graphics.VSync == GraphicsSettings{}.VSync);
}

TEST_CASE("Input bindings round-trip; UI_ actions are not persisted", "[settings][input]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    {
        InputManager input;
        input.RegisterAction(Jump, "Jump");
        input.RegisterAction(MakeAction("UI_Confirm"), "UI_Confirm");
        input.BindKey(Jump, Key::Space);
        input.BindGamepadButton(Jump, GamepadButton::South);
        input.BindGamepadAxis(Jump, GamepadAxis::LeftY, AxisHalf::Full, 0.4f);
        input.BindGamepadAxis(Jump, GamepadAxis::LeftX, AxisHalf::Negative);
        input.BindKey(MakeAction("UI_Confirm"), Key::Enter);
        Settings::SaveInputBindings(input);
        Settings::Flush();
    }

    Settings::Init(scope.Path());
    InputManager input;
    input.RegisterAction(Jump, "Jump");
    input.RegisterAction(Fire, "Fire");
    input.RegisterAction(MakeAction("UI_Confirm"), "UI_Confirm");
    input.BindKey(Jump, Key::W);          // default, should be replaced
    input.BindKey(Fire, Key::F);          // not in the file — untouched
    input.BindKey(MakeAction("UI_Confirm"), Key::Space); // UI_ defaults always win
    REQUIRE(Settings::LoadInputBindings(input));

    const auto& jump = input.GetBindings(Jump);
    REQUIRE(jump.size() == 4);
    CHECK(jump[0].BindingType == Binding::Type::Key);
    CHECK(jump[0].BoundKey == Key::Space);
    CHECK(jump[1].BindingType == Binding::Type::GamepadButton);
    CHECK(jump[1].BoundButton == GamepadButton::South);
    CHECK(jump[2].BindingType == Binding::Type::GamepadAxis);
    CHECK(jump[2].BoundAxis == GamepadAxis::LeftY);
    CHECK(jump[2].Deadzone == 0.4f);
    CHECK(jump[2].Half == AxisHalf::Full);
    CHECK(jump[3].BoundAxis == GamepadAxis::LeftX);
    CHECK(jump[3].Half == AxisHalf::Negative);

    REQUIRE(input.GetBindings(Fire).size() == 1);
    CHECK(input.GetBindings(Fire)[0].BoundKey == Key::F);
    REQUIRE(input.GetBindings(MakeAction("UI_Confirm")).size() == 1);
    CHECK(input.GetBindings(MakeAction("UI_Confirm"))[0].BoundKey == Key::Space);
}

TEST_CASE("Loading bindings without a saved section reports nothing loaded", "[settings][input]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    InputManager input;
    input.RegisterAction(Jump, "Jump");
    input.BindKey(Jump, Key::W);
    CHECK_FALSE(Settings::LoadInputBindings(input));
    CHECK(input.GetBindings(Jump).size() == 1);
}
