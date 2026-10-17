#pragma once

#include <arcbit/ecs/World.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/render/Camera2D.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/scene/WorldConfig.h>
#include <arcbit/tilemap/TileMap.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit {

struct FramePacket;
class Application;
class InputManager;
class MusicPlayer;
struct AuthoredState;
class PhysicsWorld;
class SceneScripts;
class ScenePreloader;
class ScriptEngine;
class SpriteSheet;

// ---------------------------------------------------------------------------
// Scene — top-level container for a running game world.
//
// Owns the ECS World, the active Camera2D, the WorldConfig resource, the
// TileMap, and (lazily) the PhysicsWorld. Application drives the active scene
// via Update() (fixed tick) and CollectRenderData() (variable rate). Game code
// accesses it through Application::GetScene().
//
// Subclass it and override the lifecycle hooks for a scene that builds itself;
// SceneManager calls them on load / unload. A plain Scene leaves them empty
// and is populated from outside (Application::OnStart). Data-driven scenes
// load from a file in OnLoad, so editor-made games need no subclasses.
// ---------------------------------------------------------------------------
class Scene
{
public:
    Scene();
    virtual ~Scene(); // out-of-line so PhysicsWorld only needs to be complete in the .cpp

    // Systems and hooks capture `this`, so a Scene never moves.
    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    // --- Lifecycle hooks (called by SceneManager) ---------------------------

    // Load ahead, on a loader thread, while the previous scene keeps running:
    // read and parse files into this scene's members, and name the assets
    // OnLoad will use so they decode here (see ScenePreloader). Runs before
    // OnLoad, once. Touch nothing outside this object - not the World,
    // GetApp(), the GPU, UI or audio; those belong in OnLoad.
    virtual void OnPrepare(ScenePreloader& /*preload*/) {}

    // Build entities, acquire resources (LoadTexture), register scene systems.
    virtual void OnLoad() {}

    // Release what OnLoad created outside the World and LoadTexture — samplers,
    // textures made directly on the device. Entities and LoadTexture textures
    // are released automatically. The render thread is idle, so destroying GPU
    // resources here is safe.
    virtual void OnUnload() {}

    // Another scene was pushed on top of this one: from now until OnResume
    // it isn't updated or drawn, and its sounds are stopped. Its entities and
    // resources stay as they are.
    virtual void OnSuspend() {}

    // The scene above was popped and this one is active again.
    virtual void OnResume() {}

    // Per-scene game logic, each fixed tick before the scene's systems run.
    virtual void OnUpdate(f32 /*dt*/) {}

    // Per-scene render work, each frame before the scene's render systems.
    virtual void OnRender(FramePacket& /*packet*/) {}

    // Name this scene was registered under (empty for an unmanaged scene).
    [[nodiscard]] const std::string& GetName() const { return _name; }

    // Where the switch that loaded this scene asked to arrive (e.g. a spawn
    // marker's name); empty when none was given. Read it in OnLoad.
    [[nodiscard]] const std::string& GetEntryPoint() const { return _entryPoint; }

    // The application hosting this scene — valid in the lifecycle hooks of a
    // scene loaded by an Application's SceneManager.
    [[nodiscard]] Application& GetApp();

    // Load a texture for this scene's lifetime. Cached by path and
    // reference-counted across scenes: it is released when this scene unloads
    // and destroyed once no scene (or app-wide TextureManager::Load) uses it.
    [[nodiscard]] TextureHandle LoadTexture(std::string_view path);

    // Load a sprite sheet for this scene's lifetime (cached by path within
    // the scene; its texture through LoadTexture). Null if it fails. The
    // pointer stays valid until the scene unloads - what Animator::Sheet
    // needs.
    [[nodiscard]] const SpriteSheet* LoadSpriteSheet(std::string_view path);

    // The path a sheet from LoadSpriteSheet was loaded from; empty if it
    // isn't one of this scene's.
    [[nodiscard]] std::string GetSpriteSheetPath(const SpriteSheet* sheet) const;

    // --- Events -------------------------------------------------------------

    // The application's event bus (null for a scene with no host, e.g. in
    // tests that don't give one).
    [[nodiscard]] EventBus* GetEvents() const { return _events; }

    // Subscribe for this scene's lifetime: the subscription ends when the
    // scene unloads, so a handler capturing the scene never outlives it - no
    // cleanup to write. A scene suspended under a push keeps receiving.
    // Call from OnLoad or later (not OnPrepare: the bus is main-thread only).
    void Subscribe(EventId id, EventHandler handler);

    // --- Music --------------------------------------------------------------

    // Give this scene its own music: `path` plays whenever this is the active
    // scene, crossfading from what played before (the same track carries on
    // uninterrupted); an empty path means silence. Call it in OnLoad, or any
    // time later to change track (a boss fight). A scene that never calls it
    // leaves the music alone - whatever plays keeps playing - except that
    // leaving a scene that owns music fades it out. Suspended under a pushed
    // scene, the track resumes where it was when the scene is active again.
    void SetMusic(std::string path, bool loop = true);

    [[nodiscard]] bool               OwnsMusic() const { return _ownsMusic; }
    [[nodiscard]] const std::string& GetMusic() const { return _musicPath; }

    // --- World access -------------------------------------------------------

    // ECS world — create/destroy entities, add/remove components, register systems.
    [[nodiscard]] World&       GetWorld()   { return _world; }
    [[nodiscard]] Camera2D&    GetCamera()  { return _camera; }
    [[nodiscard]] WorldConfig& GetConfig()  { return _config; }
    [[nodiscard]] TileMap&     GetTileMap() { return _tileMap; }

    // PhysicsWorld is lazy-constructed on first call using WorldConfig.TileSize
    // as the spatial-hash cell size, and is automatically wired to the scene's
    // TileMap. **Constraint**: set GetConfig().TileSize *before* the first
    // call (typically the first thing OnLoad does), otherwise the hash is
    // sized for the default (32 px) and changing TileSize afterwards has no
    // effect on the broadphase grid.
    [[nodiscard]] PhysicsWorld& GetPhysics();

    // Input source for InputMovementSystem. SceneManager wires the
    // application's InputManager in; null (the default, e.g. in tests) leaves
    // MoveIntent untouched. Not owned.
    void                              SetInput(const InputManager* input) { _input = input; }
    [[nodiscard]] const InputManager* GetInput() const { return _input; }

    // While blocked, InputMovementSystem reads every move action as released
    // (so movers stop rather than keep their last intent). Application sets
    // it each tick from the UI and from scene transitions.
    void               SetInputBlocked(const bool blocked) { _inputBlocked = blocked; }
    [[nodiscard]] bool IsInputBlocked() const { return _inputBlocked; }

    // The scene's own Lua script (docs/scripting.md) - hooks OnLoad,
    // OnUpdate(dt) and OnUnload. Set it in the constructor or OnLoad (a scene
    // file's "script"); it starts once the scene has loaded.
    void                             SetScript(std::string module) { _scriptModule = std::move(module); }
    [[nodiscard]] const std::string& GetScript() const { return _scriptModule; }

    // Called by Application each fixed tick: OnUpdate, the scripts' OnUpdate,
    // the camera, then all registered update systems in registration order.
    void Update(f32 dt);

    // Called by Application each frame after its own OnRender: sets camera
    // packet fields, then OnRender, then the render systems.
    void CollectRenderData(FramePacket& packet);

private:
    friend class SceneManager;

    // Drop this scene's LoadTexture references (SceneManager, on unload).
    void ReleaseResources();

    // Stop every AudioSource's sound; the audio system restarts the playing
    // ones once the scene updates again (SceneManager, on suspend).
    void StopSounds();

    // Scripts (SceneManager): start them once the scene has loaded, with its
    // saved state applied; end them (OnDestroy, OnUnload) as it unloads.
    void StartScripts();
    void EndScripts();

    World                         _world;
    Camera2D                      _camera;
    WorldConfig                   _config;
    TileMap                       _tileMap;
    std::unique_ptr<PhysicsWorld> _physics; // lazy; created on first GetPhysics()
    const InputManager*           _input        = nullptr;
    bool                          _inputBlocked = false;

    std::string                _name;
    std::string                _entryPoint;
    Application*               _app = nullptr;
    std::vector<TextureHandle> _textures; // LoadTexture references to release

    std::unordered_map<std::string, std::unique_ptr<SpriteSheet>> _sheets; // LoadSpriteSheet, by path

    // Music (SetMusic). _musicPlayer is set by SceneManager only while this
    // is the active scene, so SetMusic plays at once then and waits otherwise.
    bool         _ownsMusic = false;
    std::string  _musicPath;
    bool         _musicLoop   = true;
    f32          _musicResume = 0.0f; // seconds into the track when suspended
    MusicPlayer* _musicPlayer = nullptr;
    f32          _musicFade   = 0.0f;

    std::unique_ptr<AuthoredState> _authored; // persistent entities as loaded (SceneManager; save state)

    ScriptEngine*                 _scriptEngine = nullptr; // SceneManager's host; null = no scripts
    std::string                   _scriptModule;           // SetScript
    std::unique_ptr<SceneScripts> _scripts;                // running scripts, from StartScripts to EndScripts

    EventBus*                 _events = nullptr;
    std::vector<Entity>       _pendingSpawns; // EntityEvents added; announced at the next Update
    bool                      _tearingDown = false; // destructor: entities going with the scene aren't "destroyed"
    std::vector<Subscription> _subscriptions; // Subscribe; ended at unload (and destroyed first)

    void HookEntityEvents();     // constructor: OnAdd / OnDestroy for EntityEvents
    void PublishPendingSpawns(); // Update: the spawned events, now the entities are complete
    void PublishEntityEvent(Entity entity, const std::string& rename, EventId fallback);
};

} // namespace Arcbit
