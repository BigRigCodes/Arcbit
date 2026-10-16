#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/scene/Scene.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Arcbit {

class Application;
class InputManager;

// ---------------------------------------------------------------------------
// SceneManager — owns the active Scene and switches between registered ones.
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
    };

    void SetHost(Host host) { _host = std::move(host); }

    void               Register(std::string name, Factory factory);
    [[nodiscard]] bool IsRegistered(std::string_view name) const;

    // Queue a switch to the scene registered as `name`. `entry` is handed to
    // the new scene (Scene::GetEntryPoint). Unknown names are logged and
    // ignored.
    void Switch(std::string_view name, std::string entry = {});

    // Install a scene immediately, with no fade and no lifecycle hooks — for
    // Application's default unmanaged scene.
    void SetActive(std::unique_ptr<Scene> scene);

    // Advance the fade and perform a pending swap. Call once per fixed tick,
    // before the active scene updates. A swap can take a while (loading), so
    // Application resets its frame clock when ConsumeSwapped() reports one.
    void Update(f32 dt);

    // True once after each swap.
    [[nodiscard]] bool ConsumeSwapped();

    // Unload the active scene (OnUnload, release, destroy). Application calls
    // this at shutdown after the render thread has stopped.
    void Shutdown();

    [[nodiscard]] Scene* GetActive() const { return _active.get(); }

    // Fade overlay opacity: 0 = clear, 1 = fully black.
    [[nodiscard]] f32  GetFadeAlpha() const { return _alpha; }
    [[nodiscard]] bool IsTransitioning() const { return _phase != Phase::Idle || _hasPending; }

    f32 FadeOutTime = 0.3f; // seconds; 0 = cut to black
    f32 FadeInTime  = 0.3f; // seconds; 0 = cut in

private:
    enum class Phase : u8
    {
        Idle,
        FadingOut,
        FadingIn,
    };

    void StartPending();
    void Swap();
    void Unload(std::unique_ptr<Scene> scene);

    Host                                     _host;
    std::unordered_map<std::string, Factory> _factories;
    std::unique_ptr<Scene>                   _active;

    std::string _pendingName;
    std::string _pendingEntry;
    bool        _hasPending = false;
    bool        _swapped    = false;

    Phase _phase = Phase::Idle;
    f32   _alpha = 0.0f;
};

} // namespace Arcbit
