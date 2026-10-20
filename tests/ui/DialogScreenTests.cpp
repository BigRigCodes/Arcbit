// The dialog box without a renderer: lines move on with Confirm, choices
// take the arrows and Confirm, auto-advance waits while a menu is over it,
// and the dialog settings read back. Text speed is "instant" here - the
// typewriter only moves as the text is drawn.

#include "TestSupport.h"

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/save/SaveData.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/DialogScreen.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <SDL3/SDL_scancode.h>

using namespace Arcbit;

namespace
{
const char* Talk = R"({
    "speaker": "elder", "speakers": { "elder": { "name": "Elder" } },
    "nodes": {
        "a": [ "Hello.", { "choice": [ { "id": "x", "text": "First" }, { "id": "y", "text": "Second", "set": { "picked": 2 } } ] },
               { "say": "Done.", "speaker": null } ] } })";

struct Fixture
{
    InputManager  input;
    UIManager     ui;
    SaveData      data;
    DialogScreen* screen = nullptr;
    std::string   finished = "-";
    std::string   speed    = Settings::Get<std::string>(DialogSettings::TextSpeedKey, "");
    bool          autoOn   = DialogSettings::GetAutoAdvance();

    explicit Fixture(const bool autoAdvance = false)
    {
        Loc::Clear();
        DialogSettings::SetTextSpeed(DialogSettings::TextSpeed::Instant);
        DialogSettings::SetAutoAdvance(autoAdvance);
        input.RegisterAction(UIManager::ActionConfirm, "UI_Confirm");
        input.BindKey(UIManager::ActionConfirm, Key::Enter);
        input.RegisterAction(UIManager::ActionFocusNext, "UI_FocusNext");
        input.BindKey(UIManager::ActionFocusNext, Key::Down);

        std::vector<DialogIssue> issues;
        auto dialog = ParseDialog(nlohmann::ordered_json::parse(Talk), "talk", issues);
        REQUIRE(dialog);
        Loc::SetStrings(Loc::English, "test", dialog->GetStrings());

        auto owned             = std::make_unique<DialogScreen>();
        owned->LayoutPath      = ArcbitTests::SourcePath("engine/assets/ui/dialog.arcui").string();
        owned->TransitionSpeed = 0.0f;
        owned->OnFinished      = [this](const std::string& choice) { finished = choice; };
        owned->Begin(std::make_shared<const Dialog>(std::move(*dialog)), { .Data = &data });
        screen = owned.get();
        ui.Push(std::move(owned));
        Tick();
    }
    ~Fixture()
    {
        Settings::Set<std::string>(DialogSettings::TextSpeedKey, speed);
        DialogSettings::SetAutoAdvance(autoOn);
        Loc::Clear();
    }
    void Tick() { ui.Update(1.0f / 60.0f, { 1280.0f, 720.0f }, input); }
    void Press(const SDL_Scancode key)
    {
        input.InjectKeyEvent(key, true);
        input.ProcessEdges();
        Tick();
        input.InjectKeyEvent(key, false);
        input.ProcessEdges();
        Tick();
    }
    std::string Text() { return screen->FindWidget<Label>("text")->Text; }
};
} // namespace

TEST_CASE("The dialog box shows lines, the speaker, and moves on with Confirm", "[ui][dialog]")
{
    Fixture f;
    CHECK(f.Text() == "Hello.");
    CHECK(f.screen->FindWidget<Label>("name")->Text == "Elder");
    CHECK(f.screen->FindWidget<NineSlice>("name-plate")->Visible);
    CHECK_FALSE(f.screen->FindWidget<NineSlice>("choices")->Visible);

    f.Press(SDL_SCANCODE_RETURN);
    REQUIRE(f.screen->GetRunner().GetState() == DialogRunner::State::Choice);
    CHECK(f.screen->FindWidget<NineSlice>("choices")->Visible);
    CHECK(f.Text() == "Hello."); // the line stays up under the choices
}

TEST_CASE("Choices take the arrows and Confirm; the end is reported", "[ui][dialog]")
{
    Fixture f;
    f.Press(SDL_SCANCODE_RETURN); // to the choice: the press that showed it picks nothing
    REQUIRE(f.screen->GetRunner().GetState() == DialogRunner::State::Choice);
    f.Press(SDL_SCANCODE_DOWN);
    f.Press(SDL_SCANCODE_RETURN);
    CHECK(f.data.Get<i64>("picked", 0) == 2);
    CHECK(f.Text() == "Done.");
    CHECK_FALSE(f.screen->FindWidget<NineSlice>("name-plate")->Visible); // narration
    CHECK(f.finished == "-");

    f.Press(SDL_SCANCODE_RETURN);
    CHECK(f.finished == "y");
}

TEST_CASE("Auto-advance moves a shown line on, but not under a menu", "[ui][dialog]")
{
    Fixture f(true);
    auto    menu = std::make_unique<UIScreen>();
    menu->TransitionSpeed = 0.0f;
    UIScreen* raw = menu.get();
    f.ui.Push(std::move(menu)); // a menu over the dialog: the dialog waits
    for (int i = 0; i < 240; ++i) f.Tick();
    CHECK(f.screen->GetRunner().GetState() == DialogRunner::State::Line);

    f.ui.Close(raw);
    for (int i = 0; i < 240; ++i) f.Tick();
    CHECK(f.screen->GetRunner().GetState() == DialogRunner::State::Choice); // choices never auto-advance
}

TEST_CASE("Dialog settings read back, with sensible defaults", "[ui][dialog]")
{
    const std::string speed = Settings::Get<std::string>(DialogSettings::TextSpeedKey, "");
    Settings::Set<std::string>(DialogSettings::TextSpeedKey, "nonsense");
    CHECK(DialogSettings::GetTextSpeed() == DialogSettings::TextSpeed::Normal);
    DialogSettings::SetTextSpeed(DialogSettings::TextSpeed::Fast);
    CHECK(DialogSettings::GetTextSpeed() == DialogSettings::TextSpeed::Fast);
    CHECK(Settings::Get<std::string>(DialogSettings::TextSpeedKey, "") == "fast");
    CHECK(DialogSettings::CharsPerSecond(DialogSettings::TextSpeed::Slow) <
          DialogSettings::CharsPerSecond(DialogSettings::TextSpeed::Normal));
    CHECK(DialogSettings::CharsPerSecond(DialogSettings::TextSpeed::Instant) == 0.0f);
    Settings::Set<std::string>(DialogSettings::TextSpeedKey, speed);
}
