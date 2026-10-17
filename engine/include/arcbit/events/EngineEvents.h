#pragma once

#include <arcbit/events/EventBus.h>

#include <array>
#include <string_view>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// The engine's own events (docs/events.md). Game code subscribes to them;
// only the engine publishes them (EventBus::PublishEngine). Payload fields
// are listed with each.
// ---------------------------------------------------------------------------
namespace EngineEvents
{
    // A scene finished OnLoad and is active. name, entry.
    constexpr std::string_view SceneLoadedName = "engine.scene.loaded";
    // A scene was unloaded. name.
    constexpr std::string_view SceneUnloadedName = "engine.scene.unloaded";
    // A scene was pushed over (it's paused, not gone). name.
    constexpr std::string_view SceneSuspendedName = "engine.scene.suspended";
    // The scene above was popped and this one is active again. name.
    constexpr std::string_view SceneResumedName = "engine.scene.resumed";

    // The player used a different kind of device than before - swap button
    // hints. device: "keyboard_mouse" or "gamepad".
    constexpr std::string_view InputDeviceChangedName = "engine.input.device_changed";

    // The window gained or lost focus (pause on alt-tab). focused.
    constexpr std::string_view WindowFocusChangedName = "engine.window.focus_changed";
    // The window's size changed. size (Vec2, pixels).
    constexpr std::string_view WindowResizedName = "engine.window.resized";

    // The player changed a setting. section: "audio", "graphics", "input",
    // or "game" for Settings::Set (then key: the setting's key).
    constexpr std::string_view SettingsChangedName = "engine.settings.changed";

    // An entity with EntityEvents appeared / was destroyed (opt-in per
    // entity; see EntityEvents for renaming them). entity, name, tag.
    constexpr std::string_view EntitySpawnedName   = "engine.entity.spawned";
    constexpr std::string_view EntityDestroyedName = "engine.entity.destroyed";

    // The game was saved / its save loaded (Application::SaveTo / LoadFrom;
    // loaded fires once the save's state is back, as its scenes load). slot:
    // "manual_3", "auto_1" or "quick"; saved also name.
    constexpr std::string_view GameSavedName  = "engine.game.saved";
    constexpr std::string_view GameLoadedName = "engine.game.loaded";

    constexpr EventId SceneLoaded        = MakeEvent(SceneLoadedName);
    constexpr EventId SceneUnloaded      = MakeEvent(SceneUnloadedName);
    constexpr EventId SceneSuspended     = MakeEvent(SceneSuspendedName);
    constexpr EventId SceneResumed       = MakeEvent(SceneResumedName);
    constexpr EventId InputDeviceChanged = MakeEvent(InputDeviceChangedName);
    constexpr EventId WindowFocusChanged = MakeEvent(WindowFocusChangedName);
    constexpr EventId WindowResized      = MakeEvent(WindowResizedName);
    constexpr EventId SettingsChanged    = MakeEvent(SettingsChangedName);
    constexpr EventId EntitySpawned      = MakeEvent(EntitySpawnedName);
    constexpr EventId EntityDestroyed    = MakeEvent(EntityDestroyedName);
    constexpr EventId GameSaved          = MakeEvent(GameSavedName);
    constexpr EventId GameLoaded         = MakeEvent(GameLoadedName);

    // Every engine event name - each EventBus registers them all, so they're
    // reserved and named in logs from the start.
    constexpr std::array AllNames = {
        SceneLoadedName,        SceneUnloadedName,      SceneSuspendedName, SceneResumedName,
        InputDeviceChangedName, WindowFocusChangedName, WindowResizedName,  SettingsChangedName,
        EntitySpawnedName,      EntityDestroyedName,    GameSavedName,      GameLoadedName,
    };
} // namespace EngineEvents
} // namespace Arcbit
