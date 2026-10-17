#include <arcbit/scene/SceneManager.h>

#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>
#include <arcbit/scene/ScenePreloader.h>

#include <algorithm>
#include <chrono>
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

    using Clock = std::chrono::steady_clock;

    f32 SecondsSince(const Clock::time_point start)
    {
        return std::chrono::duration<f32>(Clock::now() - start).count();
    }
} // namespace

SceneManager::~SceneManager()
{
    // Prepared::Done waits for its loader thread, before the scene it uses goes.
    _prepared.reset();
    _abandoned.clear();
}

void SceneManager::Register(std::string name, Factory factory)
{
    _factories.insert_or_assign(std::move(name), std::move(factory));
}

bool SceneManager::IsRegistered(const std::string_view name) const
{
    return _factories.contains(std::string(name));
}

void SceneManager::Switch(const std::string_view name, SceneChange change)
{
    Queue(Request::Switch, name, std::move(change));
}

void SceneManager::Push(const std::string_view name, SceneChange change)
{
    Queue(Request::Push, name, std::move(change));
}

void SceneManager::Pop()
{
    if (_stack.size() < 2) {
        LOG_WARN(Engine, "SceneManager: Pop with no scene below the active one - ignored");
        return;
    }
    _pendingKind = Request::Pop;
    _pendingName.clear();
    _pendingChange = {};
    _hasPending = true;
}

void SceneManager::Preload(const std::string_view name)
{
    if (!IsRegistered(name)) {
        LOG_WARN(Engine, "SceneManager: no scene registered as '{}'", name);
        return;
    }
    if (_hasPending && _pendingKind != Request::Pop) {
        LOG_WARN(Engine, "SceneManager: Preload('{}') while a scene change is queued - ignored", name);
        return;
    }
    if (!_prepared || _prepared->Name != name) StartPrepare(name);
}

void SceneManager::Queue(const Request kind, const std::string_view name, SceneChange change)
{
    if (!IsRegistered(name)) {
        LOG_WARN(Engine, "SceneManager: no scene registered as '{}'", name);
        return;
    }
    if (!_prepared || _prepared->Name != name) StartPrepare(name);
    _pendingKind  = kind;
    _pendingName  = std::string(name);
    _pendingChange = std::move(change);
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
    ReapAbandoned();
    switch (_phase) {
        case Phase::Idle:
            if (_hasPending) StartPending();
            return;

        case Phase::FadingOut:
            _alpha = StepToward(_alpha, 1.0f, FadeOutTime, dt);
            if (_alpha >= 1.0f && IsReady()) Swap(); // else held at black while loading
            return;

        case Phase::FadingIn:
            if (_hasPending && !WaitsToStart()) {
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

bool SceneManager::IsLoading() const
{
    return _hasPending && !IsReady();
}

bool SceneManager::WantsLoadingScreen() const
{
    return IsLoading() && _phase == Phase::FadingOut && _alpha >= 1.0f && _pendingChange.ShowLoadingScreen;
}

f32 SceneManager::GetLoadProgress() const
{
    if (!_hasPending || _pendingKind == Request::Pop || !_prepared || _prepared->Name != _pendingName) return 1.0f;
    if (IsReady()) return 1.0f - _prepared->Preload->GetBuildShare(); // only the main-thread build is left
    return _prepared->Preload->GetProgress();
}

bool SceneManager::WaitsToStart() const
{
    return _pendingChange.WaitForLoad && !IsReady();
}

void SceneManager::Shutdown()
{
    _prepared.reset();
    _abandoned.clear();
    UnloadAll();
    _phase      = Phase::Idle;
    _alpha      = 0.0f;
    _hasPending = false;
}

// Begin a queued request. Leaving an unnamed scene (or none) has nothing
// worth fading out, so it cuts to black and swaps as soon as the next scene
// is prepared. A WaitForLoad change keeps the active scene running (Idle)
// until its scene is ready.
void SceneManager::StartPending()
{
    const Scene* active = GetActive();
    if (active && !active->GetName().empty()) {
        if (!WaitsToStart()) _phase = Phase::FadingOut;
        return;
    }
    _phase = Phase::FadingOut;
    _alpha = 1.0f;
    if (IsReady()) Swap();
}

std::unique_ptr<Scene> SceneManager::Create(const std::string& name)
{
    std::unique_ptr<Scene> scene = _factories.at(name)();
    scene->_name                 = name;
    scene->_app                  = _host.App;
    scene->SetInput(_host.Input);
    return scene;
}

// ---------------------------------------------------------------------------
// Loading ahead
// ---------------------------------------------------------------------------

// Create the scene now and run its OnPrepare - on the loader thread when the
// host asks for one. The preloader skips textures already on the GPU.
void SceneManager::StartPrepare(const std::string_view name)
{
    Abandon();
    auto prepared      = std::make_unique<Prepared>();
    prepared->Name     = std::string(name);
    prepared->Instance = Create(prepared->Name);
    prepared->Preload  = std::make_unique<ScenePreloader>(
        _host.Textures ? _host.Textures->GetLoadedPaths() : std::unordered_set<std::string>{});

    const auto run = [p = prepared.get(), delay = PrepareDelay] {
        const Clock::time_point start = Clock::now();
        try {
            p->Instance->OnPrepare(*p->Preload);
        }
        catch (const std::exception& e) {
            LOG_ERROR(Engine, "Scene '{}': OnPrepare threw - {}", p->Name, e.what());
        }
        p->Preload->DecodeAll(delay); // the images OnPrepare named
        p->Seconds = SecondsSince(start);
    };
    if (_host.PrepareOnThread) prepared->Done = std::async(std::launch::async, run);
    else run();
    _prepared = std::move(prepared);
}

void SceneManager::Abandon()
{
    if (!_prepared) return;
    if (_prepared->Done.valid() && _prepared->Done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        _abandoned.push_back(std::move(_prepared));
    _prepared.reset();
}

void SceneManager::ReapAbandoned()
{
    std::erase_if(_abandoned, [](const std::unique_ptr<Prepared>& p) {
        return p->Done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
}

bool SceneManager::IsReady() const
{
    if (_pendingKind == Request::Pop || !_prepared || !_prepared->Done.valid()) return true;
    return _prepared->Done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// The prepared scene for the queued request, its decoded images staged for
// OnLoad. Falls back to a fresh, unprepared scene if there is none.
std::unique_ptr<Scene> SceneManager::TakePrepared()
{
    std::unique_ptr<Scene> scene;
    if (_prepared && _prepared->Name == _pendingName) {
        const std::unique_ptr<Prepared> prepared = std::move(_prepared);
        if (prepared->Done.valid()) prepared->Done.get();
        LOG_DEBUG(Engine, "Scene '{}' prepared in {:.1f} ms ({} images decoded ahead)", prepared->Name,
                  prepared->Seconds * 1000.0f, prepared->Preload->GetDecodedCount());
        if (_host.Textures) _host.Textures->Stage(prepared->Preload->TakeImages());
        scene = std::move(prepared->Instance);
    }
    else {
        scene = Create(_pendingName);
    }
    scene->_entryPoint = std::move(_pendingChange.Entry);
    return scene;
}

void SceneManager::Swap()
{
    const Clock::time_point start = Clock::now();
    _hasPending                   = false;
    if (_host.Quiesce) _host.Quiesce(); // nothing may render any scene during the swap

    switch (_pendingKind) {
        case Request::Switch: SwapReplace(); break;
        case Request::Push: SwapPush(); break;
        case Request::Pop: SwapPop(); break;
    }
    if (_host.Textures) {
        if (const size_t unused = _host.Textures->ClearStaged())
            LOG_DEBUG(Engine, "SceneManager: {} image(s) decoded ahead were never loaded", unused);
    }
    LOG_DEBUG(Engine, "SceneManager: swap took {:.1f} ms", SecondsSince(start) * 1000.0f);
    _phase   = Phase::FadingIn;
    _swapped = true;
}

// Switch: unload the whole stack (top first), load the new scene, then
// release the old scenes' resources - after OnLoad, so textures they share
// with the new scene stay loaded.
void SceneManager::SwapReplace()
{
    std::unique_ptr<Scene> next = TakePrepared();
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
    std::unique_ptr<Scene> next = TakePrepared();
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
