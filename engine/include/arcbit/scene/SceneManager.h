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
class InputManager;
class TextureManager;

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
// OnPrepare (file reads, image decodes) on a loader thread while the fade-out
// plays. The swap itself - OnLoad, which then only uploads and builds
// entities - waits at black until the prepare is done, so a slow load holds
// the screen black instead of freezing a fade or the splash. Preload(name)
// starts that work early, with the current scene still running, so the
// switch later finds it done.
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
    void Switch(std::string_view name, std::string entry = {});

    // Queue loading `name` on top of the active scene, which is suspended
    // rather than unloaded.
    void Push(std::string_view name, std::string entry = {});

    // Queue unloading the active scene and resuming the one below it. Ignored
    // (with a warning) when there's nothing below.
    void Pop();

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

    // Number of scenes on the stack, the active one included.
    [[nodiscard]] size_t GetDepth() const { return _stack.size(); }

    // Fade overlay opacity: 0 = clear, 1 = fully black.
    [[nodiscard]] f32  GetFadeAlpha() const { return _alpha; }
    [[nodiscard]] bool IsTransitioning() const { return _phase != Phase::Idle || _hasPending; }

    // True while a swap is held at black, waiting for the next scene's
    // OnPrepare to finish.
    [[nodiscard]] bool IsLoading() const;

    f32 FadeOutTime = 0.3f; // seconds; 0 = cut to black
    f32 FadeInTime  = 0.3f; // seconds; 0 = cut in

    // Debug: the loader thread sleeps this long after each OnPrepare, to try
    // out a slow load (the splash and fades should keep animating over it).
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

    void Queue(Request kind, std::string_view name, std::string entry);
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
    void UnloadAll();

    [[nodiscard]] std::unique_ptr<Scene> Create(const std::string& name); // named and wired to the host

    Host                                     _host;
    std::unordered_map<std::string, Factory> _factories;
    std::vector<std::unique_ptr<Scene>>      _stack; // bottom first; back() is active

    std::unique_ptr<Prepared>              _prepared;  // the next scene, loading or loaded ahead
    std::vector<std::unique_ptr<Prepared>> _abandoned; // replaced while still loading

    Request     _pendingKind = Request::Switch;
    std::string _pendingName;
    std::string _pendingEntry;
    bool        _hasPending = false;
    bool        _swapped    = false;

    Phase _phase = Phase::Idle;
    f32   _alpha = 0.0f;
};

} // namespace Arcbit
