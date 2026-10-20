// Demo switcher. Every demo is a Scene registered with one DemoApp; F1 opens
// the demo menu, PgUp / PgDn cycle, and the last demo opened is remembered in
// settings. Each demo lives in its own TU and exposes a Demo::Create* factory.
//
// B pushes a small battle arena (assets/scenes/battle.arcscene) over whichever
// demo is running and B again pops back to it, exactly as it was left: the
// scene stack.
//
// `arcbit-game --cycle <seconds>` runs a soak test that needs no keyboard: on
// a timer it pushes the battle, pops it, and moves to the next demo, so every
// scene loads, suspends, resumes and unloads over and over.
//
// The two ways to cover a scene's load: PgUp / PgDn keep the current demo
// running (and playable) until the next one is ready, then fade
// (SceneChange::WaitForLoad); picking from the F1 menu fades out at once, like
// a door, and shows the loading screen if the load outlasts the fade.
// `--load-delay <seconds>` makes every load take that much longer, to see
// both. The battle is preloaded while a demo runs, so B stays instant.
//
// F3 toggles an overlay of the latest events on the event bus - engine events
// (scene changes, device switches, settings) and the demos' own.
//
// `--mute` silences everything for the run (the saved volumes are untouched),
// for unattended soak runs; `--display <n>` opens the window on monitor n
// (0 = the first), to keep a soak run off the main screen.

#include "Common.h"
#include "DemoSelectScreen.h"
#include "EventLogScreen.h"
#include "WidgetTestScreen.h"

#include <arcbit/app/Application.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/scene/FileScene.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/PauseMenuScreen.h>
#include <arcbit/ui/UISkin.h>

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

using namespace Arcbit;

std::span<const Demo::Entry> Demo::Entries()
{
    static const Entry entries[] = {
        { "World", "demo.world", &CreateWorldDemo },
        { "Physics", "demo.physics", &CreatePhysicsDemo },
        { "Physics File", "demo.physics_file",
          [] -> std::unique_ptr<Scene> { return std::make_unique<FileScene>("assets/scenes/physics.arcscene"); } },
        { "Tile Move", "demo.tile_move", &CreateTileMoveDemo },
        { "Arcs", "demo.arcs", &CreateArcDemo },
        { "Tile Arcs", "demo.tile_arcs", &CreateTileMoveArcDemo },
        { "OBB", "demo.obb", &CreateObbDemo },
        { "Triggers", "demo.triggers", &CreateTriggerDemo },
        { "Scripted", "demo.scripted",
          [] -> std::unique_ptr<Scene> { return std::make_unique<FileScene>("assets/scenes/scripted.arcscene"); } },
    };
    return entries;
}

namespace
{
constexpr ActionID ActionDemoMenu = MakeAction("Demo_Menu");
constexpr ActionID ActionDemoNext = MakeAction("Demo_Next");
constexpr ActionID ActionDemoPrev   = MakeAction("Demo_Prev");
constexpr ActionID ActionDemoBattle = MakeAction("Demo_Battle");
constexpr ActionID ActionEventLog   = MakeAction("Demo_EventLog");
constexpr ActionID ActionQuickSave  = MakeAction("Demo_QuickSave");
constexpr ActionID ActionQuickLoad  = MakeAction("Demo_QuickLoad");
constexpr ActionID ActionPause      = MakeAction("Pause");
constexpr ActionID ActionWidgetTest = MakeAction("Debug_WidgetTest");

constexpr const char* BattleScene = "Battle";

constexpr const char* LastDemoKey = "demo.last";
} // namespace

// Debug builds run the scripts and locale files in the source tree, so edits hot-reload; the
// copy in bin/ is only refreshed by a build.
#ifdef ARCBIT_DEBUG
constexpr const char* ScriptRoot   = ARCBIT_GAME_ASSETS "/scripts";
constexpr const char* LocaleFolder = ARCBIT_GAME_ASSETS "/locale";
constexpr const char* DialogFolder = ARCBIT_GAME_ASSETS "/dialog";
#else
constexpr const char* ScriptRoot   = "assets/scripts";
constexpr const char* LocaleFolder = "assets/locale";
constexpr const char* DialogFolder = "assets/dialog";
#endif

class DemoApp : public Application
{
public:
    struct Options
    {
        f32  CycleSeconds = 0.0f;  // --cycle
        f32  LoadDelay    = 0.0f;  // --load-delay
        bool Mute         = false; // --mute
        i32  Display      = -1;    // --display
    };

    explicit DemoApp(const Options& options) :
        Application({ .Title = "Arcbit Demos", .Width = 1920, .Height = 1080, .ScriptRoot = ScriptRoot,
                      .Display = options.Display, .LocaleFolder = LocaleFolder,
                      .DialogFolder = DialogFolder }),
        _cycleSeconds(options.CycleSeconds), _loadDelay(options.LoadDelay), _mute(options.Mute)
    {
        // Splash, then the title menu over the last demo (already running
        // behind it); the soak test skips the title so it can drive itself.
        _boot.ShowTitle   = _cycleSeconds <= 0.0f;
        _boot.CreditsPath = "assets/credits.json";

        // Autosave whenever a demo or the battle finishes loading; the soak
        // test skips that and the saving notice (nobody to read it).
        _saving.AutoSaveOnSceneChange = _cycleSeconds <= 0.0f;
        _saving.Notice                = _cycleSeconds <= 0.0f ? SaveNotice::FirstLaunch : SaveNotice::Never;
    }

protected:
    void OnStart() override
    {
        GetUI().SetSkin(UISkin::LoadFromFile("assets/engine/skins/default.skin.json"));
        Demo::RegisterInput(GetInput());
        Demo::RegisterSceneTypes(GetSceneRegistry());
        GetSaves().SetSealKey("arcbit-demos/saves"); // the demo's own save key (Phase 36 replaces it)
        _eventHistory = std::make_unique<EventHistory>(GetEvents()); // from the start, so F3 shows the boot too
        for (const Demo::Entry& e : Demo::Entries()) GetScenes().Register(e.Name, e.Create);
        GetScenes().Register(BattleScene, [] { return std::make_unique<FileScene>("assets/scenes/battle.arcscene"); });
        GetScenes().PrepareDelay = _loadDelay;
        // A loaded save picks its own demo: keep the title and F1 menu in step.
        _localeChanged = GetEvents().Subscribe(EngineEvents::LocaleChanged, [this](const Event&) { RetitleWindow(); });
        _sceneLoaded = GetEvents().Subscribe(EngineEvents::SceneLoaded, [this](const Event& e) {
            FollowLoadedScene(e.Payload.Get<std::string>("name", ""));
        });
        if (_mute) AudioManager::SetVolume(AudioCategories::Master, 0.0f); // this run only, not saved
        Open(LastOpenedIndex());
    }

    void OnUpdate(const f32 dt) override
    {
        // Load the battle ahead while a demo runs, so B only has to build it.
        if (!GetScenes().IsTransitioning() && GetScenes().GetDepth() == 1) GetScenes().Preload(BattleScene);

        if (GetInput().JustPressed(ActionEventLog)) ToggleEventLog(); // works over menus too
        // F1 opens the demo menu over any other menu, swapping it out (the
        // demo menu closes itself on F1).
        if (GetInput().JustPressed(ActionDemoMenu) && !GetUI().IsOpen(_demoMenu)) ShowMenu();
        HandlePauseKey();
        if (GetUI().HasBlockingScreen()) return; // a menu owns the other keys
        if (GetInput().JustPressed(ActionWidgetTest)) ShowWidgetTest();

        const size_t count = Demo::Entries().size();
        if (GetInput().JustPressed(ActionQuickSave)) (void)SaveTo(SaveSlot::Quick());
        else if (GetInput().JustPressed(ActionQuickLoad)) (void)LoadFrom(SaveSlot::Quick());
        else if (CycleDue(dt)) CycleStep(count);
        else if (GetInput().JustPressed(ActionDemoNext)) Open((_current + 1) % count, { .WaitForLoad = true });
        else if (GetInput().JustPressed(ActionDemoPrev)) Open((_current + count - 1) % count, { .WaitForLoad = true });
        else if (GetInput().JustPressed(ActionDemoBattle)) ToggleBattle();
    }

private:
    void Open(const size_t index, SceneChange change = {})
    {
        _current               = index;
        const Demo::Entry& demo = Demo::Entries()[index];
        GetScenes().Switch(demo.Name, std::move(change));
        SetDemoTitle(demo);
        Settings::Set<std::string>(LastDemoKey, demo.Name);
    }

    void SetDemoTitle(const Demo::Entry& demo)
    {
        const std::string key(demo.Key);
        SetWindowTitle(Loc::Format("demo.window_title", { { "name", Loc::Get(key + ".name") }, { "keys", Loc::Get(key + ".keys") } }));
    }

    // The title is text too: the language changed, so it follows.
    void RetitleWindow()
    {
        if (GetScenes().GetDepth() > 1) SetWindowTitle(Loc::Get("demo.window_battle"));
        else SetDemoTitle(Demo::Entries()[_current]);
    }

    // A scene loaded some other way than Open (a save loading): if it's one
    // of the demos, it's the current one now.
    void FollowLoadedScene(const std::string& name)
    {
        const auto entries = Demo::Entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].Name != name || i == _current) continue;
            _current = i;
            SetDemoTitle(entries[i]);
            Settings::Set<std::string>(LastDemoKey, entries[i].Name);
        }
    }

    // B: push the battle arena over the running demo; B again pops back to
    // the demo, which was suspended rather than unloaded.
    void ToggleBattle()
    {
        if (GetScenes().IsTransitioning()) return;
        if (GetScenes().GetDepth() > 1) {
            GetScenes().Pop();
            SetDemoTitle(Demo::Entries()[_current]);
            return;
        }
        GetScenes().Push(BattleScene);
        SetWindowTitle(Loc::Get("demo.window_battle"));
    }

    // Pause opens the pause menu - over any demo, the battle too - swapping out
    // any other menu, like the demo switcher, and closes it again (with
    // whatever settings screen it has open). While a menu is open, a pause key
    // that is also "back" (Escape) is that menu's back, not a new pause:
    // gamepad Start swaps, Escape closes.
    void HandlePauseKey()
    {
        UIManager& ui = GetUI();
        if (!GetInput().JustPressed(ActionPause)) return;
        if (ui.IsOpen(_pauseMenu)) {
            if (!UIManager::IsBackPressed(GetInput())) ui.CloseMenus(); // Escape: the menu's own back
            return;
        }
        if (ui.HasOpenMenu() && UIManager::IsBackPressed(GetInput())) return;
        if (CanCancelDialog() && UIManager::IsBackPressed(GetInput())) return; // Escape backs out of the talk
        ShowPauseMenu();
    }

    void ShowPauseMenu()
    {
        auto pause                = std::make_unique<PauseMenuScreen>();
        _pauseMenu                = pause.get();
        pause->OnResume           = [this] { GetUI().Close(_pauseMenu); };
        pause->ShowSaveGame       = true;
        pause->ShowLoadGame       = true;
        pause->OnSaveGame         = [this] { ShowSaveScreen(); };
        pause->OnLoadGame         = [this] { ShowLoadScreen(); };
        pause->OnControls         = [this] { ShowControlsScreen(); };
        pause->OnAudioSettings    = [this] { ShowAudioScreen(); };
        pause->OnGraphicsSettings = [this] { ShowGraphicsScreen(); };
        pause->OnGameSettings     = [this] { ShowGameSettingsScreen(); };
        pause->OnQuit             = [this] { RequestShutdown(); };
        GetUI().OpenMenu(std::move(pause));
    }

    // T: the widget test screen, every widget in one place.
    void ShowWidgetTest()
    {
        auto      screen = std::make_unique<WidgetTestScreen>();
        UIScreen* ptr    = screen.get();
        screen->OnClose  = [this, ptr] { GetUI().Close(ptr); };
        screen->OnDialog = [this, ptr] { // the sample conversation (assets/dialog/sample.arcdialog)
            GetUI().Close(ptr);
            (void)StartDialog("sample");
        };
        GetUI().OpenMenu(std::move(screen));
    }

    void ToggleEventLog()
    {
        if (_eventLog) {
            GetUI().Close(_eventLog);
            _eventLog = nullptr;
            return;
        }
        auto screen     = std::make_unique<EventLogScreen>();
        screen->History = _eventHistory.get();
        _eventLog       = screen.get();
        GetUI().Push(std::move(screen));
    }

    [[nodiscard]] size_t LastOpenedIndex() const
    {
        const std::string last    = Settings::Get<std::string>(LastDemoKey, "");
        const auto        entries = Demo::Entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (last == entries[i].Name) return i;
        return 0;
    }

    void ShowMenu()
    {
        auto menu     = std::make_unique<DemoSelectScreen>();
        _demoMenu     = menu.get();
        menu->Current = _current;
        menu->OnClose = [this] { GetUI().Close(_demoMenu); };
        menu->OnPick  = [this](const size_t index) {
            GetUI().Close(_demoMenu);
            // Under a pushed battle, the current demo is suspended: picking
            // it still switches (back) to it.
            if (index != _current || GetScenes().GetDepth() > 1) Open(index);
        };
        GetUI().OpenMenu(std::move(menu)); // swaps out the pause menu, if it's open
    }

    // --cycle: push the battle, pop it, next demo, repeat.
    void CycleStep(const size_t count)
    {
        if (_cycleStep++ % 3 == 2) Open((_current + 1) % count);
        else ToggleBattle();
    }

    // --cycle: true once per interval, counted only while no switch is running.
    bool CycleDue(const f32 dt)
    {
        if (_cycleSeconds <= 0.0f || GetScenes().IsTransitioning()) return false;
        _cycleTimer += dt;
        if (_cycleTimer < _cycleSeconds) return false;
        _cycleTimer = 0.0f;
        return true;
    }

    std::unique_ptr<EventHistory> _eventHistory;
    Subscription                  _sceneLoaded;   // FollowLoadedScene
    Subscription                  _localeChanged; // RetitleWindow
    const UIScreen*               _eventLog = nullptr; // the F3 overlay while shown
    const UIScreen*               _demoMenu = nullptr; // the F1 menu (check GetUI().IsOpen)
    const UIScreen*               _pauseMenu = nullptr; // Esc / Start (check GetUI().IsOpen)

    size_t _current      = 0;
    f32    _cycleSeconds = 0.0f;
    f32    _loadDelay    = 0.0f;
    bool   _mute         = false;
    f32    _cycleTimer   = 0.0f;
    u32    _cycleStep    = 0;
};

int main(const int argc, char* argv[])
{
    DemoApp::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg  = argv[i];
        const bool             more = i + 1 < argc;
        if (arg == "--cycle" && more) options.CycleSeconds = std::stof(argv[i + 1]);
        if (arg == "--load-delay" && more) options.LoadDelay = std::stof(argv[i + 1]);
        if (arg == "--mute") options.Mute = true;
        if (arg == "--display" && more) options.Display = std::stoi(argv[i + 1]);
    }

    DemoApp app(options);
    app.Run();
    return EXIT_SUCCESS;
}
