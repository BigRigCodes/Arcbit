#include <arcbit/app/Application.h>
#include <arcbit/app/UserFolder.h>
#include <arcbit/app/Window.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/scene/Scene.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/ConsoleScreen.h>
#include <arcbit/ui/LoadingScreen.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/core/Assert.h>

#include <format>

#include <chrono>
#include <limits>
#include <thread>
#include <utility>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Constructor — initializes all engine systems in dependency order.
// ---------------------------------------------------------------------------

Application::Application(const ApplicationConfig& config) : _config(config)
{
    Log::Init();
    LOG_INFO(Engine, "Arcbit starting");

    // Settings and saves live in the per-user data folder (it may log, so
    // after Log::Init).
    _userFolder = UserDataFolder(config.Studio, config.GameName ? config.GameName : config.Title);
    _saves.SetDirectory(_userFolder / "saves");
    _scriptEngine.SetRoot(config.ScriptRoot);
    LOG_INFO(Engine, "User data folder: '{}'", _userFolder.string());

    // Every delivered event goes to the log (debug level) - what fired, when,
    // with what payload.
    _eventLog = _events.SubscribeAll([this](const Event& e) {
        LOG_DEBUG(EventBus, "{} {}", _events.Describe(e.Id), e.Payload.ToString());
    });

    // The engine's loading screen, its bar following the scene load.
    _loadingScreenFactory = [this] {
        auto screen      = std::make_unique<LoadingScreen>();
        screen->Progress = [this] { return _scenes.GetLoadProgress(); };
        return screen;
    };

    // Seed Settings with config defaults so that GraphicsSettings has sensible
    // values even when no settings file exists yet. ReadFile() will then
    // overwrite these with any persisted values.
    Settings::Graphics.ResolutionWidth  = config.Width;
    Settings::Graphics.ResolutionHeight = config.Height;
    const std::filesystem::path settingsPath = _userFolder / config.SettingsPath;
    MoveLegacyFile(config.SettingsPath, settingsPath); // it used to sit next to the executable
    Settings::Init(settingsPath.string());
    Settings::SetChangeListener([this](const std::string_view section, const std::string_view key) {
        EventPayload payload{ { "section", std::string(section) } };
        if (!key.empty()) payload.Set("key", std::string(key));
        _events.PublishEngine(EngineEvents::SettingsChanged, std::move(payload));
    });
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
    Scene* scene = _scenes.GetActive();
    ARCBIT_ASSERT(scene != nullptr, "GetScene() called before Run()");
    return *scene;
}

void Application::ToggleFullscreen()
{
    _window->ToggleFullscreen();
    Settings::Graphics.Fullscreen = !Settings::Graphics.Fullscreen;
    Settings::MarkDirty("graphics");
}

SamplerHandle Application::GetStockSampler(const StockSampler which) const
{
    return which == StockSampler::Linear ? _linearSampler : _nearestSampler;
}

void Application::SetWindowTitle(const std::string_view title) { _window->SetTitle(title); }

// Engine events from this frame's window and input changes, delivered at
// the next flush.
void Application::PublishFrameEvents()
{
    if (_input.ConsumeDeviceChanged())
        _events.PublishEngine(EngineEvents::InputDeviceChanged,
                              { { "device", std::string(ToString(_input.GetLastDevice())) } });
    if (_window->WasFocusChangedThisFrame())
        _events.PublishEngine(EngineEvents::WindowFocusChanged, { { "focused", _window->HasFocus() } });
    if (_window->WasResizedThisFrame()) {
        const Vec2 size{ static_cast<f32>(_window->GetWidth()), static_cast<f32>(_window->GetHeight()) };
        _events.PublishEngine(EngineEvents::WindowResized, { { "size", size } });
    }
}

// Show the loading screen once a scene change has held at black for a
// moment, and fade it out as the new scene fades in. Never over a
// game-blocking screen (the splash): that already covers the load.
void Application::UpdateLoadingScreen(const f32 dt)
{
    constexpr f32 GraceSeconds = 0.25f;

    if (!_scenes.WantsLoadingScreen() || _ui.HasGameBlockingScreen()) {
        _loadingHeld = 0.0f;
        if (_loadingScreen) _ui.Close(_loadingScreen);
        _loadingScreen = nullptr;
        return;
    }
    _loadingHeld += dt;
    if (_loadingScreen || !_loadingScreenFactory || _loadingHeld < GraceSeconds) return;

    std::unique_ptr<UIScreen> screen = _loadingScreenFactory();
    if (!screen) return;
    screen->Layer      = UILayer::Overlay; // above menus; a Pop() meant for a menu can't close it
    _loadingScreen     = screen.get();
    _ui.Push(std::move(screen));
    LOG_DEBUG(Engine, "Loading screen shown");
}

void Application::CollectSceneFade(FramePacket& packet) const
{
    const f32 alpha = _scenes.GetFadeAlpha();
    if (alpha <= 0.0f) return;

    const Vec2 size{ static_cast<f32>(_window->GetWidth()), static_cast<f32>(_window->GetHeight()) };
    Sprite     quad{};
    quad.Texture  = _renderThread.GetUIWhiteTexture();
    quad.Sampler  = _renderThread.GetUIWhiteSampler();
    quad.Position = size * 0.5f;
    quad.Size     = size;
    quad.Tint     = { 0.0f, 0.0f, 0.0f, alpha };
    quad.Layer    = std::numeric_limits<i32>::lowest(); // under every UI screen
    packet.UISprites.push_back(quad);
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
    _input.RegisterAction(ActionEngineConsole, "Engine_Console", "Console", "System");
    _input.BindKey(ActionEngineConsole, Key::Grave);
    _input.RegisterAction(ConsoleScreen::ActionHistoryUp, "Console_HistoryUp");
    _input.BindKey(ConsoleScreen::ActionHistoryUp, Key::Up);
    _input.RegisterAction(ConsoleScreen::ActionHistoryDown, "Console_HistoryDown");
    _input.BindKey(ConsoleScreen::ActionHistoryDown, Key::Down);
    _input.RegisterAction(ConsoleScreen::ActionComplete, "Console_Complete");
    _input.BindKey(ConsoleScreen::ActionComplete, Key::Tab);
    for (const ActionID action : { ActionEngineFullscreen, ActionEngineConsole, ConsoleScreen::ActionHistoryUp,
                                   ConsoleScreen::ActionHistoryDown, ConsoleScreen::ActionComplete })
        _input.KeepWhileTyping(action);

    // Stock samplers and the scene registry's built-ins, before any scene loads.
    _nearestSampler = _device->CreateSampler(SamplerDesc{ .MinFilter = Filter::Nearest, .MagFilter = Filter::Nearest,
                                                          .AddressU = AddressMode::Repeat, .AddressV = AddressMode::Repeat,
                                                          .DebugName = "StockNearest" });
    _linearSampler  = _device->CreateSampler(SamplerDesc{ .MinFilter = Filter::Linear, .MagFilter = Filter::Linear,
                                                          .AddressU = AddressMode::ClampToEdge,
                                                          .AddressV = AddressMode::ClampToEdge, .DebugName = "StockLinear" });
    RegisterBuiltinComponents(_sceneRegistry);

    // Install a default scene before OnStart so game code can use GetScene()
    // immediately; a game that registers scenes replaces it with Switch().
    _scenes.SetHost({ .App             = this,
                      .Input           = &_input,
                      .Quiesce         = [this] { _renderThread.WaitIdle(); },
                      .Textures        = _textures.get(),
                      .PrepareOnThread = true,
                      .Music           = &AudioManager::GetMusicPlayer(),
                      .Events          = &_events,
                      .World           = &_world,
                      .Registry        = &_sceneRegistry,
                      .Assets          = [](Scene& scene) { return MakeAppSceneAssets(scene); },
                      .Scripts         = &_scriptEngine });
    RegisterSaveSections();
    _scriptEngine.SetServices({ .Scenes    = &_scenes,
                                .Input     = &_input,
                                .Events    = &_events,
                                .Saves     = &_saves,
                                .Registry  = &_sceneRegistry,
                                .UI        = &_ui,
                                .Console   = &_debugConsole,
                                .Assets    = [](Scene& scene) { return MakeAppSceneAssets(scene); },
                                .PlaySound = [](const std::string_view path, const f32 volume) {
                                    AudioManager::PlayOneShot(path, volume);
                                },
                                .SaveTo   = [this](const SaveSlot slot) { return SaveTo(slot); },
                                .LoadFrom = [this](const SaveSlot slot) { return LoadFrom(slot); },
                                .AutoSave = [this] { return AutoSave(); } });
    _scenes.SetActive(std::make_unique<Scene>());

    // Load the engine's built-in debug font before game code runs so subclasses
    // can use it in OnStart if they need to (e.g. preload additional glyphs).
    _debugFont.Load("assets/engine/fonts/Roboto-Regular.ttf", 24.0f, FontMode::SDF, *_device);

    // Init UI manager after the render thread is running so white-texture handles are valid.
    // Registers UI navigation actions (UI_Confirm, UI_FocusNext, UI_FocusPrev) before OnStart
    // so games can rebind them via Settings::LoadInputBindings after OnStart.
    _ui.Init(_renderThread, _debugFont, _input);
    StartConsole(); // after the UI (error toasts), before OnStart (games add their own commands)

    // Load engine locale strings before OnStart so built-in screens display correctly.
    if (!Loc::Load("assets/engine/locale/en.json"))
        LOG_WARN(Engine, "Failed to load locale strings");

    // Give the game a chance to register actions, create GPU resources, etc.
    // Input bindings are loaded from settings AFTER OnStart so that default
    // bindings set up in OnStart are overwritten by any user-saved bindings.
    OnStart();
    Settings::LoadInputBindings(_input);

    // Engine splash, the game's splashes, then the title screen or first
    // scene, as BootConfig asks (ApplicationBoot.cpp).
    StartBoot();

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
    _ui.SetTextInputActiveCallback([this](const bool active) {
        _window->SetTextInputActive(active);
        _input.SetTyping(active); // the game's keys rest while someone types
    });

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
        PublishFrameEvents();

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
            if (_input.JustPressed(ActionEngineConsole) && IsConsoleAllowed()) ToggleConsole();
            if (!_ui.HasGameBlockingScreen()) {
                OnUpdate(_fixedTimestep);
                _scenes.Update(_fixedTimestep);
                if (Scene* scene = _scenes.GetActive()) {
                    // An input-capturing overlay or a scene fade holds the player still.
                    scene->SetInputBlocked(_ui.HasBlockingScreen() || _scenes.IsFading());
                    scene->Update(_fixedTimestep * _timeScale);
                }
                if (!_ui.HasBlockingScreen() && !_scenes.IsFading()) _playtime += _fixedTimestep;
            }
            // Under a screen that hides the game (the splash on its logo), a
            // queued swap may run - its load stays out of sight - but the
            // fades hold (dt 0) so the scene still fades in once visible.
            else if (_ui.CoversLoading()) {
                _scenes.Update(0.0f);
            }
            UpdateLoadingScreen(_fixedTimestep);

            // Deliver this tick's events - after the scene has updated, so a
            // handler never runs mid-update, and before the UI reacts.
            _events.Flush();
            _ui.Update(_fixedTimestep,
                       {static_cast<f32>(_window->GetWidth()), static_cast<f32>(_window->GetHeight())},
                       _input);
            accumulator -= static_cast<f64>(_fixedTimestep);

            // A scene swap can take a while (loading). Don't simulate that
            // time afterwards, or the fade-in jumps ahead in one frame.
            if (_scenes.ConsumeSwapped()) {
                accumulator = 0.0;
                prevTime    = Clock::now();
                AfterSwap();
            }
            UpdateAutoSave();
        }

        // --- Variable-rate render --------------------------------------------
        FramePacket packet{};
        packet.Swapchain   = _swapchain;
        packet.Width       = _window->GetWidth();
        packet.Height      = _window->GetHeight();
        packet.ClearColor  = _clearColor;
        packet.NeedsResize = _window->WasResizedThisFrame();
        packet.CaptureWorld = std::exchange(_captureNextFrame, false); // a save's thumbnail
        if (!packet.CaptureWorld && std::exchange(_screenshotNextFrame, false)) { // one capture a frame
            packet.CaptureFrame = true;
            ++_screenshotsPending;
        }

        if (!_ui.HasGameBlockingScreen()) {
            OnRender(packet);
            if (Scene* scene = _scenes.GetActive()) scene->CollectRenderData(packet);
            CollectSceneFade(packet);
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

        // Glyphs baked while drawing this frame reach the GPU before it renders.
        FontAtlas::UploadAll(*_device, [this] { _renderThread.WaitIdle(); });
        _renderThread.SubmitFrame(std::move(packet));
        WriteCapture(); // a frame captured for a save or a screenshot, once the render thread has it

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
            _textures->CheckReloads([this] { _renderThread.WaitIdle(); });
#ifdef ARCBIT_DEBUG
            _scriptEngine.CheckReloads(); // edited scripts take effect without a restart
#endif
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


    // Unload the active scene (OnUnload, scene textures, entities) before the
    // game's OnShutdown and the GPU teardown.
    _scenes.Shutdown();

    OnShutdown();
    AudioManager::Shutdown();
    _device->DestroySampler(_nearestSampler);
    _device->DestroySampler(_linearSampler);

    // Destroy the debug font atlas resources before the device goes away.
    _debugFont.Release(*_device);

    // Release all cached textures before the device is destroyed.
    _textures->Clear();
    _textures.reset();

    _device->DestroySwapchain(_swapchain);
    Arcbit_DestroyDevice(_device);
    _device = nullptr;

    // Capture any runtime rebinds before writing the settings file.
    Settings::SaveInputBindings(_input);
    Settings::SetChangeListener({}); // it publishes to this application's bus
    Settings::Shutdown();

    LOG_INFO(Engine, "Arcbit shutdown complete");
    Log::Shutdown();
}
} // namespace Arcbit
