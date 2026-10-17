#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/core/Math.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/Font.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/save/SaveManager.h>
#include <arcbit/save/SceneState.h>
#include <arcbit/scene/SceneManager.h>
#include <arcbit/scene/SceneRegistry.h>
#include <arcbit/ui/SplashScreen.h>
#include <arcbit/ui/UIManager.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Arcbit
{
class Window;          // full type only needed in Application.cpp
class SavingIndicator; // ApplicationSaves.cpp
class SaveSlotScreen;

// ---------------------------------------------------------------------------
// How the game starts once the engine's own splash has played: the game's
// splash images, then either the first scene or a title menu. Set
// Application::_boot in the subclass constructor or OnStart; Application runs
// it right after OnStart. Phase 38's project.arcbit will fill this from data.
//
// The first scene loads underneath the splash and fades in when the splash
// ends. With ShowTitle, the title menu (MainMenuScreen) opens over it:
//   - TitleScene empty: the title sits over FirstScene, already running
//     behind it; New Game just closes the title.
//   - TitleScene set: the title sits over that scene (a backdrop); New Game
//     switches to FirstScene.
// ---------------------------------------------------------------------------
struct BootConfig
{
    std::vector<std::string> SplashImages;          // the game's own splashes, after the engine's
    f32                      SplashSeconds = 3.0f;  // each, fades included
    std::string              FirstScene;            // registered scene to start; empty = none
    std::string              TitleScene;            // backdrop for the title menu; empty = FirstScene
    bool                     ShowTitle = false;     // open the title menu after the splashes
    std::string              CreditsPath;           // credits JSON for the title's Credits button; empty hides it
};

// ---------------------------------------------------------------------------
// How saving looks, and when the game saves by itself (docs/saves.md). Set
// Application::_saving in the subclass constructor or OnStart; Phase 38's
// project.arcbit (and the editor) will fill it from data.
//
// Every save shows the indicator: the icon spinning in the bottom-right
// corner, with "Saving...". The saving notice explains it - a splash card
// after the logos: the icon spinning over white text on black.
// ---------------------------------------------------------------------------
enum class SaveNotice : u8
{
    Never,
    FirstLaunch, // until the player has seen it once (kept in their settings)
    EveryLaunch,
};

struct SavingConfig
{
    std::string IndicatorIcon    = "assets/engine/ui/saving.png"; // empty = no icon, just the text
    Color       IndicatorTint    = { 1.0f, 1.0f, 1.0f, 1.0f };
    f32         IndicatorSeconds = 1.5f; // how long it shows per save, fades included

    SaveNotice Notice        = SaveNotice::FirstLaunch;
    f32        NoticeSeconds = 5.0f; // on screen, fades included

    bool AutoSaveOnSceneChange = false; // autosave each time a scene change settles (not a load)
};

// ---------------------------------------------------------------------------
// ApplicationConfig
//
// Passed to the Application constructor to set startup defaults.
// These values act as fallbacks: if a settings.json file exists on disk,
// the persisted graphics settings take precedence over Width/Height.
//
// Studio and GameName name the per-user data folder settings and saves live
// in (UserDataFolder): %APPDATA%\<Studio>\<GameName>\ on Windows. A relative
// SettingsPath is inside it.
// ---------------------------------------------------------------------------
struct ApplicationConfig
{
    const char* Title        = "Arcbit";
    u32         Width        = 1280;
    u32         Height       = 720;
    bool        Resizable    = true;
    const char* SettingsPath = "settings.json";
    const char* Studio       = "Arcbit";
    const char* GameName     = nullptr; // null = Title
};

// ---------------------------------------------------------------------------
// Application
//
// The root class of every Arcbit game. Owns all engine systems and runs the
// main loop. Subclass it and override the virtual hooks to add game logic.
//
// Typical usage
// -------------
//   class MyGame : public Arcbit::Application {
//   public:
//       MyGame() : Application({ .Title = "My Game" }) {}
//   protected:
//       void OnStart()               override { /* load assets, bind keys */ }
//       void OnUpdate(f32 dt)        override { /* game logic */ }
//       void OnRender(FramePacket&)  override { /* push draw calls */ }
//       void OnShutdown()            override { /* free GPU resources */ }
//   };
//
//   int main(int, char*[]) { MyGame game; game.Run(); }
//
// System startup / shutdown order
// --------------------------------
//   Startup  (constructor):  Log → Settings → Window → Device → Swapchain → RenderThread
//   Runtime  (Run):          OnStart → LoadInputBindings → [loop] → OnShutdown
//   Shutdown (Run tail):     RenderThread → Device → Settings → Log
//
// Thread safety
// -------------
//   All hooks (OnStart/OnUpdate/OnRender/OnShutdown) and system accessors are
//   called from the main thread. The render thread is managed internally.
// ---------------------------------------------------------------------------
class Application
{
public:
    explicit Application(const ApplicationConfig& config = {});

    // Destructor is defined in Application.cpp so the incomplete Window type
    // is available (unique_ptr requires the complete type at destruction).
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    // Start the main loop. Blocks until the window is closed.
    // Call this exactly once after constructing the Application subclass.
    void Run();

    // --- System accessors (available from OnStart onwards) ---

    // The render device — use in OnStart / OnShutdown to create / destroy
    // pipelines, textures, buffers, and samplers.
    [[nodiscard]] RenderDevice& GetDevice();

    // The input manager — use in OnStart to register actions and add bindings,
    // then query it in OnUpdate.
    [[nodiscard]] InputManager& GetInput() { return _input; }

    // Current window dimensions in physical pixels. Updated each frame on resize.
    // Use these when converting mouse coordinates or computing aspect ratios.
    [[nodiscard]] u32 GetWindowWidth() const;
    [[nodiscard]] u32 GetWindowHeight() const;

    // Toggle between windowed and borderless fullscreen. F11 triggers this
    // automatically — only call explicitly for settings menus etc.
    void ToggleFullscreen();

    // The texture manager — load textures from disk in OnStart; the manager
    // caches by path so repeated loads are free. Application clears it
    // automatically during shutdown, but you can also Unload() explicitly.
    [[nodiscard]] TextureManager& GetTextures() { return *_textures; }

    // The active scene — access the ECS world, camera, and world config from
    // OnStart onwards. Before any scene is registered and switched to, this is
    // a default unmanaged Scene the game populates itself. Don't hold the
    // reference across a scene switch.
    [[nodiscard]] Scene& GetScene();

    // Register scenes and switch between them (fade out → swap → fade in).
    [[nodiscard]] SceneManager& GetScenes() { return _scenes; }

    // Components and named systems scene files may use. Holds the engine's
    // built-ins; register game components and systems in OnStart.
    [[nodiscard]] SceneRegistry& GetSceneRegistry() { return _sceneRegistry; }

    // Shared samplers scene files refer to by name: Nearest ("nearest", point
    // filtering, repeat) and Linear ("linear", bilinear, clamp). Created
    // before OnStart, destroyed at shutdown.
    enum class StockSampler : u8
    {
        Nearest,
        Linear,
    };
    [[nodiscard]] SamplerHandle GetStockSampler(StockSampler which) const;

    // The engine's 1x1 white texture ("builtin:white" in scene files) - tint
    // it for solid-color quads.
    [[nodiscard]] TextureHandle GetWhiteTexture() const { return _renderThread.GetUIWhiteTexture(); }

    // The UI manager — push screens in OnStart; query widgets in OnUpdate.
    [[nodiscard]] UIManager& GetUI() { return _ui; }

    // The event bus (docs/events.md). Published events are delivered once per
    // fixed tick, after the scene update.
    [[nodiscard]] EventBus& GetEvents() { return _events; }

    // Save slots, migrations and the game's save data (docs/saves.md). Its
    // folder is the per-user data folder's saves\.
    [[nodiscard]] SaveManager& GetSaves() { return _saves; }

    // The per-user data folder: settings, saves (see ApplicationConfig).
    [[nodiscard]] const std::filesystem::path& GetUserFolder() const { return _userFolder; }

    // --- Saving and loading the game (docs/saves.md) -------------------------

    // Save the game to `slot`: SaveData, every participant's section, every
    // visited scene's changes and the scene stack (the engine's own sections,
    // "world" and "scenes"). `name` labels it in the slot list; empty = the
    // active scene's name. Refused (false, logged) while a scene change is
    // under way or with no named scene loaded. Publishes engine.game.saved.
    bool SaveTo(SaveSlot slot, std::string name = {});

    // Load the save in `slot`: the screen fades out, the current scenes
    // unload, the save's state goes back and its scene stack loads. False
    // (logged) if it can't be read or a scene change is under way. Publishes
    // engine.game.loaded once the save's state is back.
    bool LoadFrom(SaveSlot slot);

    // Forget the current game: SaveData, every participant (handed an empty
    // section), the world state and playtime. The title's New Game does this.
    void ResetGame();

    // Save to the next autosave slot (SaveManager::NextAutoSlot) - at a
    // checkpoint, on resting. False if refused, like SaveTo. With
    // SavingConfig::AutoSaveOnSceneChange it also runs after scene changes.
    bool AutoSave();

    // Seconds of play this game, carried in its saves: counted while the
    // player has control - not under a menu, a fade or the splash.
    [[nodiscard]] f64 GetPlaytime() const { return _playtime; }

    // Color format of the swapchain — pass to PipelineDesc::ColorFormat so
    // pipelines match the actual surface format chosen by the driver.
    [[nodiscard]] Format GetSwapchainFormat() const;

    // Frames per second measured by the application loop (smoothed).
    [[nodiscard]] f32 GetFPS() const { return _renderStats.FPS; }

    // Ask the main loop to exit after the current frame (e.g. a menu's Quit).
    void RequestShutdown() { _shouldShutdown = true; }

    // Replace the window's title bar text.
    void SetWindowTitle(std::string_view title);

    // Push one of the engine's settings screens (each pops itself on Back) -
    // what the title menu's and a pause menu's buttons open.
    void ShowControlsScreen();
    void ShowAudioScreen();
    void ShowGraphicsScreen();

    // Roll the credits (BootConfig::CreditsPath) - the title menu's Credits
    // button, or the end of a game.
    void ShowCredits();

    // The save slot list (SaveSlotScreen), for a pause menu's Save Game and
    // the title's or a pause menu's Load Game. Saving over a slot asks first,
    // then closes the list; loading closes every menu.
    void ShowSaveScreen();
    void ShowLoadScreen();

    // Ask a yes / no question over the current menu (ConfirmScreen); onYes
    // runs if the player agrees. Either answer closes it.
    void Confirm(std::string message, std::function<void()> onYes);

    // The screen shown while a scene change waits at black for its scene to
    // load (SceneChange::ShowLoadingScreen), after a short grace period so a
    // quick load doesn't flash it. Defaults to the engine's LoadingScreen;
    // pass a factory for your own (any UIScreen, BlocksGame off) or null for
    // plain black.
    using ScreenFactory = std::function<std::unique_ptr<UIScreen>()>;
    void SetLoadingScreen(ScreenFactory factory) { _loadingScreenFactory = std::move(factory); }

protected:
    
    void ToggleDebugGrid() { _showGrid = !_showGrid; }

    // -----------------------------------------------------------------------
    // Game hooks — override these in your Application subclass.
    // -----------------------------------------------------------------------

    // Called once after all systems are initialized, before the loop begins.
    // Register input actions, create pipelines, load assets here.
    virtual void OnStart() {}

    // Called at a fixed rate (fixedTimestep seconds per call) for game logic
    // that must be frame-rate-independent (physics, AI, gameplay state).
    virtual void OnUpdate(f32 dt) {}

    // Called every frame (variable rate) to build the render packet.
    // The packet is pre-filled with Swapchain, Width, Height, NeedsResize,
    // and ClearColor. Push your DrawCalls into packet.DrawCalls.
    virtual void OnRender(FramePacket& packet) {}

    // Called once after the loop exits, before systems are shut down.
    // Destroy GPU resources (pipelines, textures, buffers) here.
    virtual void OnShutdown() {}

    // -----------------------------------------------------------------------
    // Tunable knobs — set in the subclass constructor or OnStart.
    // -----------------------------------------------------------------------

    // Background clear color used to pre-fill each FramePacket.
    Color _clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    // Target rate for OnUpdate calls. Defaults to 60 Hz.
    // Change before Run() is called; modifying mid-loop is not recommended.
    f32 _fixedTimestep = 1.0f / 60.0f;

    // How the game starts after the engine splash (see BootConfig). Set it in
    // the constructor or OnStart.
    BootConfig _boot;

    // The saving indicator, the first-launch notice and autosaves (see
    // SavingConfig). Set it in the constructor or OnStart.
    SavingConfig _saving;

    // The engine's built-in SDF debug font — Roboto loaded at engine startup.
    [[nodiscard]] const FontAtlas& GetDebugFont() const { return _debugFont; }
    
    bool        _showGrid = false;

private:
    ApplicationConfig     _config;
    std::filesystem::path _userFolder; // set first in the constructor: settings and saves live in it
    SaveManager           _saves;
    WorldState            _world; // every visited scene's changes; the saves' "world" section
    f64                   _playtime = 0.0;

    // Saving (ApplicationSaves.cpp).
    void RegisterSaveSections(); // "world" and "scenes"
    void StartSaving();          // the indicator, once OnStart has set _saving
    void AfterSwap();            // a scene change finished: queue an autosave?
    void UpdateAutoSave();       // run a queued autosave once the game is idle
    void WriteThumbnail();       // a frame captured for the last save arrived: write it
    void WireThumbnails(SaveSlotScreen& screen); // ApplicationBoot.cpp
    [[nodiscard]] bool        NeedsSaveNotice() const;
    [[nodiscard]] SplashEntry SaveNoticeCard();

    SavingIndicator*       _savingIndicator = nullptr; // on the UI stack from StartBoot
    TextureHandle          _savingIcon;
    bool                   _autoSavePending = false;
    bool                   _restoredSave    = false; // the last swap loaded a save: no autosave
    std::optional<SaveSlot> _thumbnailFor;            // saved, its picture not yet written
    bool                    _captureNextFrame = false; // ask the render thread for the next frame

    // Member declaration order determines both initialization and destruction
    // order (destruction is reverse of initialization).
    //
    // Required destruction order:
    //   _input        — closes SDL gamepad handles (SDL must still be alive)
    //   _window       — calls SDL_Quit (must outlive InputManager)
    //   _renderThread — stopped explicitly in Run(); destructor is a no-op
    //   _swapchain    — handle only; destroyed explicitly in Run()
    //   _textures     — unique_ptr; reset() called explicitly before DestroyDevice
    //   _device       — raw pointer; destroyed explicitly in Run()
    //
    // So declaration order is the reverse of that (destructor fires bottom-up):
    // Engine system actions — registered before OnStart so settings can rebind them.
    static constexpr ActionID ActionEngineFullscreen = MakeAction("Engine_Fullscreen");

    bool _shouldShutdown = false;

    FontAtlas   _debugFont;
    RenderStats _renderStats{};

    UIManager    _ui;
    EventBus     _events;   // before _scenes: scenes' subscriptions end before the bus goes
    Subscription _eventLog; // every delivered event, on the EventBus log channel (debug level)

    SceneManager                    _scenes;
    SceneRegistry                   _sceneRegistry;
    SamplerHandle                   _nearestSampler;
    SamplerHandle                   _linearSampler;
    RenderDevice*                   _device = nullptr;
    std::unique_ptr<TextureManager> _textures;
    SwapchainHandle                 _swapchain;
    RenderThread                    _renderThread;
    std::unique_ptr<Window>         _window;
    InputManager                    _input;

    // Device, focus and resize changes this frame, as engine events.
    void PublishFrameEvents();

    // Full-window black quad over the world while a scene switch fades.
    void CollectSceneFade(FramePacket& packet) const;

    // Boot flow (ApplicationBoot.cpp).
    void StartBoot();
    void PushSplash();
    void FinishSplash();
    void ShowTitle();
    std::vector<TextureHandle> _splashTextures; // released once the splash ends

    // Loading screen over a scene change held at black.
    void            UpdateLoadingScreen(f32 dt);
    ScreenFactory   _loadingScreenFactory;    // the engine's LoadingScreen unless the game changes it
    const UIScreen* _loadingScreen = nullptr; // on the UI stack while shown
    f32             _loadingHeld   = 0.0f;    // seconds held at black so far

    // Accumulates real time to rate-limit hot-reload checks to once per second.
    f64 _hotReloadAccumulator = 0.0;
};
} // namespace Arcbit
