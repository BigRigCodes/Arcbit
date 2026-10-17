#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/ScenePreloader.h>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit {

class Application;
class EventBus;
class InputManager;
class SceneAssets;
class SceneRegistry;
struct WorldState;
class MusicPlayer;
class TextureManager;

// ---------------------------------------------------------------------------
// SceneChange - how a Switch or Push goes.
//
// Two ways to cover the next scene's load (its OnPrepare, on the loader
// thread):
//
//   Fade first (default)  The fade-out starts at once and the load runs under
//                         it; if the load outlasts the fade, the screen holds
//                         at black and Application shows its loading screen.
//                         For doors: instant feedback, nowhere to wander off.
//   WaitForLoad           The current scene stays on screen and playable while
//                         the next one loads; the fade-out starts once it's
//                         ready. For menu jumps, or a game that keeps playing
//                         while it loads where you're going.
// ---------------------------------------------------------------------------
struct SceneChange
{
    std::string Entry;                     // handed to the new scene (Scene::GetEntryPoint)
    bool        WaitForLoad       = false; // keep the current scene running until the next is ready
    bool        ShowLoadingScreen = true;  // fade first: the loading screen over a long hold at black
};

// One scene on the stack, as a save records it.
struct SceneStackEntry
{
    std::string Name;
    std::string Entry;

    bool operator==(const SceneStackEntry& other) const = default;
};

// ---------------------------------------------------------------------------
// SceneManager — owns a stack of scenes and switches between registered ones.
//
// Scenes are registered by name with a factory, so the same call covers a
// C++ subclass and a data-driven scene loaded from a file:
//
//   scenes.Register("Village", [] { return std::make_unique<VillageScene>(); });
//   scenes.Switch("Village", "north_gate");   // queued; safe from any hook
//
// A switch fades the screen to black, swaps scenes while the render thread is
// idle, then fades back in:
//
//   Idle ──Switch──▶ FadingOut ──alpha 1──▶ swap ──▶ FadingIn ──alpha 0──▶ Idle
//
// Switching away from an unnamed scene (Application's default, or none at
// all) skips the fade-out: there's nothing worth fading. A Switch during the
// fade-out retargets it; during the fade-in it reverses from the current
// alpha.
//
// Swap order: old OnUnload → new OnLoad → old scene's resources released →
// old scene destroyed. Loading the new scene first keeps textures the two
// share alive instead of freeing and reloading them.
//
// Loading ahead: a Switch or Push creates the next scene at once and runs its
// OnPrepare (file reads, image decodes) on a loader thread. The swap itself -
// OnLoad, which then only uploads and builds entities - waits until the
// prepare is done. SceneChange picks what the player sees meanwhile: the
// fade-out and then black with a loading screen (the default), or the current
// scene still running (WaitForLoad). Preload(name) starts that work early,
// with the current scene still running, so the switch later finds it done.
//
// Music follows the active scene (Scene::SetMusic): at each swap the new
// active scene's track crossfades in (MusicFadeTime), the same track carries
// on, and leaving a scene that owns music for one that doesn't fades it out.
// A suspended scene's track resumes where it was when it's popped back to.
//
// Saved scene state (Host::World): right after a named scene's OnLoad, its
// Persistent entities are snapshotted as authored and its saved changes
// applied on top; just before it unloads, what changed is captured back into
// the world state - so a scene remembers its changes after it's gone.
//
// Each swap is announced on the host's event bus (EngineEvents): unloaded,
// suspended, loaded, resumed - delivered at the next flush, after the swap.
// Unnamed scenes (Application's default) aren't announced.
//
// Loading a save replaces the whole stack in one swap (Restore): the old
// scenes unload, the save's state goes back in, then every saved scene loads
// bottom first - the ones under the top suspended at once.
//
// Scene stack: Push loads a scene on top and suspends the one under it
// (OnSuspend; it isn't updated or drawn and its sounds stop, but its state
// is kept); Pop unloads the top scene and resumes the one below (OnResume).
// Use it for a scene you return from with the world as you left it — a
// battle over the overworld. Switch replaces the whole stack. Pushes, pops
// and switches all fade the same way, and a later request replaces a queued
// one. Only the top scene is drawn: one frame has one camera, so a scene
// showing through another needs offscreen render targets (Phase 34). Overlays
// that keep the world visible (inventory, pause) belong on the UI stack.
// ---------------------------------------------------------------------------
class SceneManager
{
public:
    using Factory = std::function<std::unique_ptr<Scene>()>;

    // Services the manager hands to scenes. Application wires these; tests
    // leave them empty and drive the state machine headless.
    struct Host
    {
        Application*          App   = nullptr; // Scene::GetApp, texture release
        const InputManager*   Input = nullptr; // Scene::SetInput on every new scene
        std::function<void()> Quiesce;         // RenderThread::WaitIdle before a swap
        TextureManager*       Textures = nullptr; // images decoded ahead are staged here before OnLoad
        bool                  PrepareOnThread = false; // OnPrepare on a loader thread; off = inline (tests)
        MusicPlayer*          Music = nullptr; // scene music (Scene::SetMusic); null = no music
        EventBus*             Events = nullptr; // Scene::Subscribe; null = scenes can't subscribe

        // Saved scene state (docs/saves.md): every named scene's changes kept
        // in World across unloads. All three set, or scene state is off.
        WorldState*                                         World    = nullptr;
        const SceneRegistry*                                Registry = nullptr;
        std::function<std::unique_ptr<SceneAssets>(Scene&)> Assets;
    };

    SceneManager() = default;
    ~SceneManager(); // waits for any loader thread still running

    SceneManager(const SceneManager&)            = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void SetHost(Host host) { _host = std::move(host); }

    void               Register(std::string name, Factory factory);
    [[nodiscard]] bool IsRegistered(std::string_view name) const;

    // Queue a switch to the scene registered as `name`, replacing every scene
    // on the stack. `entry` is handed to the new scene (Scene::GetEntryPoint).
    // Unknown names are logged and ignored.
    void Switch(std::string_view name, std::string entry = {}) { Switch(name, SceneChange{ .Entry = std::move(entry) }); }
    void Switch(std::string_view name, SceneChange change);

    // Queue loading `name` on top of the active scene, which is suspended
    // rather than unloaded.
    void Push(std::string_view name, std::string entry = {}) { Push(name, SceneChange{ .Entry = std::move(entry) }); }
    void Push(std::string_view name, SceneChange change);

    // Queue unloading the active scene and resuming the one below it. Ignored
    // (with a warning) when there's nothing below.
    void Pop();

    // Queue replacing the whole stack with `stack` (bottom first) - loading
    // a save. `between` runs once the old scenes have unloaded and before the
    // new ones load: where the save's state goes back in. Refused (false,
    // logged) if it's empty or names an unregistered scene. Like any change,
    // a later request replaces it before it swaps, and `between` never runs.
    bool Restore(std::vector<SceneStackEntry> stack, std::function<void()> between = {});

    // The named scenes on the stack, bottom first - what a save records.
    [[nodiscard]] std::vector<SceneStackEntry> GetStack() const;

    // Start loading `name` ahead (its OnPrepare, on the loader thread) without
    // switching; the active scene keeps running. A later Switch or Push to the
    // same name uses the prepared scene. One scene is prepared at a time, so
    // preloading or switching to another name discards it. Ignored while a
    // switch or push is queued.
    void Preload(std::string_view name);

    // Install a scene immediately, with no fade and no lifecycle hooks — for
    // Application's default unmanaged scene.
    void SetActive(std::unique_ptr<Scene> scene);

    // Advance the fade and perform a pending swap. Call once per fixed tick,
    // before the active scene updates. A swap can take a while (loading), so
    // Application resets its frame clock when ConsumeSwapped() reports one.
    void Update(f32 dt);

    // True once after each swap.
    [[nodiscard]] bool ConsumeSwapped();

    // Unload every scene, top first (OnUnload, release, destroy). Application
    // calls this at shutdown after the render thread has stopped.
    void Shutdown();

    // The scene on top of the stack (updated and drawn); null when empty.
    [[nodiscard]] Scene* GetActive() const { return _stack.empty() ? nullptr : _stack.back().get(); }

    // Capture every loaded scene's changes into the world state - what's on
    // the stack hasn't unloaded yet, so a save needs this first.
    void CaptureLive();

    // Number of scenes on the stack, the active one included.
    [[nodiscard]] size_t GetDepth() const { return _stack.size(); }

    // Fade overlay opacity: 0 = clear, 1 = fully black.
    [[nodiscard]] f32  GetFadeAlpha() const { return _alpha; }
    // A scene change is queued or under way (loading, fading or both).
    [[nodiscard]] bool IsTransitioning() const { return _phase != Phase::Idle || _hasPending; }

    // The screen is fading out or in. Application holds the player still
    // only then, so a WaitForLoad switch leaves them in control while it loads.
    [[nodiscard]] bool IsFading() const { return _phase != Phase::Idle; }

    // The queued scene's OnPrepare is still running.
    [[nodiscard]] bool IsLoading() const;

    // Held at black waiting for a load, and the change asked for a loading
    // screen. Application shows its loading screen while this holds.
    [[nodiscard]] bool WantsLoadingScreen() const;

    // How far the queued scene's load is, 0..1, for a loading screen's bar
    // (see ScenePreloader for the stages). Holds below 1 while OnLoad still
    // has to run; 1 when nothing is loading.
    [[nodiscard]] f32 GetLoadProgress() const;

    f32 FadeOutTime = 0.3f; // seconds; 0 = cut to black
    f32 FadeInTime  = 0.3f; // seconds; 0 = cut in
    f32 MusicFadeTime = 0.6f; // seconds the music crossfades over, from the swap

    // Debug: stretch each scene's image decoding by this many seconds, to try
    // out a slow load (the splash, fades and loading bar keep moving over it).
    f32 PrepareDelay = 0.0f;

private:
    enum class Phase : u8
    {
        Idle,
        FadingOut,
        FadingIn,
    };

    enum class Request : u8
    {
        Switch,
        Push,
        Pop,
        Restore,
    };

    // A scene being loaded ahead: created on the main thread, OnPrepare run
    // on the loader thread (Done), then taken by the swap. Heap-allocated so
    // the loader thread's pointer to it stays valid.
    struct Prepared
    {
        std::string                     Name;
        std::unique_ptr<Scene>          Instance;
        std::unique_ptr<ScenePreloader> Preload;
        f32                             Seconds = 0.0f; // OnPrepare's duration, set by the loader
        std::future<void>               Done;           // invalid when it ran inline; destroyed first, so it waits
    };

    void Queue(Request kind, std::string_view name, SceneChange change);
    [[nodiscard]] bool WaitsToStart() const; // a WaitForLoad change whose scene is still loading
    void StartPrepare(std::string_view name);
    void Abandon(); // drop the prepared scene; one still loading waits in _abandoned
    void ReapAbandoned();
    [[nodiscard]] bool IsReady() const; // the queued request can swap now
    [[nodiscard]] std::unique_ptr<Scene> TakePrepared();
    void StartPending();
    void Swap();
    void SwapReplace();
    void SwapPush();
    void SwapPop();
    void SwapRestore();
    void LoadSuspended(const SceneStackEntry& entry); // a restored scene under the top
    void RestoreMusic(Scene* oldTop, bool ownerLeft);
    void UnloadAll();
    void               Unload(Scene& scene);     // capture its state, OnUnload, then its subscriptions end
    void               BeginState(Scene& scene); // after OnLoad: snapshot, apply its saved changes, track tiles
    void               CaptureState(Scene& scene);
    [[nodiscard]] bool TracksState(const Scene& scene) const;
    void        Announce(EventId id, const Scene& scene) const; // an engine scene event, if named
    // `to` became active in place of `from`; ownerLeft = a scene that owned
    // music went away (unloaded, not just covered by a push).
    void HandOverMusic(Scene* from, Scene& to, bool ownerLeft);

    [[nodiscard]] std::unique_ptr<Scene> Create(const std::string& name); // named and wired to the host

    Host                                     _host;
    std::unordered_map<std::string, Factory> _factories;
    std::vector<std::unique_ptr<Scene>>      _stack; // bottom first; back() is active

    std::unique_ptr<Prepared>              _prepared;  // the next scene, loading or loaded ahead
    std::vector<std::unique_ptr<Prepared>> _abandoned; // replaced while still loading

    Request     _pendingKind = Request::Switch;
    std::string _pendingName;
    SceneChange _pendingChange;
    bool        _hasPending = false;

    std::vector<SceneStackEntry> _pendingStack;   // Restore: every scene, bottom first
    std::function<void()>        _pendingBetween; // Restore: run between unload and load
    bool        _swapped    = false;

    Phase _phase = Phase::Idle;
    f32   _alpha = 0.0f;
};

} // namespace Arcbit
