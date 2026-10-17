#include <arcbit/ui/MainMenuScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Loc.h>

namespace Arcbit
{

void MainMenuScreen::OnEnter()
{
    _roots.clear();

    if (!LoadLayout(LayoutPath)) {
        LOG_WARN(UI, "MainMenuScreen: '{}' not found, using built-in layout", LayoutPath);
        BuildFallback();
        return;
    }

    if (auto* btn = FindWidget<Button>("new-game-btn"))
        btn->OnClick = OnNewGame;

    if (auto* btn = FindWidget<Button>("continue-btn")) {
        btn->Visible = ShowContinue;
        btn->OnClick = OnContinue;
    }

    if (auto* btn = FindWidget<Button>("load-btn")) {
        btn->Visible = ShowLoadGame;
        btn->OnClick = OnLoadGame;
    }

    if (auto* btn = FindWidget<Button>("controls-btn")) {
        btn->Visible = ShowControls;
        btn->OnClick = OnControls;
    }

    if (auto* btn = FindWidget<Button>("audio-btn")) {
        btn->Visible = ShowAudioSettings;
        btn->OnClick = OnAudioSettings;
    }

    if (auto* btn = FindWidget<Button>("graphics-btn")) {
        btn->Visible = ShowGraphicsSettings;
        btn->OnClick = OnGraphicsSettings;
    }

    if (auto* btn = FindWidget<Button>("credits-btn")) {
        btn->Visible = ShowCredits;
        btn->OnClick = OnCredits;
    }

    if (auto* btn = FindWidget<Button>("quit-btn"))
        btn->OnClick = OnQuit;

    StackButtons(FindWidget<UIWidget>("bg"),
                 { "new-game-btn", "continue-btn", "load-btn", "controls-btn", "audio-btn", "graphics-btn",
                   "credits-btn", "quit-btn" },
                 GetMetaF32("btn_start_y", 70.0f), GetMetaF32("btn_gap", 52.0f), 26.0f);
}

void MainMenuScreen::OnBackPressed() { if (OnQuit) OnQuit(); }

// ---------------------------------------------------------------------------
// BuildFallback — inline layout used when the .arcui file is unavailable.
// ---------------------------------------------------------------------------

void MainMenuScreen::BuildFallback()
{
    const i32 btnCount = 1
        + (ShowContinue ? 1 : 0)
        + (ShowLoadGame ? 1 : 0)
        + (ShowControls ? 1 : 0)
        + (ShowAudioSettings ? 1 : 0)
        + (ShowGraphicsSettings ? 1 : 0)
        + (ShowCredits ? 1 : 0)
        + 1; // quit

    const f32 panelW = 360.0f;
    const f32 panelH = 80.0f + static_cast<f32>(btnCount) * 52.0f + 20.0f;

    auto* bg    = Add<Panel>();
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = Loc::Get("ui.mainmenu.title");
    title->Align  = TextAlign::Center;
    title->Size   = {panelW, 32.0f};
    title->Anchor = {0.5f, 0.0f};
    title->Pivot  = {0.5f, 0.0f};
    title->Offset = {0.0f, 16.0f};
    title->ZOrder = 2;

    auto* sep   = bg->AddChild<Panel>();
    sep->Size   = {panelW - 40.0f, 1.0f};
    sep->Anchor = {0.5f, 0.0f};
    sep->Pivot  = {0.5f, 0.0f};
    sep->Offset = {0.0f, 58.0f};
    sep->ZOrder = 2;

    const f32 btnW = 260.0f;
    const f32 btnH = 44.0f;
    const f32 gap  = 52.0f;

    auto MakeButton = [&](const char* labelKey, f32 yOffset, std::function<void()> cb,
                          Color textColor = {0, 0, 0, 0}) {
        auto* btn      = bg->AddChild<Button>();
        btn->Text      = Loc::Get(labelKey);
        btn->Size      = {btnW, btnH};
        btn->Anchor    = {0.5f, 0.0f};
        btn->Pivot     = {0.5f, 0.0f};
        btn->Offset    = {0.0f, yOffset};
        btn->Focusable = true;
        btn->ZOrder    = 2;
        if (textColor.A > 0.0f) btn->SkinOverride.TextLabel = textColor;
        btn->OnClick = std::move(cb);
    };

    f32 y = 70.0f;
    MakeButton("ui.mainmenu.new_game", y, OnNewGame);
    y += gap;
    if (ShowContinue) {
        MakeButton("ui.mainmenu.continue", y, OnContinue);
        y += gap;
    }
    if (ShowLoadGame) {
        MakeButton("ui.mainmenu.load", y, OnLoadGame);
        y += gap;
    }
    if (ShowControls) {
        MakeButton("ui.pause.controls", y, OnControls);
        y += gap;
    }
    if (ShowAudioSettings) {
        MakeButton("ui.pause.audio", y, OnAudioSettings);
        y += gap;
    }
    if (ShowGraphicsSettings) {
        MakeButton("ui.pause.graphics", y, OnGraphicsSettings);
        y += gap;
    }
    if (ShowCredits) {
        MakeButton("ui.mainmenu.credits", y, OnCredits);
        y += gap;
    }
    MakeButton("ui.mainmenu.quit", y, OnQuit, {0.88f, 0.30f, 0.30f, 1.0f});
}

} // namespace Arcbit
