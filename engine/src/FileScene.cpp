#include <arcbit/scene/FileScene.h>

#include <arcbit/app/Application.h>
#include <arcbit/core/Log.h>
#include <arcbit/physics/PhysicsWorld.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/scene/ScenePreloader.h>

#include <unordered_set>
#include <arcbit/tilemap/TileMap.h>

namespace Arcbit
{
namespace
{
    constexpr std::string_view BuiltinWhite = "builtin:white";

    // SceneAssets on top of a hosted scene: textures and sprite sheets owned
    // by the scene (reference-counted), the application's stock samplers,
    // white texture and input action names.
    class AppSceneAssets final : public SceneAssets
    {
    public:
        explicit AppSceneAssets(Scene& scene) : _scene(scene), _app(scene.GetApp()) {}

        TextureHandle LoadTexture(const std::string_view path) override
        {
            return path == BuiltinWhite ? _app.GetWhiteTexture() : _scene.LoadTexture(path);
        }
        std::string TexturePath(const TextureHandle texture) const override
        {
            if (!texture.IsValid()) return {};
            if (texture == _app.GetWhiteTexture()) return std::string(BuiltinWhite);
            return _app.GetTextures().GetPath(texture);
        }
        const ItemLibrary* Items() const override { return &_app.GetItems(); }

        SamplerHandle Sampler(const std::string_view name) override
        {
            return _app.GetStockSampler(name == "linear" ? Application::StockSampler::Linear
                                                         : Application::StockSampler::Nearest);
        }
        std::string SamplerName(const SamplerHandle sampler) const override
        {
            if (sampler == _app.GetStockSampler(Application::StockSampler::Nearest)) return "nearest";
            if (sampler == _app.GetStockSampler(Application::StockSampler::Linear)) return "linear";
            return {};
        }
        const SpriteSheet* LoadSpriteSheet(const std::string_view path) override { return _scene.LoadSpriteSheet(path); }
        std::string SpriteSheetPath(const SpriteSheet* sheet) const override { return _scene.GetSpriteSheetPath(sheet); }
        std::string ActionName(const ActionID action) const override
        {
            return std::string(_app.GetInput().GetActionName(action));
        }
        bool LoadTileMap(TileMap& tilemap, const std::string_view path) override
        {
            return tilemap.LoadMap(path, _app.GetTextures(), Sampler("nearest"), Sampler("linear"));
        }

    private:
        Scene&       _scene;
        Application& _app;
    };

    // The texture and sprite sheet paths entity components name.
    void PreloadComponents(const nlohmann::json& components, ScenePreloader& preload)
    {
        for (const nlohmann::json& component : components) {
            if (!component.is_object()) continue;
            if (const auto it = component.find("texture"); it != component.end() && it->is_string())
                preload.AddTexture(it->get<std::string>());
            if (const auto it = component.find("sheet"); it != component.end() && it->is_string())
                preload.AddSpriteSheet(it->get<std::string>());
        }
    }

    // Every entity's own textures and sheets. A prefab copy's prefab's load
    // in OnLoad: the prefabs are the app's, out of reach on the loader thread.
    void PreloadComponentAssets(const nlohmann::json& doc, ScenePreloader& preload)
    {
        const auto entities = doc.find("entities");
        if (entities == doc.end() || !entities->is_object()) return;
        for (const nlohmann::json& entity : *entities)
            if (entity.is_object()) PreloadComponents(entity, preload);
    }

    // The optional "load" block: measured stage shares for the progress bar,
    // and the textures they were measured with. A texture list that no longer
    // matches the scene means the shares are stale too - still used (they're
    // only a hint), but worth re-measuring.
    void ApplyLoadHints(const nlohmann::json& load, ScenePreloader& preload, const std::string& path)
    {
        if (const auto shares = load.find("shares"); shares != load.end() && shares->is_object())
            preload.SetShares(shares->value("prepare", 0.0f), shares->value("decode", 0.0f), shares->value("build", 0.0f));

        const auto listed = load.find("textures");
        if (listed == load.end() || !listed->is_array()) return;
        std::unordered_set<std::string> expected;
        for (const nlohmann::json& t : *listed)
            if (t.is_string()) expected.insert(TextureManager::NormalizePath(t.get<std::string>()));
        const std::vector<std::string>& named = preload.GetTexturePaths();
        if (expected != std::unordered_set<std::string>(named.begin(), named.end()))
            LOG_WARN(Engine, "FileScene: '{}' has stale load data (its textures changed) - re-save it in the editor",
                     path);
    }

    std::optional<Color> ReadAmbient(const nlohmann::json& doc)
    {
        const auto render = doc.find("render");
        if (render == doc.end() || !render->contains("ambient")) return std::nullopt;
        const nlohmann::json& c = render->at("ambient");
        return Color{ c.at(0).get<f32>(), c.at(1).get<f32>(), c.at(2).get<f32>(), c.size() > 3 ? c.at(3).get<f32>() : 1.0f };
    }
} // namespace

std::unique_ptr<SceneAssets> MakeAppSceneAssets(Scene& scene) { return std::make_unique<AppSceneAssets>(scene); }

void FileScene::OnPrepare(ScenePreloader& preload)
{
    _parsed = SceneFile::ReadFile(_path);
    if (!_parsed) return;
    if (const auto map = _parsed->find("tilemap"); map != _parsed->end() && map->is_string())
        preload.AddTileMap(map->get<std::string>());
    PreloadComponentAssets(*_parsed, preload);
    if (const auto load = _parsed->find("load"); load != _parsed->end() && load->is_object())
        ApplyLoadHints(*load, preload, _path);
}

void FileScene::OnLoad()
{
    std::optional<nlohmann::json> doc = _parsed ? std::move(_parsed) : SceneFile::ReadFile(_path);
    _parsed.reset();
    if (!doc) return;

    AppSceneAssets assets(*this);
    _loaded = SceneFile::Load(*this, *doc, GetApp().GetSceneRegistry(), assets, _ids, &GetApp().GetPrefabs(), &_prefabLinks);
    if (!_loaded) {
        LOG_ERROR(Engine, "FileScene: '{}' failed to load", _path);
        return;
    }
    try {
        _ambient = ReadAmbient(*doc);
        if (const auto music = doc->find("music"); music != doc->end() && music->is_object())
            SetMusic(music->value("path", std::string{}), music->value("loop", true),
                     music->value("category", std::string("music")));
        if (const auto script = doc->find("script"); script != doc->end() && script->is_string())
            SetScript(script->get<std::string>());
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "FileScene: '{}' has malformed render / music settings - {}", _path, e.what());
        _loaded = false;
        return;
    }
    doc->erase("entities"); // the rest is written back as-is by Save
    _document = std::move(*doc);
}

void FileScene::OnRender(FramePacket& packet)
{
    if (_ambient) packet.AmbientColor = *_ambient;
    if (PhysicsWorld& physics = GetPhysics(); physics.GetDebugDraw())
        physics.CollectDebugDraw(packet, GetApp().GetWhiteTexture(),
                                 GetApp().GetStockSampler(Application::StockSampler::Nearest));
}

Entity FileScene::FindEntity(const std::string_view id) const
{
    const auto it = _ids.find(std::string(id));
    return it != _ids.end() ? it->second : Entity::Invalid();
}

bool FileScene::Save(const std::string_view path)
{
    AppSceneAssets       assets(*this);
    const nlohmann::json doc = SceneFile::Save(*this, GetApp().GetSceneRegistry(), assets, _document, _ids, &_prefabLinks);
    return SceneFile::WriteFile(path.empty() ? std::string_view(_path) : path, doc);
}
} // namespace Arcbit
