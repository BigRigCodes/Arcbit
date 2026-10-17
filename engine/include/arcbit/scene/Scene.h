#pragma once

#include <arcbit/ecs/World.h>
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
class PhysicsWorld;
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

    // Build entities, acquire resources (LoadTexture), register scene systems.
    virtual void OnLoad() {}

    // Release what OnLoad created outside the World and LoadTexture — samplers,
    // textures made directly on the device. Entities and LoadTexture textures
    // are released automatically. The render thread is idle, so destroying GPU
    // resources here is safe.
    virtual void OnUnload() {}

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

    // Called by Application each fixed tick: OnUpdate, the camera, then all
    // registered update systems in registration order.
    void Update(f32 dt);

    // Called by Application each frame after its own OnRender: sets camera
    // packet fields, then OnRender, then the render systems.
    void CollectRenderData(FramePacket& packet);

private:
    friend class SceneManager;

    // Drop this scene's LoadTexture references (SceneManager, on unload).
    void ReleaseResources();

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
};

} // namespace Arcbit
