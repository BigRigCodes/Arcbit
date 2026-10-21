// SceneManager driven headless: no Application host, so scenes never touch
// the GPU. Covers the switch state machine (fades, retargeting, reversal) and
// the lifecycle order scenes see.

#include <arcbit/audio/MusicPlayer.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/save/SceneState.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
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

TEST_CASE("Restore rebuilds a saved stack, the scenes under the top suspended", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    REQUIRE(f.scenes.Restore({ { "B", "gate" }, { "C", "" } }, [&] { f.log.emplace_back("between"); }));
    f.Tick(40);

    CHECK(f.log == std::vector<std::string>{ "unload:A", "between", "load:B@gate", "suspend:B", "load:C" });
    CHECK(f.ActiveName() == "C");
    CHECK(f.scenes.GetStack() == std::vector<SceneStackEntry>{ { "B", "gate" }, { "C", "" } });

    f.log.clear();
    f.scenes.Pop(); // the restored scene below resumes like a pushed-over one
    f.Tick(40);
    CHECK(f.log == std::vector<std::string>{ "unload:C", "resume:B" });
}

TEST_CASE("Restore refuses a stack naming an unknown scene", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    CHECK_FALSE(f.scenes.Restore({ { "B", "" }, { "Nope", "" } }));
    CHECK_FALSE(f.scenes.Restore({}));
    CHECK_FALSE(f.scenes.IsTransitioning());
}

TEST_CASE("A later change replaces a queued restore before it runs", "[scene]")
{
    Fixture f;
    f.SwitchAndSettle("A");
    bool ran = false;
    REQUIRE(f.scenes.Restore({ { "B", "" } }, [&] { ran = true; }));
    f.Tick();
    f.scenes.Switch("C");
    f.Tick(40);
    CHECK_FALSE(ran);
    CHECK(f.ActiveName() == "C");
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

TEST_CASE("A fade-first change asks for the loading screen while held at black", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    CHECK(f.scenes.GetLoadProgress() == 1.0f); // nothing loading
    f.scenes.Switch("Slow");
    f.Tick(5);
    CHECK_FALSE(f.scenes.WantsLoadingScreen()); // still fading out
    f.Tick(25);
    CHECK(f.scenes.WantsLoadingScreen());
    CHECK(f.scenes.GetLoadProgress() < 1.0f);

    f.gate = true;
    REQUIRE(f.TickUntil([&] { return f.ActiveName() == "Slow"; }));
    CHECK_FALSE(f.scenes.WantsLoadingScreen());
}

TEST_CASE("ShowLoadingScreen = false keeps a long hold plain black", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("Slow", SceneChange{ .ShowLoadingScreen = false });
    f.Tick(30);
    CHECK(f.scenes.IsLoading());
    CHECK_FALSE(f.scenes.WantsLoadingScreen());
}

TEST_CASE("WaitForLoad keeps the current scene running until the next is ready", "[scene][async]")
{
    AsyncFixture f;
    f.SwitchAndSettle("A");

    f.scenes.Switch("Slow", SceneChange{ .WaitForLoad = true });
    f.Tick(30);
    CHECK(f.ActiveName() == "A");
    CHECK(f.scenes.GetFadeAlpha() == 0.0f); // no fade yet
    CHECK_FALSE(f.scenes.IsFading());       // so the player keeps control
    CHECK(f.scenes.IsLoading());
    CHECK(f.scenes.IsTransitioning());
    CHECK_FALSE(f.scenes.WantsLoadingScreen());

    f.gate = true;
    REQUIRE(f.TickUntil([&] { return f.scenes.GetFadeAlpha() > 0.0f; })); // ready: now it fades out
    CHECK(f.ActiveName() == "A");
    REQUIRE(f.TickUntil([&] { return !f.scenes.IsTransitioning(); }));
    CHECK(f.ActiveName() == "Slow");
    CHECK(f.log == std::vector<std::string>{ "unload:A", "load:Slow" });
}

// ---------------------------------------------------------------------------
// Scene music
// ---------------------------------------------------------------------------

namespace
{
// Records what the scene manager asks of the music, and behaves like the
// device: the same track carries on rather than restarting.
class FakeMusic : public MusicPlayer
{
public:
    std::vector<std::string> Calls;
    std::string              Current;
    f32                      Position = 0.0f;

    void Play(const std::string_view path, bool, f32, const f32 startSeconds, std::string_view) override
    {
        if (path == Current) return;
        Current = std::string(path);
        Calls.push_back(std::format("play:{}@{}", path, startSeconds));
    }
    void Stop(f32) override
    {
        Current.clear();
        Calls.push_back("stop");
    }
    std::string GetPath() const override { return Current; }
    f32         GetPosition() const override { return Position; }
};

// Sets `track` as its music in OnLoad; no track = never sets music.
class MusicScene : public Scene
{
public:
    explicit MusicScene(std::optional<std::string> track) : _track(std::move(track)) {}
    void OnLoad() override
    {
        if (_track) SetMusic(*_track);
    }

private:
    std::optional<std::string> _track;
};

struct MusicFixture
{
    FakeMusic    music;
    SceneManager scenes;

    MusicFixture()
    {
        scenes.SetHost({ .Music = &music });
        scenes.FadeOutTime = 0.0f;
        scenes.FadeInTime  = 0.0f;
        const auto add     = [this](const char* name, std::optional<std::string> track) {
            scenes.Register(name, [track] { return std::make_unique<MusicScene>(track); });
        };
        add("Town", "town.ogg");
        add("Field", "town.ogg"); // same track as Town
        add("Cave", "cave.ogg");
        add("Quiet", std::string{}); // owns music: silence
        add("Plain", std::nullopt);  // never sets music
    }

    void Settle()
    {
        for (int i = 0; i < 10; ++i) scenes.Update(Dt);
    }
    void SwitchTo(const char* name)
    {
        scenes.Switch(name);
        Settle();
    }
};
} // namespace

TEST_CASE("Each scene's own track plays when it becomes active", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.SwitchTo("Cave");
    f.SwitchTo("Quiet");
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0", "play:cave.ogg@0", "stop" });
}

TEST_CASE("Scenes sharing a track carry it on without restarting", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.SwitchTo("Field");
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0" });
}

TEST_CASE("Leaving a music-owning scene for one without music fades it out", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Plain");
    CHECK(f.music.Calls.empty()); // never set music: the music is left alone
    f.SwitchTo("Town");
    f.SwitchTo("Plain");
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0", "stop" });
}

TEST_CASE("A pushed scene's track takes over and the one below resumes where it was", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.music.Position = 12.5f; // the town track has played this far
    f.scenes.Push("Cave");
    f.Settle();
    f.scenes.Pop();
    f.Settle();
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0", "play:cave.ogg@0", "play:town.ogg@12.5" });
}

TEST_CASE("A pushed scene without music keeps the music below playing", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.scenes.Push("Plain");
    f.Settle();
    f.scenes.Pop();
    f.Settle();
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0" });
    CHECK(f.music.Current == "town.ogg");
}

TEST_CASE("Switching away from a stack stops a suspended scene's music", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.scenes.Push("Plain"); // town music carries on under it
    f.Settle();
    f.SwitchTo("Plain");    // the town scene is unloaded with the stack
    CHECK(f.music.Calls == std::vector<std::string>{ "play:town.ogg@0", "stop" });
}

TEST_CASE("SetMusic on the active scene changes track at once", "[scene][music]")
{
    MusicFixture f;
    f.SwitchTo("Town");
    f.scenes.GetActive()->SetMusic("boss.ogg"); // e.g. a boss fight starts
    CHECK(f.music.Current == "boss.ogg");
    f.scenes.Push("Plain");
    f.Settle();
    f.scenes.GetActive()->SetMusic("menu.ogg"); // the pushed scene is active now
    CHECK(f.music.Current == "menu.ogg");
}

TEST_CASE("A track changed while suspended starts from its beginning", "[scene][music]")
{
    // Regression (PR #4 review): the old track's saved position was kept and
    // the new track resumed partway through.
    MusicFixture f;
    f.SwitchTo("Town");
    Scene* town      = f.scenes.GetActive();
    f.music.Position = 12.5f;
    f.scenes.Push("Cave");
    f.Settle();
    town->SetMusic("night.ogg"); // e.g. night fell while the battle ran
    f.scenes.Pop();
    f.Settle();
    CHECK(f.music.Calls.back() == "play:night.ogg@0");
}

// ---------------------------------------------------------------------------
// Scene event subscriptions
// ---------------------------------------------------------------------------

namespace
{
constexpr EventId Ping = MakeEvent("test.ping");

// Counts Ping deliveries; subscribes in OnLoad. On unload, records how many
// it had seen, so a test can tell the subscription ended there.
class ListeningScene : public Scene
{
public:
    explicit ListeningScene(int& heard) : _heard(heard) {}
    void OnLoad() override
    {
        Subscribe(Ping, [this](const Event&) { ++_heard; });
    }

private:
    int& _heard;
};

struct EventFixture
{
    EventBus     events;
    int          heard = 0;
    SceneManager scenes;

    EventFixture()
    {
        scenes.SetHost({ .Events = &events });
        scenes.FadeOutTime = 0.0f;
        scenes.FadeInTime  = 0.0f;
        scenes.Register("Listener", [this] { return std::make_unique<ListeningScene>(heard); });
        scenes.Register("Other", [] { return std::make_unique<Scene>(); });
    }
    void Go(const char* name, const bool push = false)
    {
        if (push) scenes.Push(name);
        else scenes.Switch(name);
        for (int i = 0; i < 10; ++i) scenes.Update(Dt);
    }
    void Ping()
    {
        events.Publish(::Ping);
        events.Flush();
    }
};
} // namespace

TEST_CASE("A scene's subscriptions end when it unloads", "[scene][events]")
{
    EventFixture f;
    f.Go("Listener");
    f.Ping();
    CHECK(f.heard == 1);

    f.Go("Other");
    f.Ping();
    CHECK(f.heard == 1); // the listener is gone, and so is its handler
}

TEST_CASE("A suspended scene keeps its subscriptions until it's popped off", "[scene][events]")
{
    EventFixture f;
    f.Go("Other");
    f.Go("Listener", true);
    f.Go("Other", true); // the listener is suspended under it
    f.Ping();
    CHECK(f.heard == 1); // paused, not gone

    f.scenes.Pop(); // back to the listener
    for (int i = 0; i < 10; ++i) f.scenes.Update(Dt);
    f.scenes.Pop(); // the listener unloads
    for (int i = 0; i < 10; ++i) f.scenes.Update(Dt);
    f.Ping();
    CHECK(f.heard == 1);
}

TEST_CASE("Scene changes are announced as engine events", "[scene][events]")
{
    EventFixture             f;
    std::vector<std::string> log;
    Subscription all = f.events.SubscribeAll([&](const Event& e) {
        log.push_back(std::format("{}:{}", f.events.GetName(e.Id), e.Payload.Get<std::string>("name", "")));
    });
    const auto settle = [&] { for (int i = 0; i < 10; ++i) f.scenes.Update(Dt); f.events.Flush(); };

    f.scenes.Switch("Other", "gate");
    settle();
    f.scenes.Push("Listener");
    settle();
    f.scenes.Pop();
    settle();
    f.scenes.Switch("Listener");
    settle();

    CHECK(log == std::vector<std::string>{
        "engine.scene.loaded:Other",
        "engine.scene.suspended:Other", "engine.scene.loaded:Listener",
        "engine.scene.unloaded:Listener", "engine.scene.resumed:Other",
        "engine.scene.unloaded:Other", "engine.scene.loaded:Listener",
    });
}

TEST_CASE("A loaded scene's event carries its entry point", "[scene][events]")
{
    EventFixture f;
    std::string  entry;
    Subscription sub = f.events.Subscribe(EngineEvents::SceneLoaded,
                                          [&](const Event& e) { entry = e.Payload.Get<std::string>("entry", "?"); });
    f.Go("Other");
    f.scenes.Switch("Listener", "north_gate");
    for (int i = 0; i < 10; ++i) f.scenes.Update(Dt);
    f.events.Flush();
    CHECK(entry == "north_gate");
}

// ---------------------------------------------------------------------------
// Saved scene state
// ---------------------------------------------------------------------------

namespace
{
// A village with one persistent chest.
class ChestScene : public Scene
{
public:
    Entity Chest;

    void OnLoad() override
    {
        Chest                                    = GetWorld().CreateEntity();
        GetWorld().GetTransform(Chest)->Position = { 10, 0 };
        GetWorld().AddComponent<Persistent>(Chest, Persistent{ .Id = "chest" });
    }
};

class NoSceneAssets final : public SceneAssets
{
public:
    TextureHandle      LoadTexture(std::string_view) override { return {}; }
    std::string        TexturePath(TextureHandle) const override { return {}; }
    SamplerHandle      Sampler(std::string_view) override { return {}; }
    std::string        SamplerName(SamplerHandle) const override { return {}; }
    const SpriteSheet* LoadSpriteSheet(std::string_view) override { return nullptr; }
    std::string        SpriteSheetPath(const SpriteSheet*) const override { return {}; }
    std::string        ActionName(ActionID) const override { return {}; }
    bool               LoadTileMap(TileMap&, std::string_view) override { return true; }
};

struct StateFixture
{
    WorldState    world;
    SceneRegistry registry;
    SceneManager  scenes;

    StateFixture()
    {
        RegisterBuiltinComponents(registry);
        scenes.SetHost({ .World    = &world,
                         .Registry = &registry,
                         .Assets   = [](Scene&) { return std::make_unique<NoSceneAssets>(); } });
        scenes.FadeOutTime = 0.0f;
        scenes.FadeInTime  = 0.0f;
        scenes.Register("Village", [] { return std::make_unique<ChestScene>(); });
        scenes.Register("Cave", [] { return std::make_unique<Scene>(); });
    }
    void Go(const char* name, const bool push = false)
    {
        if (push) scenes.Push(name);
        else scenes.Switch(name);
        for (int i = 0; i < 10; ++i) scenes.Update(Dt);
    }
    ChestScene& Village() { return *static_cast<ChestScene*>(scenes.GetActive()); }
};
} // namespace

TEST_CASE("A scene remembers its changes after it unloads", "[scene][save]")
{
    StateFixture f;
    f.Go("Village");
    f.Village().GetWorld().GetTransform(f.Village().Chest)->Position = { 99, 0 }; // pushed aside

    f.Go("Cave"); // the village unloads: its change is kept
    REQUIRE(f.world.Scenes.contains("Village"));
    CHECK_FALSE(f.world.Scenes["Village"].Empty());

    f.Go("Village"); // rebuilt as authored, then the change applied
    CHECK(f.Village().GetWorld().GetTransform(f.Village().Chest)->Position == Vec2{ 99, 0 });
}

TEST_CASE("CaptureLive records scenes that haven't unloaded", "[scene][save]")
{
    StateFixture f;
    f.Go("Village");
    f.Village().GetWorld().DestroyEntity(f.Village().Chest);
    f.Go("Cave", true); // pushed over: the village is suspended, not unloaded
    CHECK_FALSE(f.world.Scenes.contains("Village"));

    f.scenes.CaptureLive(); // what a save does first
    REQUIRE(f.world.Scenes.contains("Village"));
    CHECK(f.world.Scenes["Village"].Destroyed == std::vector<std::string>{ "chest" });
}

TEST_CASE("A restore loads the state put back between unload and load", "[scene][save]")
{
    StateFixture f;
    f.Go("Village");
    f.Village().GetWorld().GetTransform(f.Village().Chest)->Position = { 99, 0 };
    f.scenes.CaptureLive();
    const WorldState saved = f.world; // the save

    f.Village().GetWorld().GetTransform(f.Village().Chest)->Position = { 5, 0 }; // played on, not saved
    REQUIRE(f.scenes.Restore({ { "Village", "" } }, [&] { f.world = saved; }));
    for (int i = 0; i < 10; ++i) f.scenes.Update(Dt);
    CHECK(f.Village().GetWorld().GetTransform(f.Village().Chest)->Position == Vec2{ 99, 0 });
}

TEST_CASE("Without a world state, scenes keep nothing", "[scene][save]")
{
    StateFixture f;
    f.scenes.SetHost({}); // scene state off
    f.Go("Village");
    f.Village().GetWorld().GetTransform(f.Village().Chest)->Position = { 99, 0 };
    f.Go("Cave");
    f.Go("Village");
    CHECK(f.Village().GetWorld().GetTransform(f.Village().Chest)->Position == Vec2{ 10, 0 });
}
