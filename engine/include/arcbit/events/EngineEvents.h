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

    // An update placed new (or reset) actions among the player's bindings
    // (Settings::LoadInputBindings), published once the boot is done. count,
    // text (the notice's lines, one per action, localized).
    constexpr std::string_view BindingsUpdatedName = "engine.input.bindings_updated";

    // A game bag (Inventories) gained or lost items. bag, item, count (how
    // many - always positive).
    constexpr std::string_view ItemAddedName   = "engine.item.added";
    constexpr std::string_view ItemRemovedName = "engine.item.removed";
    // A currency's amount changed (Currencies). currency, amount (now), change.
    constexpr std::string_view CurrencyChangedName = "engine.currency.changed";

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

    // An Interactor used an Interactable (docs/scripting.md). entity (the
    // one used), by (the interactor).
    constexpr std::string_view EntityInteractedName = "engine.entity.interacted";

    // The game was saved / its save loaded (Application::SaveTo / LoadFrom;
    // loaded fires once the save's state is back, as its scenes load). slot:
    // "manual_3", "auto_1" or "quick"; saved also name.
    constexpr std::string_view GameSavedName  = "engine.game.saved";
    constexpr std::string_view GameLoadedName = "engine.game.loaded";

    // The player-visible language changed (Application::SetLanguage). from,
    // to: locale codes.
    constexpr std::string_view LocaleChangedName = "engine.locale.changed";

    // A conversation (docs/dialog.md) started / ended. id, with (the entity
    // talked to, if any); ended also choice (the last choice's id, or "").
    constexpr std::string_view DialogStartedName = "engine.dialog.started";
    constexpr std::string_view DialogEndedName   = "engine.dialog.ended";
    // A line showed. id, key (its text's Loc key), speaker (an id; "" for
    // narration).
    constexpr std::string_view DialogLineName = "engine.dialog.line";
    // The player picked a choice. id, choice (its id).
    constexpr std::string_view DialogChoiceName = "engine.dialog.choice";

    constexpr EventId SceneLoaded        = MakeEvent(SceneLoadedName);
    constexpr EventId SceneUnloaded      = MakeEvent(SceneUnloadedName);
    constexpr EventId SceneSuspended     = MakeEvent(SceneSuspendedName);
    constexpr EventId SceneResumed       = MakeEvent(SceneResumedName);
    constexpr EventId InputDeviceChanged = MakeEvent(InputDeviceChangedName);
    constexpr EventId BindingsUpdated    = MakeEvent(BindingsUpdatedName);
    constexpr EventId ItemAdded          = MakeEvent(ItemAddedName);
    constexpr EventId ItemRemoved        = MakeEvent(ItemRemovedName);
    constexpr EventId CurrencyChanged    = MakeEvent(CurrencyChangedName);
    constexpr EventId WindowFocusChanged = MakeEvent(WindowFocusChangedName);
    constexpr EventId WindowResized      = MakeEvent(WindowResizedName);
    constexpr EventId SettingsChanged    = MakeEvent(SettingsChangedName);
    constexpr EventId EntitySpawned      = MakeEvent(EntitySpawnedName);
    constexpr EventId EntityDestroyed    = MakeEvent(EntityDestroyedName);
    constexpr EventId EntityInteracted   = MakeEvent(EntityInteractedName);
    constexpr EventId GameSaved          = MakeEvent(GameSavedName);
    constexpr EventId GameLoaded         = MakeEvent(GameLoadedName);
    constexpr EventId LocaleChanged      = MakeEvent(LocaleChangedName);
    constexpr EventId DialogStarted      = MakeEvent(DialogStartedName);
    constexpr EventId DialogEnded        = MakeEvent(DialogEndedName);
    constexpr EventId DialogLine         = MakeEvent(DialogLineName);
    constexpr EventId DialogChoice       = MakeEvent(DialogChoiceName);

    // Every engine event name - each EventBus registers them all, so they're
    // reserved and named in logs from the start.
    constexpr std::array AllNames = {
        SceneLoadedName,        SceneUnloadedName,      SceneSuspendedName, SceneResumedName,
        InputDeviceChangedName, WindowFocusChangedName, WindowResizedName,  SettingsChangedName,
        EntitySpawnedName,      EntityDestroyedName,    GameSavedName,      GameLoadedName,
        EntityInteractedName,   LocaleChangedName,      DialogStartedName,  DialogEndedName,
        DialogLineName,         DialogChoiceName,       BindingsUpdatedName,
        ItemAddedName,          ItemRemovedName,        CurrencyChangedName,
    };
} // namespace EngineEvents
} // namespace Arcbit
