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
// for unattended soak runs.

#include "Common.h"
#include "DemoSelectScreen.h"
#include "EventLogScreen.h"

#include <arcbit/app/Application.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/scene/FileScene.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/UISkin.h>

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

using namespace Arcbit;

std::span<const Demo::Entry> Demo::Entries()
{
    static const Entry entries[] = {
        { "World", "tilemap world: lights, audio, ships, HUD",
          "Esc pause, T widgets, Space attack, E shake, G grid", &CreateWorldDemo },
        { "Physics", "free movement, knockback bumper, pushable crate (22B, 22I)", "F2 colliders, G grid",
          &CreatePhysicsDemo },
        { "Physics File", "the Physics demo as a scene file - no C++ scene class (23)", "F2 colliders",
          [] -> std::unique_ptr<Scene> { return std::make_unique<FileScene>("assets/scenes/physics.arcscene"); } },
        { "Tile Move", "tile movement, smooth and snap, pushable crates (22C, 22H, 22I)",
          "M smooth/snap, F2 colliders, G grid",
          &CreateTileMoveDemo },
        { "Arcs", "directional colliders, free movement (22D)", "F2 colliders, G grid", &CreateArcDemo },
        { "Tile Arcs", "directional colliders, tile movement (22D)", "F2 colliders, G grid",
          &CreateTileMoveArcDemo },
        { "OBB", "rotated colliders (22E)", "R pause gate, C player shape, F2 colliders, G grid", &CreateObbDemo },
        { "Triggers", "triggers, collision events, coins on the event bus (22F, 24)",
          "M movement style, F2 colliders, G grid, F3 events",
          &CreateTriggerDemo },
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

constexpr const char* BattleScene = "Battle";

constexpr const char* LastDemoKey = "demo.last";
} // namespace

class DemoApp : public Application
{
public:
    struct Options
    {
        f32  CycleSeconds = 0.0f;  // --cycle
        f32  LoadDelay    = 0.0f;  // --load-delay
        bool Mute         = false; // --mute
    };

    explicit DemoApp(const Options& options) :
        Application({ .Title = "Arcbit Demos", .Width = 1920, .Height = 1080 }),
        _cycleSeconds(options.CycleSeconds), _loadDelay(options.LoadDelay), _mute(options.Mute)
    {
        // Splash, then the title menu over the last demo (already running
        // behind it); the soak test skips the title so it can drive itself.
        _boot.ShowTitle   = _cycleSeconds <= 0.0f;
        _boot.CreditsPath = "assets/credits.json";
    }

protected:
    void OnStart() override
    {
        GetUI().SetSkin(UISkin::LoadFromFile("assets/engine/skins/default.skin.json"));
        Demo::RegisterInput(GetInput());
        Demo::RegisterSceneTypes(GetSceneRegistry());
        _eventHistory = std::make_unique<EventHistory>(GetEvents()); // from the start, so F3 shows the boot too
        for (const Demo::Entry& e : Demo::Entries()) GetScenes().Register(e.Name, e.Create);
        GetScenes().Register(BattleScene, [] { return std::make_unique<FileScene>("assets/scenes/battle.arcscene"); });
        GetScenes().PrepareDelay = _loadDelay;
        if (_mute) AudioManager::SetMasterVolume(0.0f); // this run only, not saved
        Open(LastOpenedIndex());
    }

    void OnUpdate(const f32 dt) override
    {
        // Load the battle ahead while a demo runs, so B only has to build it.
        if (!GetScenes().IsTransitioning() && GetScenes().GetDepth() == 1) GetScenes().Preload(BattleScene);

        if (GetInput().JustPressed(ActionEventLog)) ToggleEventLog(); // works over menus too
        if (GetUI().HasBlockingScreen()) return; // a menu owns the keys (the demo menu closes itself)

        const size_t count = Demo::Entries().size();
        if (CycleDue(dt)) CycleStep(count);
        else if (GetInput().JustPressed(ActionDemoMenu)) ShowMenu();
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
        SetWindowTitle(std::format("Arcbit Demos - {}  ({})   |   F1 demos, PgUp/PgDn cycle, B battle, F3 events", demo.Name,
                                   demo.Keys));
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
        SetWindowTitle("Arcbit Demos - Battle, pushed over the demo   |   B returns to it, WASD move");
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
        menu->Current = _current;
        menu->OnClose = [this] { GetUI().Pop(); };
        menu->OnPick  = [this](const size_t index) {
            GetUI().Pop();
            // Under a pushed battle, the current demo is suspended: picking
            // it still switches (back) to it.
            if (index != _current || GetScenes().GetDepth() > 1) Open(index);
        };
        GetUI().Push(std::move(menu));
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
    const UIScreen*               _eventLog = nullptr; // the F3 overlay while shown

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
    }

    DemoApp app(options);
    app.Run();
    return EXIT_SUCCESS;
}
