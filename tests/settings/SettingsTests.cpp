#include "TestSupport.h"

#include <arcbit/app/UserFolder.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/settings/Settings.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

#include <limits>
#include <string>
#include <utility>
#include <vector>

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
    Settings::Audio.Volumes["music"]   = 0.25f;
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
    CHECK(Settings::Audio.Volume("music") == 0.25f);
    CHECK(Settings::Audio.Volume("voice") == 1.0f); // not set: full
}

TEST_CASE("An older settings file's three volumes read into the categories", "[settings]")
{
    SettingsScope scope;
    Settings::Audio = AudioSettings{};
    Settings::Init(scope.Dir().Write("settings.json", R"({ "audio": { "master_volume": 0.8, "music_volume": 0.5, "sfx_volume": 0.4 } })"));
    CHECK(Settings::Audio.Volume("master") == 0.8f);
    CHECK(Settings::Audio.Volume("music") == 0.5f);
    CHECK(Settings::Audio.Volume("sfx") == 0.4f);
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

TEST_CASE("Float values convert to integers only when they fit", "[settings]")
{
    // Regression: Get<integer> cast any stored f32 straight to the integer
    // type, which is undefined behavior for NaN, infinity, and out-of-range
    // values.
    SettingsScope scope;
    Settings::Init(scope.Path());
    Settings::Set<f32>("frac", 3.7f);
    Settings::Set<f32>("negative", -1.0f);
    Settings::Set<f32>("huge", 1e30f);
    Settings::Set<f32>("twoPow31", 2147483648.0f); // one past i32 max, exact in f32
    Settings::Set<f32>("nan", std::numeric_limits<f32>::quiet_NaN());
    Settings::Set<f32>("inf", std::numeric_limits<f32>::infinity());

    CHECK(Settings::Get<i32>("frac") == 3);
    CHECK(Settings::Get<i32>("negative") == -1);
    CHECK(Settings::Get<u32>("negative", 7u) == 7u);
    CHECK(Settings::Get<i32>("huge", -7) == -7);
    CHECK(Settings::Get<u32>("huge", 7u) == 7u);
    CHECK(Settings::Get<i32>("twoPow31", -7) == -7);
    CHECK(Settings::Get<u32>("twoPow31") == 2147483648u);
    CHECK(Settings::Get<i32>("nan", -7) == -7);
    CHECK(Settings::Get<u32>("inf", 7u) == 7u);
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

TEST_CASE("The change listener hears player-facing changes only", "[settings]")
{
    SettingsScope                                    scope;
    std::vector<std::pair<std::string, std::string>> heard;
    Settings::SetChangeListener([&](const std::string_view section, const std::string_view key) {
        heard.emplace_back(section, key);
    });

    Settings::MarkDirty("audio");
    Settings::MarkDirty(); // internal: dirty, but not announced
    Settings::Set<i32>("demo.level", 3);
    Settings::SetChangeListener({});
    Settings::MarkDirty("graphics"); // no listener any more

    CHECK(heard == std::vector<std::pair<std::string, std::string>>{ { "audio", "" }, { "game", "demo.level" } });
    CHECK(Settings::IsDirty());
}

TEST_CASE("Settings from next to the executable move to the user folder once", "[settings]")
{
    const ArcbitTests::TempDir  dir;
    const std::string           legacy  = dir.Write("settings.json", R"({ "audio": { "master": 0.5 } })");
    const std::filesystem::path current = dir.File("user") / "settings.json";

    MoveLegacyFile(legacy, current);
    REQUIRE(std::filesystem::exists(current));
    CHECK(std::filesystem::file_size(current) == std::filesystem::file_size(legacy));

    // Once there, the user folder's copy is the one that counts: never overwritten.
    dir.Write("settings.json", R"({ "audio": { "master": 1.0 } })");
    const auto before = std::filesystem::file_size(current);
    MoveLegacyFile(legacy, current);
    CHECK(std::filesystem::file_size(current) == before);
}

TEST_CASE("Floats are written as the number they show, volumes to four decimals", "[settings]")
{
    SettingsScope scope;
    Settings::Init(scope.Path());
    f32 drifted = 1.0f;
    for (int i = 0; i < 8; ++i) drifted -= 0.1f; // 0.19999993f, not 0.2f
    Settings::Audio.Volumes["music"] = drifted;
    Settings::Audio.Volumes["sfx"]   = 0.3f;
    Settings::Set<f32>("game.speed", 0.1f);
    Settings::MarkDirty();
    Settings::Flush();

    std::ifstream     in(scope.Path());
    std::stringstream text;
    text << in.rdbuf();
    CHECK(text.str().find("\"music\": 0.2,") != std::string::npos);
    CHECK(text.str().find("\"sfx\": 0.3") != std::string::npos);
    CHECK(text.str().find("\"game.speed\": 0.1") != std::string::npos);
    CHECK(text.str().find("0000") == std::string::npos); // no 0.30000001192092896

    Settings::Init(scope.Path());
    CHECK(Settings::Audio.Volume("sfx") == 0.3f);
    CHECK(Settings::Get<f32>("game.speed") == 0.1f);
}
