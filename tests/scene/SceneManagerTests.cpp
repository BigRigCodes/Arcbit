// SceneManager driven headless: no Application host, so scenes never touch
// the GPU. Covers the switch state machine (fades, retargeting, reversal) and
// the lifecycle order scenes see.

#include <arcbit/scene/SceneManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Arcbit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr f32 Dt = 1.0f / 60.0f;

// Records every lifecycle call as "<event>:<name>[@entry]".
class LogScene : public Scene
{
public:
    explicit LogScene(std::vector<std::string>& log) : _log(log) {}

    void OnLoad() override
    {
        _log.push_back("load:" + GetName() + (GetEntryPoint().empty() ? "" : "@" + GetEntryPoint()));
    }
    void OnUnload() override { _log.push_back("unload:" + GetName()); }
    void OnSuspend() override { _log.push_back("suspend:" + GetName()); }
    void OnResume() override { _log.push_back("resume:" + GetName()); }
    void OnUpdate(f32) override { _log.push_back("update:" + GetName()); }

    int Counter = 0; // stand-in for scene state that must survive a suspend

private:
    std::vector<std::string>& _log;
};

struct Fixture
{
    std::vector<std::string> log;
    SceneManager             scenes;

    Fixture()
    {
        scenes.FadeOutTime = 0.25f; // 15 ticks
        scenes.FadeInTime  = 0.25f;
        for (const char* name : { "A", "B", "C" })
            scenes.Register(name, [this] { return std::make_unique<LogScene>(log); });
    }

    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) scenes.Update(Dt);
    }

    // Switch and run until the transition finishes.
    void SwitchAndSettle(const char* name)
    {
        scenes.Switch(name);
        for (int i = 0; i < 600 && scenes.IsTransitioning(); ++i) Tick();
        log.clear();
    }

    [[nodiscard]] std::string ActiveName() const
    {
        return scenes.GetActive() ? scenes.GetActive()->GetName() : "<none>";
    }
};
} // namespace

TEST_CASE("The first switch loads at once behind black, then fades in", "[scene]")
{
    Fixture f;
    f.scenes.SetActive(std::make_unique<Scene>()); // Application's default, unnamed
    f.scenes.Switch("A", "spawn");
    CHECK(f.scenes.IsTransitioning());

    f.Tick();
    CHECK(f.ActiveName() == "A");
    CHECK(f.log == std::vector<std::string>{ "load:A@spawn" });
    CHECK(f.scenes.GetFadeAlpha() == 1.0f);
    CHECK(f.scenes.ConsumeSwapped());
    CHECK_FALSE(f.scenes.ConsumeSwapped()); // reported once

    f.Tick(15);
    CHECK_THAT(f.scenes.GetFadeAlpha(), WithinAbs(0.0f, 1e-4f));
    f.Tick();
    CHECK_FALSE(f.scenes.IsTransitioning());
}

TEST_CASE("Switching between scenes fades out, swaps, then fades in", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("B", "door");
    f.Tick(); // Idle → FadingOut
    f.Tick(7);
    CHECK(f.ActiveName() == "A"); // still fading out
    CHECK(f.scenes.GetFadeAlpha() > 0.3f);
    CHECK(f.scenes.GetFadeAlpha() < 0.6f);

    f.Tick(8); // alpha reaches 1 → swap
    CHECK(f.ActiveName() == "B");
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:B@door" }); // old unloads before new loads
    CHECK(f.scenes.GetFadeAlpha() == 1.0f);

    f.Tick(16);
    CHECK(f.scenes.GetFadeAlpha() == 0.0f);
    CHECK_FALSE(f.scenes.IsTransitioning());
}

TEST_CASE("A switch during the fade-out retargets it", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("B");
    f.Tick(5);
    f.scenes.Switch("C");
    f.Tick(30);
    CHECK(f.ActiveName() == "C");
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:C" }); // B was never built
}

TEST_CASE("A switch during the fade-in reverses from the current alpha", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Switch("B");
    f.Tick(16);
    REQUIRE(f.ActiveName() == "B");
    f.Tick(5); // partway back in
    const f32 partial = f.scenes.GetFadeAlpha();
    REQUIRE(partial < 1.0f);

    f.scenes.Switch("C");
    f.Tick(); // FadingIn → FadingOut, no step yet
    CHECK(f.scenes.GetFadeAlpha() == partial);
    f.Tick();
    CHECK(f.scenes.GetFadeAlpha() > partial); // darkening again, not jumping to black

    f.Tick(30);
    CHECK(f.ActiveName() == "C");
}

TEST_CASE("Zero fade times cut instantly", "[scene]")
{
    Fixture f;
    f.scenes.FadeOutTime = 0.0f;
    f.scenes.FadeInTime  = 0.0f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("B");
    f.Tick(); // Idle → FadingOut
    f.Tick(); // cut to black and swap
    CHECK(f.ActiveName() == "B");
    f.Tick();
    CHECK(f.scenes.GetFadeAlpha() == 0.0f);
    CHECK_FALSE(f.scenes.IsTransitioning());
}

TEST_CASE("Unknown scene names are ignored", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Switch("Nowhere");
    CHECK_FALSE(f.scenes.IsTransitioning());
    f.Tick(30);
    CHECK(f.ActiveName() == "A");
}

TEST_CASE("Shutdown unloads the active scene", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Shutdown();
    CHECK(f.scenes.GetActive() == nullptr);
    CHECK(f.log == std::vector<std::string>{ "unload:A" });
}

TEST_CASE("Scene::Update runs OnUpdate before the scene's systems", "[scene]")
{
    std::vector<std::string> log;
    LogScene                 scene(log);
    scene.GetWorld().RegisterSystem("Probe", [&](Scene&, f32) { log.emplace_back("system"); });

    scene.Update(Dt);
    CHECK(log == std::vector<std::string>{ "update:", "system" });
}

// ---------------------------------------------------------------------------
// Scene stack
// ---------------------------------------------------------------------------

TEST_CASE("Push suspends the active scene instead of unloading it", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    static_cast<LogScene*>(f.scenes.GetActive())->Counter = 42;

    f.scenes.Push("B", "arena");
    f.Tick(40);
    CHECK(f.ActiveName() == "B");
    CHECK(f.scenes.GetDepth() == 2);
    CHECK(f.log == std::vector<std::string>{ "suspend:A", "load:B@arena" }); // A was never unloaded
}

TEST_CASE("Pop unloads the top scene and resumes the one below with its state", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    auto* a    = static_cast<LogScene*>(f.scenes.GetActive());
    a->Counter = 7;
    f.scenes.Push("B");
    f.Tick(40);
    f.log.clear();

    f.scenes.Pop();
    f.Tick(40);
    CHECK(f.scenes.GetActive() == a); // the same scene object, not a reload
    CHECK(a->Counter == 7);
    CHECK(f.scenes.GetDepth() == 1);
    CHECK(f.log == std::vector<std::string>{ "unload:B", "resume:A" });
}

TEST_CASE("Only the top of the stack updates", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Push("B");
    f.Tick(40);
    f.log.clear();

    f.scenes.GetActive()->Update(Dt); // what Application does each tick
    CHECK(f.log == std::vector<std::string>{ "update:B" });
}

TEST_CASE("Switch replaces the whole stack, top first", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Push("B");
    f.Tick(40);
    f.log.clear();

    f.scenes.Switch("C");
    f.Tick(40);
    CHECK(f.scenes.GetDepth() == 1);
    CHECK(f.ActiveName() == "C");
    CHECK(f.log == std::vector<std::string>{ "unload:B", "unload:A", "load:C" });
}

TEST_CASE("Pop with nothing below is ignored", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Pop();
    CHECK_FALSE(f.scenes.IsTransitioning());
    f.Tick(40);
    CHECK(f.ActiveName() == "A");
    CHECK(f.log.empty());
}

TEST_CASE("Shutdown unloads every scene on the stack, top first", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    f.scenes.Push("B");
    f.Tick(40);
    f.log.clear();

    f.scenes.Shutdown();
    CHECK(f.scenes.GetDepth() == 0);
    CHECK(f.log == std::vector<std::string>{ "unload:B", "unload:A" });
}

// ---------------------------------------------------------------------------
// Loading ahead on the loader thread
// ---------------------------------------------------------------------------

namespace
{
// Counts OnPrepare calls (on the loader thread). With a gate, OnPrepare
// blocks until the gate opens: a slow load.
class SlowScene : public Scene
{
public:
    SlowScene(std::vector<std::string>& log, std::atomic<int>& prepares, const std::atomic<bool>* gate) :
        _log(log), _prepares(prepares), _gate(gate)
    {}

    void OnPrepare(ScenePreloader&) override
    {
        ++_prepares;
        while (_gate && !_gate->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void OnLoad() override { _log.push_back("load:" + GetName()); }
    void OnUnload() override { _log.push_back("unload:" + GetName()); }

private:
    std::vector<std::string>& _log;
    std::atomic<int>&         _prepares;
    const std::atomic<bool>*  _gate;
};

struct AsyncFixture
{
    std::vector<std::string> log;
    std::atomic<int>         prepares{ 0 };
    std::atomic<bool>        gate{ false };
    SceneManager             scenes; // declared last: destroyed first, waiting for its loader threads

    AsyncFixture()
    {
        scenes.SetHost({ .PrepareOnThread = true });
        scenes.FadeOutTime = 0.25f;
        scenes.FadeInTime  = 0.25f;
        for (const char* name : { "A", "C" })
            scenes.Register(name, [this] { return std::make_unique<SlowScene>(log, prepares, nullptr); });
        scenes.Register("Slow", [this] { return std::make_unique<SlowScene>(log, prepares, &gate); });
    }
    ~AsyncFixture() { gate = true; } // release any loader still waiting, before `scenes` joins it

    void Tick(const int n = 1)
    {
        for (int i = 0; i < n; ++i) scenes.Update(Dt);
    }

    // Tick until `done` holds, giving the loader thread time; false on timeout.
    template<typename Pred>
    bool TickUntil(Pred done)
    {
        for (int i = 0; i < 5000 && !done(); ++i) {
            Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }

    void SwitchAndSettle(const char* name)
    {
        scenes.Switch(name);
        REQUIRE(TickUntil([this] { return !scenes.IsTransitioning(); }));
        log.clear();
    }

    [[nodiscard]] std::string ActiveName() const
    {
        return scenes.GetActive() ? scenes.GetActive()->GetName() : "<none>";
    }
};
} // namespace

TEST_CASE("A slow load holds the swap at black while the old scene stays active", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("Slow");
    f.Tick(30); // well past the 15-tick fade-out
    CHECK(f.scenes.GetFadeAlpha() == 1.0f);
    CHECK(f.scenes.IsLoading());
    CHECK(f.ActiveName() == "A");
    CHECK(f.log.empty());

    f.gate = true;
    REQUIRE(f.TickUntil([&] { return f.ActiveName() == "Slow"; }));
    CHECK_FALSE(f.scenes.IsLoading());
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:Slow" });
}

TEST_CASE("A switch away from a slow load doesn't wait for it", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("Slow");
    f.Tick(5);
    f.scenes.Switch("C");
    REQUIRE(f.TickUntil([&] { return f.ActiveName() == "C"; })); // the gate is still shut
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:C" });

    f.gate = true; // the abandoned load finishes and is dropped, never loaded
    f.TickUntil([&] { return !f.scenes.IsTransitioning(); });
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:C" });
}

TEST_CASE("Preload prepares a scene once and the switch to it uses it", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");
    CHECK(f.prepares == 1);

    f.scenes.Preload("C");
    f.scenes.Preload("C"); // already preparing: not started again
    REQUIRE(f.TickUntil([&] { return f.prepares == 2; }));
    CHECK(f.ActiveName() == "A"); // preloading doesn't switch

    f.SwitchAndSettle("C");
    CHECK(f.ActiveName() == "C");
    CHECK(f.prepares == 2);
}

TEST_CASE("Preload is ignored while a scene change is queued", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("C");
    f.scenes.Preload("Slow"); // would discard C's prepare
    REQUIRE(f.TickUntil([&] { return !f.scenes.IsTransitioning(); }));
    CHECK(f.ActiveName() == "C");
    CHECK(f.prepares == 2); // A and C only
}
