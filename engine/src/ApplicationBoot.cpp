// Boot flow: the engine splash, the game's splashes, then the title menu or
// the first scene (BootConfig). Split out of Application.cpp, which owns the
// main loop.

#include <arcbit/app/Application.h>

#include <arcbit/core/Log.h>
#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/CreditsScreen.h>
#include <arcbit/ui/GraphicsSettingsScreen.h>
#include <arcbit/ui/InputRebindScreen.h>
#include <arcbit/ui/MainMenuScreen.h>
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
    constexpr f32         EngineSplashSeconds = 8.0f;
    constexpr f32         SplashFadeSeconds   = 0.5f;
} // namespace

void Application::StartBoot()
{
    // The scene the player first sees loads under the splash (the main loop
    // runs a queued swap once the splash covers the screen) and fades in as
    // the splash ends.
    const std::string& first = _boot.ShowTitle && !_boot.TitleScene.empty() ? _boot.TitleScene : _boot.FirstScene;
    if (!first.empty()) _scenes.Switch(first);
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
// Continue, shown once there's a save, loads the newest one.
void Application::ShowTitle()
{
    auto      title     = std::make_unique<MainMenuScreen>();
    UIScreen* screen    = title.get();
    title->ShowCredits  = !_boot.CreditsPath.empty();
    title->ShowContinue = !_saves.List().empty();
    title->OnNewGame    = [this, screen] {
        _ui.Close(screen);
        if (_boot.TitleScene.empty() || _boot.FirstScene.empty()) ResetGame();
        else _scenes.Restore({ { _boot.FirstScene, "" } }, [this] { ResetGame(); });
    };
    title->OnContinue = [this, screen] {
        const std::vector<SaveSlotInfo> saves = _saves.List();
        if (!saves.empty() && LoadFrom(saves.front().Slot)) _ui.Close(screen);
    };
    title->OnControls         = [this] { ShowControlsScreen(); };
    title->OnAudioSettings    = [this] { ShowAudioScreen(); };
    title->OnGraphicsSettings = [this] { ShowGraphicsScreen(); };
    title->OnCredits          = [this] { ShowCredits(); };
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
