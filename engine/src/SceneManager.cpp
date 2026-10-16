#include <arcbit/scene/SceneManager.h>

#include <arcbit/core/Log.h>

#include <algorithm>

namespace Arcbit {

namespace
{
    // Step `alpha` toward `target` at 1/duration per second; a zero duration
    // jumps straight there.
    f32 StepToward(const f32 alpha, const f32 target, const f32 duration, const f32 dt)
    {
        if (duration <= 0.0f) return target;
        const f32 step = dt / duration;
        return target > alpha ? std::min(alpha + step, target) : std::max(alpha - step, target);
    }
} // namespace

void SceneManager::Register(std::string name, Factory factory)
{
    _factories.insert_or_assign(std::move(name), std::move(factory));
}

bool SceneManager::IsRegistered(const std::string_view name) const
{
    return _factories.contains(std::string(name));
}

void SceneManager::Switch(const std::string_view name, std::string entry)
{
    if (!IsRegistered(name)) {
        LOG_WARN(Engine, "SceneManager: no scene registered as '{}'", name);
        return;
    }
    _pendingName  = std::string(name);
    _pendingEntry = std::move(entry);
    _hasPending   = true;
}

void SceneManager::SetActive(std::unique_ptr<Scene> scene)
{
    if (scene) scene->SetInput(_host.Input);
    _active = std::move(scene);
}

void SceneManager::Update(const f32 dt)
{
    switch (_phase) {
        case Phase::Idle:
            if (_hasPending) StartPending();
            return;

        case Phase::FadingOut:
            _alpha = StepToward(_alpha, 1.0f, FadeOutTime, dt);
            if (_alpha >= 1.0f) Swap();
            return;

        case Phase::FadingIn:
            if (_hasPending) {
                _phase = Phase::FadingOut; // reverse from the current alpha
                return;
            }
            _alpha = StepToward(_alpha, 0.0f, FadeInTime, dt);
            if (_alpha <= 0.0f) _phase = Phase::Idle;
            return;
    }
}

bool SceneManager::ConsumeSwapped()
{
    const bool swapped = _swapped;
    _swapped           = false;
    return swapped;
}

void SceneManager::Shutdown()
{
    if (_active) Unload(std::move(_active));
    _phase      = Phase::Idle;
    _alpha      = 0.0f;
    _hasPending = false;
}

// Begin a queued switch. Leaving an unnamed scene (or none) has nothing worth
// fading out, so it swaps at once behind a black screen.
void SceneManager::StartPending()
{
    if (!_active || _active->GetName().empty()) {
        _alpha = 1.0f;
        Swap();
        return;
    }
    _phase = Phase::FadingOut;
}

void SceneManager::Swap()
{
    std::unique_ptr<Scene> next = _factories.at(_pendingName)();
    next->_name                 = _pendingName;
    next->_entryPoint           = std::move(_pendingEntry);
    next->_app                  = _host.App;
    next->SetInput(_host.Input);
    _hasPending = false;

    if (_host.Quiesce) _host.Quiesce(); // nothing may render either scene during the swap

    std::unique_ptr<Scene> old = std::move(_active);
    if (old) old->OnUnload();
    _active = std::move(next);
    _active->OnLoad();
    if (old) {
        old->ReleaseResources(); // after OnLoad, so textures both scenes use stay loaded
        old.reset();
    }

    LOG_INFO(Engine, "Scene '{}' loaded", _active->GetName());
    _phase   = Phase::FadingIn;
    _swapped = true;
}

void SceneManager::Unload(std::unique_ptr<Scene> scene)
{
    scene->OnUnload();
    scene->ReleaseResources();
}

} // namespace Arcbit
