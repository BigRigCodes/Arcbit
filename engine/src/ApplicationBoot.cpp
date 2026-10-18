// Boot flow: the engine splash, the game's splashes, then the title menu or
// the first scene (BootConfig). Split out of Application.cpp, which owns the
// main loop.

#include <arcbit/app/Application.h>

#include <arcbit/core/Log.h>
#include <arcbit/core/Loc.h>
#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/ConfirmScreen.h>
#include <arcbit/ui/CreditsScreen.h>
#include <arcbit/ui/GraphicsSettingsScreen.h>
#include <arcbit/ui/InputRebindScreen.h>
#include <arcbit/ui/MainMenuScreen.h>
#include <arcbit/ui/SaveSlotScreen.h>
#include <arcbit/ui/SplashScreen.h>

namespace Arcbit
{
namespace
{
    // The engine's own splash. Engine-owned on purpose: a game appends its
    // splashes after this one (BootConfig::SplashImages) but has no way to
    // change, reorder or skip past it in code. Packed builds harden that
    // with the Phase 36 asset protection.
    constexpr const char* EngineSplashImage   = "assets/engine/ui/ArcbitA_withbackground.png";
    constexpr f32         EngineSplashSeconds = 5.0f;
    constexpr f32         SplashFadeSeconds   = 0.5f;
} // namespace

void Application::StartBoot()
{
    // The scene the player first sees loads under the splash (the main loop
    // runs a queued swap once the splash covers the screen) and fades in as
    // the splash ends.
    const std::string& first = _boot.ShowTitle && !_boot.TitleScene.empty() ? _boot.TitleScene : _boot.FirstScene;
    if (!first.empty()) _scenes.Switch(first);
    StartSaving();
    PushSplash();
}

void Application::PushSplash()
{
    auto                splash  = std::make_unique<SplashScreen>();
    const SamplerHandle sampler = GetStockSampler(StockSampler::Linear);
    const auto          add     = [&](const std::string& path, const f32 seconds) {
        // Acquire, not Load: the first scene loads under the splash and may
        // use the same image, so the splash only drops its own reference.
        const TextureHandle texture = _textures->Acquire(path);
        if (!texture.IsValid()) {
            LOG_WARN(Engine, "Splash image '{}' not found - skipped", path);
            return;
        }
        _splashTextures.push_back(texture);
        splash->Entries.push_back({ texture, sampler, seconds, SplashFadeSeconds, SplashFadeSeconds });
    };
    add(EngineSplashImage, EngineSplashSeconds);
    for (const std::string& path : _boot.SplashImages) add(path, _boot.SplashSeconds);
    if (NeedsSaveNotice()) splash->Entries.push_back(SaveNoticeCard()); // after the logos

    if (splash->Entries.empty()) {
        FinishSplash();
        return;
    }
    // On the System layer: removed by pointer (Pop() only closes menus).
    splash->OnComplete = [this, screen = splash.get()] {
        _ui.Remove(screen);
        FinishSplash();
    };
    _ui.Push(std::move(splash));
}

// The splash's references to its images are dropped (an image a scene also
// uses stays loaded). The render thread may still hold the last splash
// frame, so wait for it before releasing.
void Application::FinishSplash()
{
    LOG_INFO(Engine, "Splash finished");
    if (!_splashTextures.empty()) {
        _renderThread.WaitIdle();
        for (const TextureHandle texture : _splashTextures) _textures->Release(texture);
        _splashTextures.clear();
    }
    if (_boot.ShowTitle) ShowTitle();
}

// The title menu over the running scene. With a separate TitleScene
// backdrop, New Game switches to FirstScene; otherwise the first scene is
// already behind the menu and New Game just closes it. Either way the game
// starts over (ResetGame) - for the switch, once the backdrop has unloaded.
// Continue, shown once there's a save, loads the newest one; Load Game picks
// one.
void Application::ShowTitle()
{
    auto      title     = std::make_unique<MainMenuScreen>();
    UIScreen* screen    = title.get();
    title->ShowCredits  = !_boot.CreditsPath.empty();
    title->ShowContinue = !_saves.List().empty();
    title->ShowLoadGame = title->ShowContinue;
    title->OnNewGame    = [this, screen] {
        _ui.Close(screen);
        if (_boot.TitleScene.empty() || _boot.FirstScene.empty()) ResetGame();
        else _scenes.Restore({ { _boot.FirstScene, "" } }, [this] { ResetGame(); });
    };
    title->OnContinue = [this, screen] {
        const std::vector<SaveSlotInfo> saves = _saves.List();
        if (!saves.empty() && LoadFrom(saves.front().Slot)) _ui.Close(screen);
    };
    title->OnLoadGame         = [this] { ShowLoadScreen(); };
    title->OnControls         = [this] { ShowControlsScreen(); };
    title->OnAudioSettings    = [this] { ShowAudioScreen(); };
    title->OnGraphicsSettings = [this] { ShowGraphicsScreen(); };
    title->OnCredits          = [this] { ShowCredits(); };
    title->OnGameSettings     = [this] { ShowGameSettingsScreen(); };
    title->OnQuit             = [this] { RequestShutdown(); };
    _ui.OpenMenu(std::move(title));
}

void Application::ShowCredits()
{
    auto credits        = std::make_unique<CreditsScreen>();
    credits->Path       = _boot.CreditsPath;
    credits->ViewHeight = static_cast<f32>(GetWindowHeight());
    credits->OnClose    = [this, screen = credits.get()] { _ui.Close(screen); };
    _ui.Push(std::move(credits));
}

// The slot lists' thumbnails: loaded as a list opens; released once it
// closes, after the render thread is done drawing them.
void Application::WireThumbnails(SaveSlotScreen& screen)
{
    screen.ThumbnailSampler = GetStockSampler(StockSampler::Linear);
    screen.LoadThumbnail    = [this](const std::filesystem::path& path) { return _textures->Acquire(path.string()); };
    screen.ReleaseThumbnails = [this](const std::vector<TextureHandle>& textures) {
        _renderThread.WaitIdle();
        for (const TextureHandle texture : textures) _textures->Release(texture);
    };
}

void Application::ShowSaveScreen()
{
    auto save    = std::make_unique<SaveSlotScreen>();
    save->For    = SaveSlotScreen::Purpose::Save;
    save->Saves  = &_saves;
    WireThumbnails(*save);
    save->OnBack = [this, screen = save.get()] { _ui.Close(screen); };
    save->OnPick = [this, screen = save.get()](const SaveSlot slot, const bool hasSave) {
        const auto write = [this, screen, slot] {
            if (SaveTo(slot)) _ui.Close(screen);
        };
        if (hasSave) Confirm(Loc::Get("ui.saves.overwrite"), write);
        else write();
    };
    _ui.Push(std::move(save));
}

void Application::ShowLoadScreen()
{
    auto load    = std::make_unique<SaveSlotScreen>();
    load->For    = SaveSlotScreen::Purpose::Load;
    load->Saves  = &_saves;
    WireThumbnails(*load);
    load->OnBack = [this, screen = load.get()] { _ui.Close(screen); };
    load->OnPick = [this](const SaveSlot slot, bool) {
        if (LoadFrom(slot)) _ui.CloseMenus();
    };
    _ui.Push(std::move(load));
}

void Application::Confirm(std::string message, std::function<void()> onYes)
{
    auto confirm     = std::make_unique<ConfirmScreen>();
    confirm->Message = std::move(message);
    confirm->OnNo    = [this, screen = confirm.get()] { _ui.Close(screen); };
    confirm->OnYes   = [this, screen = confirm.get(), yes = std::move(onYes)] {
        _ui.Close(screen);
        if (yes) yes();
    };
    _ui.Push(std::move(confirm));
}

void Application::ShowControlsScreen()
{
    auto rebind    = std::make_unique<InputRebindScreen>();
    rebind->Input  = &_input;
    rebind->OnBack = [this, screen = rebind.get()] { _ui.Close(screen); };
    _ui.Push(std::move(rebind));
}

void Application::ShowAudioScreen()
{
    auto audio    = std::make_unique<AudioSettingsScreen>();
    audio->OnBack = [this, screen = audio.get()] { _ui.Close(screen); };
    _ui.Push(std::move(audio));
}

void Application::ShowGraphicsScreen()
{
    auto graphics                = std::make_unique<GraphicsSettingsScreen>();
    graphics->OnBack             = [this, screen = graphics.get()] { _ui.Close(screen); };
    graphics->OnToggleFullscreen = [this] { ToggleFullscreen(); };
    _ui.Push(std::move(graphics));
}
} // namespace Arcbit
