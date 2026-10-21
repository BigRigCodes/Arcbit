#pragma once

#include <arcbit/core/NumberCast.h>
#include <arcbit/core/Types.h>
#include <arcbit/input/BindingUpdate.h>

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Arcbit {

class InputManager;

// ---------------------------------------------------------------------------
// GraphicsSettings
//
// Controls display and rendering options. Inspect or modify fields directly,
// then call Settings::MarkDirty() (or use Settings::Set<T>() which does it
// automatically). Changes take effect the next time the engine reads them —
// VSync and fullscreen require re-creating the swapchain (Phase 11).
// ---------------------------------------------------------------------------
struct GraphicsSettings
{
    bool VSync            = true;
    u32  ResolutionWidth  = 1280;
    u32  ResolutionHeight = 720;
    bool Fullscreen       = false;

    // Maximum frames per second when VSync is off. 0 = unlimited.
    // Typical values: 60, 120, 144, 165, 240, 250.
    u32  FpsLimit         = 0;

    bool ShowFps       = false; // show the HUD FPS label
    bool ShowDebugInfo = false; // show the engine debug overlay (sprites, lights, etc.)
};

// ---------------------------------------------------------------------------
// AudioSettings
//
// Volume levels are in [0, 1] and mean perceived loudness: 1 is the mix as
// the game was authored, 0.5 sounds about half as loud, 0.1 about a tenth
// (AudioManager::SliderToGain turns them into gain). One per audio category
// (AudioCategories.h), by name - "master", "music", a game's "music.combat";
// a category not listed is at 1. Saved as "audio": { "volumes": { ... } };
// an older file's master_volume / music_volume / sfx_volume read into them.
// ---------------------------------------------------------------------------
struct AudioSettings
{
    std::map<std::string, f32, std::less<>> Volumes;

    [[nodiscard]] f32 Volume(const std::string_view category) const
    {
        const auto it = Volumes.find(category);
        return it != Volumes.end() ? it->second : 1.0f;
    }
};

// ---------------------------------------------------------------------------
// Settings
//
// A static, globally-accessible store for all user-facing configuration.
// Data is persisted to a JSON file on disk; loaded at startup and written on
// shutdown (or any explicit Flush() call).
//
// Typical usage
// -------------
//   // Startup:
//   Settings::Init("settings.json");
//   Settings::LoadInputBindings(input);      // restore last session's bindings
//
//   // Runtime (e.g. from a settings screen):
//   Settings::Graphics.VSync = false;
//   Settings::MarkDirty();
//   // or equivalently:
//   Settings::Set<bool>("graphics.vsync", false);
//
//   // Shutdown:
//   Settings::SaveInputBindings(input);      // capture any runtime rebinds
//   Settings::Shutdown();                    // flushes if dirty
//
// Generic Get/Set
// ---------------
//   Supported types: bool, i32, u32, f32, std::string.
//   Keys use a dot-path convention ("game.difficulty", "debug.show_fps").
//   They are persisted under a "custom" section in the JSON file, separate
//   from the well-known graphics/audio/input sections.
//
// Thread safety
// -------------
//   NOT thread-safe. Call all Settings methods from the main/game thread only.
// ---------------------------------------------------------------------------
class Settings
{
public:
    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Load settings from the given file path. If the file does not exist,
    // defaults are used and the file will be created on the next Flush().
    // Must be called before any Get/Set or binding load/save calls.
    static void Init(std::string_view path = "settings.json");

    // Flush if dirty, then release internal state.
    // Call after all systems have saved their settings (e.g. input bindings).
    static void Shutdown();

    // Write current settings to disk immediately and clear the dirty flag.
    static void Flush();

    // True if any setting has changed since the last Init() or Flush().
    [[nodiscard]] static bool IsDirty() { return s_dirty; }

    // Mark settings as modified. Call after writing directly to Graphics or
    // Audio fields so Shutdown()/Flush() knows to persist the change. Name
    // the section ("audio", "graphics", "input") when the player changed it,
    // so the change listener hears about it.
    static void MarkDirty(std::string_view section = {});

    // Told about every player-facing change: MarkDirty with a section, and
    // Set<T> (section "game", plus the key). Application publishes these as
    // engine.settings.changed. Empty to clear.
    using ChangeListener = std::function<void(std::string_view section, std::string_view key)>;
    static void SetChangeListener(ChangeListener listener) { s_changeListener = std::move(listener); }

    // -----------------------------------------------------------------------
    // Well-known settings sections
    //
    // Read and write fields directly on these structs. Call MarkDirty() after
    // any write, or use Set<T>() which marks dirty automatically.
    // -----------------------------------------------------------------------
    static GraphicsSettings Graphics;
    static AudioSettings    Audio;

    // -----------------------------------------------------------------------
    // Generic typed accessors
    //
    // Supported types: bool, i32, u32, f32, std::string.
    // Values are stored separately from the well-known sections.
    // -----------------------------------------------------------------------

    // Returns the stored value for key, or defaultValue if not found.
    //
    // Numbers convert between i32 / u32 / f32: JSON has a single number type,
    // so a value written by Set<u32> can be read back from the file as an i32
    // (and vice versa). An integer that doesn't fit in T (e.g. a negative
    // number read as u32) returns the default.
    // bool and std::string must match exactly; other mismatches return the
    // default.
    template<typename T>
    [[nodiscard]] static T Get(std::string_view key, const T& defaultValue = T{})
    {
        const auto it = s_store.find(std::string(key));
        if (it == s_store.end()) return defaultValue;
        if (const T* val = std::get_if<T>(&it->second)) return *val;
        return std::visit([&](const auto& stored) { return ConvertNumber<T>(stored, defaultValue); }, it->second);
    }

    // Store a value and mark dirty. Overwrites any previous value for this key.
    template<typename T>
    static void Set(std::string_view key, const T& value)
    {
        s_store[std::string(key)] = Value(value);
        s_dirty = true;
        if (s_changeListener) s_changeListener("game", key);
    }

    // -----------------------------------------------------------------------
    // Input binding persistence
    // -----------------------------------------------------------------------

    // Serialize all registered actions and their current bindings into the
    // in-memory settings document. Call before Shutdown() to capture any
    // runtime rebinds made via InputManager (e.g. from a settings screen).
    static void SaveInputBindings(const InputManager& input);

    // Read the player's bindings from the settings document and apply them
    // over the defaults the game registered, keeping what the player is
    // used to across game updates (BindingUpdate.h, docs/input.md): actions
    // they had keep their bindings; new ones (and resets) take free keys.
    // The defaults are recorded with the bindings, so the next load can tell
    // what changed. Unknown action names in the file are ignored. Returns
    // true if the file had bindings.
    static bool LoadInputBindings(InputManager& input);

    // Put an action back to its new default in an update that changed it -
    // an old default that was broken - through the same clash rules and
    // notice as a new action. Call before LoadInputBindings (in OnStart);
    // once the player's file records the new default, it no longer applies.
    static void ResetBindingOnUpdate(std::string_view action);

    // What the last LoadInputBindings placed (new and reset actions), for
    // the one-time notice; empty when nothing changed.
    [[nodiscard]] static const std::vector<BindingChange>& GetBindingChanges();

private:
    // The variant covers all types supported by Get<T>/Set<T>.
    using Value = std::variant<bool, i32, u32, f32, std::string>;

    static bool                                   s_dirty;
    static ChangeListener                         s_changeListener;
    static std::string                            s_filePath;
    static std::unordered_map<std::string, Value> s_store;

    // Helpers called by Init() and Flush().
    static void ReadFile();
    static void WriteFile();

    // Sync the GraphicsSettings / AudioSettings structs to/from s_Store.
    // SyncFromStore copies the known-section values out of the loaded JSON
    // into the public structs; SyncToStore does the reverse before writing.
    static void SyncFromStore();
    static void SyncToStore();
};

} // namespace Arcbit
