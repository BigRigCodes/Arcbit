#include <arcbit/scene/SceneManager.h>

#include <arcbit/core/Log.h>

#include <algorithm>
#include <ranges>

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
    Queue(Request::Switch, name, std::move(entry));
}

void SceneManager::Push(const std::string_view name, std::string entry)
{
    Queue(Request::Push, name, std::move(entry));
}

void SceneManager::Pop()
{
    if (_stack.size() < 2) {
        LOG_WARN(Engine, "SceneManager: Pop with no scene below the active one - ignored");
        return;
    }
    _pendingKind = Request::Pop;
    _pendingName.clear();
    _pendingEntry.clear();
    _hasPending = true;
}

void SceneManager::Queue(const Request kind, const std::string_view name, std::string entry)
{
    if (!IsRegistered(name)) {
        LOG_WARN(Engine, "SceneManager: no scene registered as '{}'", name);
        return;
    }
    _pendingKind  = kind;
    _pendingName  = std::string(name);
    _pendingEntry = std::move(entry);
    _hasPending   = true;
}

void SceneManager::SetActive(std::unique_ptr<Scene> scene)
{
    _stack.clear();
    if (!scene) return;
    scene->SetInput(_host.Input);
    _stack.push_back(std::move(scene));
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
    UnloadAll();
    _phase      = Phase::Idle;
    _alpha      = 0.0f;
    _hasPending = false;
}

// Begin a queued request. Leaving an unnamed scene (or none) has nothing
// worth fading out, so it swaps at once behind a black screen.
void SceneManager::StartPending()
{
    const Scene* active = GetActive();
    if (!active || active->GetName().empty()) {
        _alpha = 1.0f;
        Swap();
        return;
    }
    _phase = Phase::FadingOut;
}

std::unique_ptr<Scene> SceneManager::Create()
{
    std::unique_ptr<Scene> scene = _factories.at(_pendingName)();
    scene->_name                 = _pendingName;
    scene->_entryPoint           = std::move(_pendingEntry);
    scene->_app                  = _host.App;
    scene->SetInput(_host.Input);
    return scene;
}

void SceneManager::Swap()
{
    _hasPending = false;
    if (_host.Quiesce) _host.Quiesce(); // nothing may render any scene during the swap

    switch (_pendingKind) {
        case Request::Switch: SwapReplace(); break;
        case Request::Push: SwapPush(); break;
        case Request::Pop: SwapPop(); break;
    }
    _phase   = Phase::FadingIn;
    _swapped = true;
}

// Switch: unload the whole stack (top first), load the new scene, then
// release the old scenes' resources - after OnLoad, so textures they share
// with the new scene stay loaded.
void SceneManager::SwapReplace()
{
    std::unique_ptr<Scene> next = Create();
    for (const std::unique_ptr<Scene>& scene : _stack | std::views::reverse) scene->OnUnload();
    std::vector<std::unique_ptr<Scene>> old = std::move(_stack);

    _stack.clear();
    _stack.push_back(std::move(next));
    _stack.back()->OnLoad();
    LOG_INFO(Engine, "Scene '{}' loaded", _stack.back()->GetName());

    while (!old.empty()) {
        old.back()->ReleaseResources();
        old.pop_back();
    }
}

// Push: suspend the active scene in place (state intact, sounds stopped) and
// load the new one on top.
void SceneManager::SwapPush()
{
    std::unique_ptr<Scene> next = Create();
    if (!_stack.empty()) {
        _stack.back()->OnSuspend();
        _stack.back()->StopSounds();
    }
    _stack.push_back(std::move(next));
    _stack.back()->OnLoad();
    LOG_INFO(Engine, "Scene '{}' pushed ({} deep)", _stack.back()->GetName(), _stack.size());
}

// Pop: unload the top scene and resume the one under it. A Pop queued when
// the stack has since shrunk to one scene does nothing.
void SceneManager::SwapPop()
{
    if (_stack.size() < 2) return;
    std::unique_ptr<Scene> top = std::move(_stack.back());
    _stack.pop_back();

    top->OnUnload();
    _stack.back()->OnResume();
    top->ReleaseResources();
    LOG_INFO(Engine, "Scene '{}' resumed", _stack.back()->GetName());
}

void SceneManager::UnloadAll()
{
    while (!_stack.empty()) {
        _stack.back()->OnUnload();
        _stack.back()->ReleaseResources();
        _stack.pop_back();
    }
}

} // namespace Arcbit
