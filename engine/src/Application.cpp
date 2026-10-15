#include <arcbit/app/Application.h>
#include <arcbit/app/Window.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/physics/ContactTracker.h>
#include <arcbit/physics/DirectionArc.h>
#include <arcbit/physics/Narrowphase.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/physics/SpatialHash.h>
#include <arcbit/physics/Sweep.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/SplashScreen.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <format>

#include <chrono>
#include <thread>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Constructor — initializes all engine systems in dependency order.
// ---------------------------------------------------------------------------

Application::Application(const ApplicationConfig& config) : _config(config)
{
    Log::Init();
    LOG_INFO(Engine, "Arcbit starting");

    #ifdef ARCBIT_DEBUG
    // Cheap startup sanity checks on core data structures (ECS storage, physics).
    // Stripped from release builds along with all assertions.
    World::SelfTest();
    SpatialHash::SelfTest();
    PhysicsWorld::SelfTest();
    Narrowphase::SelfTest();
    Sweep::SelfTest();
    DirectionArcSelfTest();
    ContactTracker::SelfTest();
    #endif

    // Seed Settings with config defaults so that GraphicsSettings has sensible
    // values even when no settings file exists yet. ReadFile() will then
    // overwrite these with any persisted values.
    Settings::Graphics.ResolutionWidth  = config.Width;
    Settings::Graphics.ResolutionHeight = config.Height;
    Settings::Init(config.SettingsPath);
    AudioManager::Init();

    _window = std::make_unique<Window>(Window::Desc{
        .Title     = config.Title,
        .Width     = Settings::Graphics.ResolutionWidth,
        .Height    = Settings::Graphics.ResolutionHeight,
        .Resizable = config.Resizable,
    });

    DeviceDesc deviceDesc{};
    deviceDesc.NativeWindowHandle = _window->GetNativeHandle();
    deviceDesc.AppName            = config.Title;
    deviceDesc.AppVersion         = 1;
    #ifdef ARCBIT_DEBUG
    deviceDesc.EnableValidation = true;
    #endif
    _device = Arcbit_CreateDevice(deviceDesc);
    ARCBIT_ASSERT(_device != nullptr, "Failed to create render device");
    LOG_INFO(Render, "Render device ready");

    SwapchainDesc swapDesc{};
    swapDesc.NativeWindowHandle = _window->GetNativeHandle();
    swapDesc.Width              = _window->GetWidth();
    swapDesc.Height             = _window->GetHeight();
    swapDesc.VSync              = Settings::Graphics.VSync;
    _swapchain                  = _device->CreateSwapchain(swapDesc);
    ARCBIT_ASSERT(_swapchain.IsValid(), "Failed to create swapchain");

    _renderThread.Start(_device, _device->GetSwapchainColorFormat(_swapchain));

    _textures = std::make_unique<TextureManager>(*_device);
}

// ---------------------------------------------------------------------------
// Destructor — defined here so the compiler sees Window's complete type.
// ---------------------------------------------------------------------------

Application::~Application() = default;

// ---------------------------------------------------------------------------
// System accessors
// ---------------------------------------------------------------------------

RenderDevice& Application::GetDevice()
{
    ARCBIT_ASSERT(_device != nullptr, "GetDevice() called after shutdown");
    return *_device;
}

u32 Application::GetWindowWidth() const { return _window->GetWidth(); }
u32 Application::GetWindowHeight() const { return _window->GetHeight(); }

Scene& Application::GetScene()
{
    ARCBIT_ASSERT(_scene != nullptr, "GetScene() called before Run()");
    return *_scene;
}

void Application::ToggleFullscreen()
{
    _window->ToggleFullscreen();
    Settings::Graphics.Fullscreen = !Settings::Graphics.Fullscreen;
    Settings::MarkDirty();
}

Format Application::GetSwapchainFormat() const { return _device->GetSwapchainColorFormat(_swapchain); }

// ---------------------------------------------------------------------------
// Run — the main loop.
// ---------------------------------------------------------------------------

void Application::Run()
{
    // Register engine system actions with defaults before OnStart so that:
    //   a) settings can rebind them (LoadInputBindings runs after OnStart), and
    //   b) game code cannot accidentally stomp the same ActionID before they exist.
    _input.RegisterAction(ActionEngineFullscreen, "Engine_Fullscreen");
    _input.BindKey(ActionEngineFullscreen, Key::F11);

    // Create the scene before OnStart so game code can use GetScene() immediately.
    _scene = std::make_unique<Scene>();

    // Load the engine's built-in debug font before game code runs so subclasses
    // can use it in OnStart if they need to (e.g. preload additional glyphs).
    _debugFont.Load("assets/engine/fonts/Roboto-Regular.ttf", 24.0f, FontMode::SDF, *_device);

    // Init UI manager after the render thread is running so white-texture handles are valid.
    // Registers UI navigation actions (UI_Confirm, UI_FocusNext, UI_FocusPrev) before OnStart
    // so games can rebind them via Settings::LoadInputBindings after OnStart.
    _ui.Init(_renderThread, _debugFont, _input);

    // Load engine locale strings before OnStart so built-in screens display correctly.
    if (!Loc::Load("assets/engine/locale/en.json"))
        LOG_WARN(Engine, "Failed to load locale strings");

    // Give the game a chance to register actions, create GPU resources, etc.
    // Input bindings are loaded from settings AFTER OnStart so that default
    // bindings set up in OnStart are overwritten by any user-saved bindings.
    OnStart();
    Settings::LoadInputBindings(_input);

    // Push the engine-branded splash screen on top of any screens the game
    // pushed in OnStart. It blocks input until complete, then pops itself.
    SamplerHandle splashSampler;
    {
        TextureHandle splashTex = _textures->Load("assets/engine/ui/ArcbitA_withbackground.png");
        if (splashTex.IsValid()) {
            splashSampler = _device->CreateSampler(SamplerDesc{
                .MinFilter = Filter::Linear,
                .MagFilter = Filter::Linear,
                .AddressU  = AddressMode::ClampToEdge,
                .AddressV  = AddressMode::ClampToEdge,
                .DebugName = "EngineSplashSampler",
            });
            auto splash = std::make_unique<SplashScreen>();
            splash->Entries.push_back({splashTex, splashSampler, 8.0f, 0.5f, 0.5f});
            splash->OnComplete = [this] { _ui.Pop(); };
            _ui.Push(std::move(splash));
        }
        else { LOG_WARN(Engine, "Engine splash image not found — skipping splash screen"); }
    }

    // Route Window input events to InputManager so JustPressed/JustReleased are
    // driven by the SDL event queue rather than polling-based state comparison.
    // This prevents edge events from being missed when the fixed-timestep
    // accumulator fires zero update ticks in a given display frame.
    _window->SetKeyEventCallback([this](i32 sc, bool down) { _input.InjectKeyEvent(sc, down); });
    _window->SetMouseButtonCallback([this](i32 btn, bool down) { _input.InjectMouseButton(btn, down); });
    _window->SetGamepadButtonCallback([this](u32 which, i32 btn, bool down) {
        _input.InjectGamepadButton(which, btn, down);
    });
    _window->SetScrollCallback([this](f32 delta) { _input.InjectMouseScroll(delta); });
    _window->SetTextInputCallback([this](const char* text) { _input.InjectTextInput(text); });
    _ui.SetTextInputActiveCallback([this](bool active) { _window->SetTextInputActive(active); });

    // High-resolution wall-clock timer for accurate delta time.
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<f64>;

    auto prevTime    = Clock::now();
    f64  accumulator = 0.0;

    // FPS counter state — reported once per second via LOG_TRACE.
    auto fpsWindowStart = Clock::now();
    u32  fpsFrameCount  = 0;

    // Restore fullscreen state persisted from a previous session.
    if (Settings::Graphics.Fullscreen)
        _window->ToggleFullscreen();

    LOG_INFO(Engine, "Entering game loop");

    while (_window->PollEvents() && !_shouldShutdown) {
        // Record the frame start time before any work so the FPS limiter can
        // measure the total cost of this frame (including SubmitFrame's back-
        // pressure wait) and sleep only the time that remains.
        const auto frameStart = Clock::now();

        // Input must be updated after PollEvents so SDL's internal state
        // reflects all events from this tick.
        _input.Update();

        // --- Fixed-timestep update -------------------------------------------
        // Accumulate real elapsed time and drain it in fixed-size chunks so
        // game logic runs at a predictable rate regardless of frame rate.
        // The 250 ms cap prevents a "spiral of death" if the app stalls
        // (breakpoint hit, disk stall, etc.) and the accumulator explodes.
        const f64 dt = std::min(Duration(frameStart - prevTime).count(), 0.25);
        prevTime     = frameStart;

        accumulator += dt;
        while (accumulator >= static_cast<f64>(_fixedTimestep)) {
            // ProcessEdges() consumes accumulated key/mouse/gamepad button events
            // and sets JustPressed/JustReleased for this tick. Events are held in
            // the pending sets until ProcessEdges() runs, so they survive frames
            // where the accumulator doesn't reach the threshold.
            _input.ProcessEdges();
            if (_input.JustPressed(ActionEngineFullscreen)) ToggleFullscreen();
            if (!_ui.HasGameBlockingScreen()) {
                OnUpdate(_fixedTimestep);
                if (_scene) _scene->Update(_fixedTimestep);
            }
            _ui.Update(_fixedTimestep,
                       {static_cast<f32>(_window->GetWidth()), static_cast<f32>(_window->GetHeight())},
                       _input);
            accumulator -= static_cast<f64>(_fixedTimestep);
        }

        // --- Variable-rate render --------------------------------------------
        FramePacket packet{};
        packet.Swapchain   = _swapchain;
        packet.Width       = _window->GetWidth();
        packet.Height      = _window->GetHeight();
        packet.ClearColor  = _clearColor;
        packet.NeedsResize = _window->WasResizedThisFrame();

        if (!_ui.HasGameBlockingScreen()) {
            OnRender(packet);
            if (_scene) _scene->CollectRenderData(packet);
        }
        _ui.CollectRenderData(packet,
                              {static_cast<f32>(_window->GetWidth()), static_cast<f32>(_window->GetHeight())});

        // Capture tilemap stats from the packet before the move — the tilemap
        // system populates these during CollectRenderData on the game thread.
        _renderStats.ChunksTotal    = packet.TilemapChunksTotal;
        _renderStats.ChunksRendered = packet.TilemapChunksRendered;
        _renderStats.TilesRendered  = packet.TilemapTilesRendered;

        if (Settings::Graphics.ShowDebugInfo && _debugFont.IsValid() && !_ui.HasGameBlockingScreen()) {
            const std::string overlay = std::format(
                "FPS: {:.0f}\n"
                "Sprites: {} ({} batches)\n"
                "Draw Calls: {}  Lights: {}\n"
                "Chunks: {}/{}  Tiles: {}",
                _renderStats.FPS,
                _renderStats.SpritesSubmitted, _renderStats.SpriteBatches,
                _renderStats.LegacyDrawCalls, _renderStats.LightsActive,
                _renderStats.ChunksRendered, _renderStats.ChunksTotal,
                _renderStats.TilesRendered);
            DrawText(packet, _debugFont, overlay, {8.0f, 8.0f}, 1.0f, {1.0f, 1.0f, 0.0f, 1.0f});
        }

        _renderThread.SubmitFrame(std::move(packet));

        // --- FPS counter + render stats --------------------------------------
        // Count completed frames; report once per second alongside the most
        // recent frame's render statistics from the render thread.
        ++fpsFrameCount;
        if (const f64 fpsElapsed = Duration(Clock::now() - fpsWindowStart).count(); fpsElapsed >= 1.0) {
            _renderStats     = _renderThread.GetStats();
            _renderStats.FPS = static_cast<f32>(static_cast<f64>(fpsFrameCount) / fpsElapsed);
            fpsFrameCount    = 0;
            fpsWindowStart   = Clock::now();
        }

        // --- Asset hot-reload ------------------------------------------------
        // Checked once per second rather than every frame to avoid the cost
        // of stat()-ing every loaded texture path each tick.
        _hotReloadAccumulator += dt;
        if (_hotReloadAccumulator >= 1.0) {
            _textures->CheckReloads();
            _hotReloadAccumulator -= 1.0;
        }

        // --- FPS limiter -----------------------------------------------------
        // Only active when VSync is off and a limit is configured.
        // sleep_for is imprecise (~1-2 ms granularity on Windows), which is
        // acceptable here — VSync is the preferred path for exact pacing.
        if (!Settings::Graphics.VSync && Settings::Graphics.FpsLimit > 0) {
            const f64 targetFrameTime = 1.0 / static_cast<f64>(Settings::Graphics.FpsLimit);
            const f64 frameTime       = Duration(Clock::now() - frameStart).count();
            if (const f64 remaining = targetFrameTime - frameTime; remaining > 0.001)
                // skip sleep for sub-millisecond remainders
                std::this_thread::sleep_for(std::chrono::duration<f64>(remaining));
        }
    }

    // --- Shutdown sequence ---------------------------------------------------
    // GPU work must be fully drained before any Vulkan object is destroyed.
    // Stop the render thread first (joins the thread, so the last frame is
    // guaranteed to have been submitted), then WaitIdle to flush the GPU queue.
    // Only then is it safe for OnShutdown to destroy pipelines, textures, etc.
    _renderThread.Stop();
    _device->WaitIdle();

    if (splashSampler.IsValid())
        _device->DestroySampler(splashSampler);

    // Destroy scene entities before shutting down GPU resources.
    _scene.reset();

    OnShutdown();
    AudioManager::Shutdown();

    // Destroy the debug font atlas resources before the device goes away.
    if (_debugFont.IsValid()) {
        _device->DestroyTexture(_debugFont.GetTexture());
        _device->DestroySampler(_debugFont.GetSampler());
    }

    // Release all cached textures before the device is destroyed.
    _textures->Clear();
    _textures.reset();

    _device->DestroySwapchain(_swapchain);
    Arcbit_DestroyDevice(_device);
    _device = nullptr;

    // Capture any runtime rebinds before writing the settings file.
    Settings::SaveInputBindings(_input);
    Settings::Shutdown();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Log::Shutdown();
}
} // namespace Arcbit
