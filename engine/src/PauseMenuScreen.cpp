#include <arcbit/ui/PauseMenuScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

namespace Arcbit
{
void PauseMenuScreen::OnEnter()
{
    _roots.clear();

    // Load visual layout from file; fall back to built-in if the file is absent.
    if (!LoadLayout(LayoutPath)) {
        LOG_WARN(UI, "PauseMenuScreen: '{}' not found, using built-in layout", LayoutPath);
        BuildFallback();
        return;
    }

    // Wire callbacks — layout is data, behavior is code.
    if (auto* btn = FindWidget<Button>("resume-btn"))
        btn->OnClick = OnResume;

    if (auto* btn = FindWidget<Button>("save-btn")) {
        btn->Visible = ShowSaveGame;
        btn->OnClick = OnSaveGame;
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

    if (auto* btn = FindWidget<Button>("quit-btn"))
        btn->OnClick = OnQuit;

    StackButtons(FindWidget<UIWidget>("bg"),
                 { "resume-btn", "save-btn", "load-btn", "controls-btn", "audio-btn", "graphics-btn", "quit-btn" },
                 GetMetaF32("btn_start_y", 72.0f), GetMetaF32("btn_gap", 56.0f), 24.0f);
}

void PauseMenuScreen::OnBackPressed() { if (OnResume) OnResume(); }

// ---------------------------------------------------------------------------
// BuildFallback — inline layout used when the .arcui file is unavailable.
// ---------------------------------------------------------------------------

void PauseMenuScreen::BuildFallback()
{
    Add<Scrim>();

    const i32 btnCount = 2
            + (ShowSaveGame ? 1 : 0)
            + (ShowLoadGame ? 1 : 0)
            + (ShowControls ? 1 : 0)
            + (ShowAudioSettings ? 1 : 0)
            + (ShowGraphicsSettings ? 1 : 0);

    const f32 panelW = 360.0f;
    const f32 panelH = 80.0f + static_cast<f32>(btnCount) * 56.0f + 20.0f;

    UIWidget* bg;
    if (PanelTexture.IsValid()) {
        auto* ns         = Add<NineSlice>();
        ns->Texture      = PanelTexture;
        ns->Sampler      = PanelSampler;
        ns->UVBorderLeft = ns->UVBorderRight  = 0.125f;
        ns->UVBorderTop  = ns->UVBorderBottom = 0.125f;
        ns->PixelLeft    = ns->PixelRight     = 16.0f;
        ns->PixelTop     = ns->PixelBottom    = 16.0f;
        bg               = ns;
    }
    else { bg = Add<Panel>(); }
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = Loc::Get("ui.pause.title");
    title->Align  = TextAlign::Center;
    title->Size   = {panelW, 32.0f};
    title->Anchor = {0.5f, 0.0f};
    title->Pivot  = {0.5f, 0.0f};
    title->Offset = {0.0f, 20.0f};
    title->ZOrder = 2;

    auto* sep   = bg->AddChild<Panel>();
    sep->Size   = {panelW - 40.0f, 1.0f};
    sep->Anchor = {0.5f, 0.0f};
    sep->Pivot  = {0.5f, 0.0f};
    sep->Offset = {0.0f, 62.0f};
    sep->ZOrder = 2;

    const f32 btnW = 240.0f;
    const f32 btnH = 44.0f;
    const f32 gap  = 56.0f;

    auto MakeButton = [&](const char* label, f32 yOffset, std::function<void()> cb,
                          Color       textColor = {0, 0, 0, 0}) {
        auto* btn      = bg->AddChild<Button>();
        btn->Text      = label;
        btn->Size      = {btnW, btnH};
        btn->Anchor    = {0.5f, 0.0f};
        btn->Pivot     = {0.5f, 0.0f};
        btn->Offset    = {0.0f, yOffset};
        btn->Focusable = true;
        btn->ZOrder    = 2;
        if (textColor.A > 0.0f) btn->SkinOverride.TextLabel = textColor;
        btn->OnClick = std::move(cb);
    };

    f32 y = 72.0f;
    MakeButton("Resume", y, OnResume);
    y += gap;
    if (ShowSaveGame) {
        MakeButton("Save Game", y, OnSaveGame);
        y += gap;
    }
    if (ShowLoadGame) {
        MakeButton("Load Game", y, OnLoadGame);
        y += gap;
    }
    if (ShowControls) {
        MakeButton("Controls", y, OnControls);
        y += gap;
    }
    if (ShowAudioSettings) {
        MakeButton("Audio", y, OnAudioSettings);
        y += gap;
    }
    if (ShowGraphicsSettings) {
        MakeButton("Graphics", y, OnGraphicsSettings);
        y += gap;
    }
    MakeButton("Quit", y, OnQuit, {0.88f, 0.30f, 0.30f, 1.0f});
}
} // namespace Arcbit
