// Demo switcher. Every demo is a Scene registered with one DemoApp; F1 opens
// the demo menu, PgUp / PgDn cycle, and the last demo opened is remembered in
// settings. Each demo lives in its own TU and exposes a Demo::Create* factory.
//
// `arcbit-game --cycle <seconds>` switches to the next demo on a timer — a
// soak test for scene loading / unloading that needs no keyboard.

#include "Common.h"
#include "DemoSelectScreen.h"

#include <arcbit/app/Application.h>
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
        { "Triggers", "triggers and collision events (22F)", "M movement style, F2 colliders, G grid",
          &CreateTriggerDemo },
    };
    return entries;
}

namespace
{
constexpr ActionID ActionDemoMenu = MakeAction("Demo_Menu");
constexpr ActionID ActionDemoNext = MakeAction("Demo_Next");
constexpr ActionID ActionDemoPrev = MakeAction("Demo_Prev");

constexpr const char* LastDemoKey = "demo.last";
} // namespace

class DemoApp : public Application
{
public:
    explicit DemoApp(const f32 cycleSeconds) :
        Application({ .Title = "Arcbit Demos", .Width = 1920, .Height = 1080 }), _cycleSeconds(cycleSeconds)
    {}

protected:
    void OnStart() override
    {
        GetUI().SetSkin(UISkin::LoadFromFile("assets/engine/skins/default.skin.json"));
        Demo::RegisterInput(GetInput());
        Demo::RegisterSceneTypes(GetSceneRegistry());
        for (const Demo::Entry& e : Demo::Entries()) GetScenes().Register(e.Name, e.Create);
        Open(LastOpenedIndex());
    }

    void OnUpdate(const f32 dt) override
    {
        if (GetUI().HasBlockingScreen()) return; // a menu owns the keys (the demo menu closes itself)

        const size_t count = Demo::Entries().size();
        if (CycleDue(dt)) Open((_current + 1) % count);
        else if (GetInput().JustPressed(ActionDemoMenu)) ShowMenu();
        else if (GetInput().JustPressed(ActionDemoNext)) Open((_current + 1) % count);
        else if (GetInput().JustPressed(ActionDemoPrev)) Open((_current + count - 1) % count);
    }

private:
    void Open(const size_t index)
    {
        _current               = index;
        const Demo::Entry& demo = Demo::Entries()[index];
        GetScenes().Switch(demo.Name);
        SetWindowTitle(std::format("Arcbit Demos - {}  ({})   |   F1 demos, PgUp/PgDn cycle", demo.Name, demo.Keys));
        Settings::Set<std::string>(LastDemoKey, demo.Name);
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
            if (index != _current) Open(index);
        };
        GetUI().Push(std::move(menu));
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

    size_t _current      = 0;
    f32    _cycleSeconds = 0.0f;
    f32    _cycleTimer   = 0.0f;
};

int main(const int argc, char* argv[])
{
    f32 cycleSeconds = 0.0f;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--cycle") cycleSeconds = std::stof(argv[i + 1]);

    DemoApp app(cycleSeconds);
    app.Run();
    return EXIT_SUCCESS;
}
