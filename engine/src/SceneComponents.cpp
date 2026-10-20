// JSON read / write for the engine's built-in components, as they appear in
// .arcscene files. Field keys are snake_case; rotations are radians. Writers
// write every field - SceneRegistry drops the ones equal to the defaults.

#include <arcbit/scene/SceneRegistry.h>

#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/ecs/Components.h>
#include <arcbit/ecs/Interaction.h>
#include <arcbit/ecs/Movement.h>
#include <arcbit/physics/Collider2D.h>
#include <arcbit/physics/CollisionLayers.h>
#include <arcbit/scene/SceneAssets.h>
#include <arcbit/script/Script.h>

#include <array>
#include <bit>
#include <utility>

namespace Arcbit
{
namespace
{
    using nlohmann::json;

    template<typename T>
    void Read(const json& j, const char* key, T& out)
    {
        if (const auto it = j.find(key); it != j.end()) out = it->get<T>();
    }

    json ToJson(const Vec2 v) { return json::array({ v.X, v.Y }); }
    json ToJson(const Color c) { return json::array({ c.R, c.G, c.B, c.A }); }

    void Read(const json& j, const char* key, Vec2& out)
    {
        const auto it = j.find(key);
        if (it == j.end()) return;
        out = { it->at(0).get<f32>(), it->at(1).get<f32>() };
    }

    void Read(const json& j, const char* key, Color& out)
    {
        const auto it = j.find(key);
        if (it == j.end()) return;
        out = { it->at(0).get<f32>(), it->at(1).get<f32>(), it->at(2).get<f32>(), it->size() > 3 ? it->at(3).get<f32>() : 1.0f };
    }

    // ---- Collision layers: engine bits by name, game bits as numbers -------

    constexpr std::array<std::pair<const char*, u32>, 9> LayerNames = { {
        { "Default", CollisionLayers::Default }, { "Player", CollisionLayers::Player },
        { "NPC", CollisionLayers::NPC },         { "Enemy", CollisionLayers::Enemy },
        { "Wall", CollisionLayers::Wall },       { "Prop", CollisionLayers::Prop },
        { "Pickup", CollisionLayers::Pickup },   { "Projectile", CollisionLayers::Projectile },
        { "Trigger", CollisionLayers::Trigger },
    } };

    json LayerBitToJson(const u32 bit)
    {
        for (const auto& [name, value] : LayerNames)
            if (value == bit) return name;
        return bit;
    }

    // A single bit's name, or the bits as a list ("all" for every bit).
    json LayersToJson(const u32 bits, const bool asList)
    {
        if (bits == CollisionLayers::AllLayers) return "all";
        if (!asList && std::popcount(bits) == 1) return LayerBitToJson(bits);
        json list = json::array();
        for (u32 i = 0; i < 32; ++i)
            if (bits & (1u << i)) list.push_back(LayerBitToJson(1u << i));
        return list;
    }

    u32 LayersFromJson(const json& j)
    {
        if (j.is_number()) return j.get<u32>();
        if (j.is_array()) {
            u32 bits = 0;
            for (const json& item : j) bits |= LayersFromJson(item);
            return bits;
        }
        const std::string name = j.get<std::string>();
        if (name == "all") return CollisionLayers::AllLayers;
        for (const auto& [layerName, value] : LayerNames)
            if (name == layerName) return value;
        throw std::invalid_argument("unknown collision layer '" + name + "'");
    }

    // ---- Components ---------------------------------------------------------

    void RegisterCoreComponents(SceneRegistry& r)
    {
        r.RegisterComponent<Tag>(
            "Tag", [](Tag& c, const json& j, SceneAssets&) { Read(j, "value", c.Value); },
            [](const Tag& c, json& j, const SceneAssets&) { j["value"] = c.Value; });
        r.RegisterComponent<Name>(
            "Name", [](Name& c, const json& j, SceneAssets&) { Read(j, "value", c.Value); },
            [](const Name& c, json& j, const SceneAssets&) { j["value"] = c.Value; });
        r.RegisterComponent<Transform2D>(
            "Transform2D",
            [](Transform2D& c, const json& j, SceneAssets&) {
                Read(j, "position", c.Position);
                Read(j, "scale", c.Scale);
                Read(j, "rotation", c.Rotation);
            },
            [](const Transform2D& c, json& j, const SceneAssets&) {
                j["position"] = ToJson(c.Position);
                j["scale"]    = ToJson(c.Scale);
                j["rotation"] = c.Rotation;
            });
        r.RegisterComponent<Disabled>("Disabled", [](Disabled&, const json&, SceneAssets&) {},
                                      [](const Disabled&, json&, const SceneAssets&) {});
        r.RegisterComponent<Lifetime>(
            "Lifetime", [](Lifetime& c, const json& j, SceneAssets&) { Read(j, "remaining", c.Remaining); },
            [](const Lifetime& c, json& j, const SceneAssets&) { j["remaining"] = c.Remaining; });
        r.RegisterComponent<Persistent>(
            "Persistent",
            [](Persistent& c, const json& j, SceneAssets&) {
                Read(j, "id", c.Id);
                Read(j, "enabled", c.Enabled);
                Read(j, "components", c.Components);
            },
            [](const Persistent& c, json& j, const SceneAssets&) {
                j["id"]         = c.Id;
                j["enabled"]    = c.Enabled;
                j["components"] = c.Components;
            });
        r.RegisterComponent<Interactor>(
            "Interactor",
            [](Interactor& c, const json& j, SceneAssets&) {
                Read(j, "action", c.Action);
                Read(j, "reach", c.Reach);
            },
            [](const Interactor& c, json& j, const SceneAssets&) {
                j["action"] = c.Action;
                j["reach"]  = c.Reach;
            });
        r.RegisterComponent<Interactable>(
            "Interactable",
            [](Interactable& c, const json& j, SceneAssets&) {
                Read(j, "prompt", c.Prompt);
                Read(j, "dialog", c.Dialog);
                Read(j, "dialog_node", c.DialogNode);
            },
            [](const Interactable& c, json& j, const SceneAssets&) {
                j["prompt"] = c.Prompt;
                if (!c.Dialog.empty()) j["dialog"] = c.Dialog;
                if (!c.DialogNode.empty()) j["dialog_node"] = c.DialogNode;
            });
        r.RegisterComponent<Script>(
            "Script",
            [](Script& c, const json& j, SceneAssets&) {
                Read(j, "script", c.Module);
                if (const auto it = j.find("properties"); it != j.end() && it->is_object()) c.Properties = *it;
                if (const auto it = j.find("saved"); it != j.end() && it->is_object()) c.Saved = *it;
            },
            [](const Script& c, json& j, const SceneAssets&) {
                j["script"]     = c.Module;
                j["properties"] = c.Properties;
                j["saved"]      = c.Saved;
            });
        r.RegisterComponent<EntityEvents>(
            "EntityEvents",
            [](EntityEvents& c, const json& j, SceneAssets&) {
                Read(j, "spawned", c.Spawned);
                Read(j, "destroyed", c.Destroyed);
                Read(j, "spawned_event", c.SpawnedEvent);
                Read(j, "destroyed_event", c.DestroyedEvent);
            },
            [](const EntityEvents& c, json& j, const SceneAssets&) {
                j["spawned"]         = c.Spawned;
                j["destroyed"]       = c.Destroyed;
                j["spawned_event"]   = c.SpawnedEvent;
                j["destroyed_event"] = c.DestroyedEvent;
            });
    }

    void ReadSprite(SpriteRenderer& c, const json& j, SceneAssets& assets)
    {
        if (const auto it = j.find("texture"); it != j.end()) c.Texture = assets.LoadTexture(it->get<std::string>());
        if (const auto it = j.find("sampler"); it != j.end()) c.Sampler = assets.Sampler(it->get<std::string>());
        if (const auto it = j.find("uv"); it != j.end())
            c.UV = { it->at(0).get<f32>(), it->at(1).get<f32>(), it->at(2).get<f32>(), it->at(3).get<f32>() };
        Read(j, "pivot", c.Pivot);
        Read(j, "tint", c.Tint);
        Read(j, "layer", c.Layer);
        Read(j, "flip_x", c.FlipX);
        if (const auto it = j.find("y_sort"); it != j.end() && it->is_boolean()) c.YSort = it->get<bool>();
    }

    void WriteSprite(const SpriteRenderer& c, json& j, const SceneAssets& assets)
    {
        j["texture"] = assets.TexturePath(c.Texture);
        j["sampler"] = assets.SamplerName(c.Sampler);
        j["uv"]      = json::array({ c.UV.U0, c.UV.V0, c.UV.U1, c.UV.V1 });
        j["pivot"]   = ToJson(c.Pivot);
        j["tint"]    = ToJson(c.Tint);
        j["layer"]   = c.Layer;
        j["flip_x"]  = c.FlipX;
        if (c.YSort) j["y_sort"] = *c.YSort; // unset stays unset: the project decides
    }

    void ReadAnimator(Animator& c, const json& j, SceneAssets& assets)
    {
        if (const auto it = j.find("sheet"); it != j.end()) c.Sheet = assets.LoadSpriteSheet(it->get<std::string>());
        if (const auto it = j.find("clip"); it != j.end() && c.Sheet) c.Clip = c.Sheet->GetAnimation(it->get<std::string>());
        Read(j, "playing", c.Playing);
    }

    void WriteAnimator(const Animator& c, json& j, const SceneAssets& assets)
    {
        j["sheet"]   = assets.SpriteSheetPath(c.Sheet);
        j["clip"]    = c.Clip ? c.Clip->Name : "";
        j["playing"] = c.Playing;
    }

    void RegisterVisualComponents(SceneRegistry& r)
    {
        r.RegisterComponent<SpriteRenderer>("SpriteRenderer", ReadSprite, WriteSprite);
        r.RegisterComponent<Animator>("Animator", ReadAnimator, WriteAnimator);
        r.RegisterComponent<FacingAnimation>(
            "FacingAnimation",
            [](FacingAnimation& c, const json& j, SceneAssets&) {
                Read(j, "idle", c.Idle);
                Read(j, "walk", c.Walk);
                Read(j, "mirror_left", c.MirrorLeft);
            },
            [](const FacingAnimation& c, json& j, const SceneAssets&) {
                j["idle"]        = c.Idle;
                j["walk"]        = c.Walk;
                j["mirror_left"] = c.MirrorLeft;
            });
        r.RegisterComponent<LightEmitter>(
            "LightEmitter",
            [](LightEmitter& c, const json& j, SceneAssets&) {
                Read(j, "radius", c.Radius);
                Read(j, "intensity", c.Intensity);
                Read(j, "color", c.LightColor);
                Read(j, "casts_shadows", c.CastsShadows);
            },
            [](const LightEmitter& c, json& j, const SceneAssets&) {
                j["radius"]        = c.Radius;
                j["intensity"]     = c.Intensity;
                j["color"]         = ToJson(c.LightColor);
                j["casts_shadows"] = c.CastsShadows;
            });
        r.RegisterComponent<Parallax>(
            "Parallax", [](Parallax& c, const json& j, SceneAssets&) { Read(j, "scroll_factor", c.ScrollFactor); },
            [](const Parallax& c, json& j, const SceneAssets&) { j["scroll_factor"] = ToJson(c.ScrollFactor); });
        r.RegisterComponent<CameraTarget>(
            "CameraTarget", [](CameraTarget& c, const json& j, SceneAssets&) { Read(j, "lag", c.Lag); },
            [](const CameraTarget& c, json& j, const SceneAssets&) { j["lag"] = c.Lag; });
    }

    void RegisterAudioComponents(SceneRegistry& r)
    {
        r.RegisterComponent<AudioSource>(
            "AudioSource",
            [](AudioSource& c, const json& j, SceneAssets&) {
                Read(j, "path", c.Path);
                Read(j, "volume", c.Volume);
                Read(j, "radius", c.Radius);
                Read(j, "loop", c.Loop);
                Read(j, "playing", c.Playing);
            },
            [](const AudioSource& c, json& j, const SceneAssets&) {
                j["path"]    = c.Path;
                j["volume"]  = c.Volume;
                j["radius"]  = c.Radius;
                j["loop"]    = c.Loop;
                j["playing"] = c.Playing;
            });
    }

    void ReadCollider(Collider2D& c, const json& j, SceneAssets&)
    {
        if (const auto it = j.find("shape"); it != j.end())
            c.Shape = it->get<std::string>() == "circle" ? ColliderShape::Circle : ColliderShape::Box;
        if (const auto it = j.find("kind"); it != j.end())
            c.Kind = it->get<std::string>() == "static" ? BodyKind::Static : BodyKind::Kinematic;
        Read(j, "offset", c.Offset);
        Read(j, "rotation", c.Rotation);
        Read(j, "half_extents", c.HalfExtents);
        Read(j, "radius", c.Radius);
        if (const auto it = j.find("layer"); it != j.end()) c.Layer = LayersFromJson(*it);
        if (const auto it = j.find("mask"); it != j.end()) c.Mask = LayersFromJson(*it);
        Read(j, "is_trigger", c.IsTrigger);
        if (const auto it = j.find("blocked_from"); it != j.end()) {
            c.BlockedFrom.clear();
            for (const json& arc : *it)
                c.BlockedFrom.push_back({ arc.at("center").get<f32>(), arc.at("half_width").get<f32>() });
        }
    }

    void WriteCollider(const Collider2D& c, json& j, const SceneAssets&)
    {
        j["shape"]        = c.Shape == ColliderShape::Circle ? "circle" : "box";
        j["kind"]         = c.Kind == BodyKind::Static ? "static" : "kinematic";
        j["offset"]       = ToJson(c.Offset);
        j["rotation"]     = c.Rotation;
        j["half_extents"] = ToJson(c.HalfExtents);
        j["radius"]       = c.Radius;
        j["layer"]        = LayersToJson(c.Layer, false);
        j["mask"]         = LayersToJson(c.Mask, true);
        j["is_trigger"]   = c.IsTrigger;
        json arcs         = json::array();
        for (const DirectionArc& arc : c.BlockedFrom)
            arcs.push_back({ { "center", arc.CenterDegrees }, { "half_width", arc.HalfWidthDegrees } });
        j["blocked_from"] = arcs;
    }

    ActionID ReadAction(const json& j, const char* key, const ActionID current)
    {
        const auto it = j.find(key);
        if (it == j.end()) return current;
        return it->is_number() ? it->get<ActionID>() : MakeAction(it->get<std::string>());
    }

    json WriteAction(const ActionID action, const SceneAssets& assets)
    {
        const std::string name = assets.ActionName(action);
        return name.empty() ? json(action) : json(name);
    }

    void RegisterControllerComponents(SceneRegistry& r)
    {
        r.RegisterComponent<InputMovement>(
            "InputMovement",
            [](InputMovement& c, const json& j, SceneAssets&) {
                c.MoveLeft  = ReadAction(j, "move_left", c.MoveLeft);
                c.MoveRight = ReadAction(j, "move_right", c.MoveRight);
                c.MoveUp    = ReadAction(j, "move_up", c.MoveUp);
                c.MoveDown  = ReadAction(j, "move_down", c.MoveDown);
            },
            [](const InputMovement& c, json& j, const SceneAssets& a) {
                j["move_left"]  = WriteAction(c.MoveLeft, a);
                j["move_right"] = WriteAction(c.MoveRight, a);
                j["move_up"]    = WriteAction(c.MoveUp, a);
                j["move_down"]  = WriteAction(c.MoveDown, a);
            });
        r.RegisterComponent<AIMovement>(
            "AIMovement",
            [](AIMovement& c, const json& j, SceneAssets&) {
                Read(j, "target_position", c.TargetPosition);
                Read(j, "has_target", c.HasTarget);
            },
            [](const AIMovement& c, json& j, const SceneAssets&) {
                j["target_position"] = ToJson(c.TargetPosition);
                j["has_target"]      = c.HasTarget;
            });
        r.RegisterComponent<MoveIntent>(
            "MoveIntent", [](MoveIntent& c, const json& j, SceneAssets&) { Read(j, "facing", c.Facing); },
            [](const MoveIntent& c, json& j, const SceneAssets&) { j["facing"] = ToJson(c.Facing); });
    }

    void RegisterStyleComponents(SceneRegistry& r)
    {
        r.RegisterComponent<FreeMovement>(
            "FreeMovement",
            [](FreeMovement& c, const json& j, SceneAssets&) {
                Read(j, "velocity", c.Velocity);
                Read(j, "friction", c.Friction);
                Read(j, "max_speed", c.MaxSpeed);
                Read(j, "speed", c.Speed);
            },
            [](const FreeMovement& c, json& j, const SceneAssets&) {
                j["velocity"]  = ToJson(c.Velocity);
                j["friction"]  = c.Friction;
                j["max_speed"] = c.MaxSpeed;
                j["speed"]     = c.Speed;
            });
        r.RegisterComponent<SmoothTileMovement>(
            "SmoothTileMovement",
            [](SmoothTileMovement& c, const json& j, SceneAssets&) {
                Read(j, "speed", c.Speed);
                Read(j, "turn_delay", c.TurnDelay);
            },
            [](const SmoothTileMovement& c, json& j, const SceneAssets&) {
                j["speed"]      = c.Speed;
                j["turn_delay"] = c.TurnDelay;
            });
        r.RegisterComponent<SnapTileMovement>(
            "SnapTileMovement",
            [](SnapTileMovement& c, const json& j, SceneAssets&) {
                Read(j, "step_interval", c.StepInterval);
                Read(j, "turn_delay", c.TurnDelay);
            },
            [](const SnapTileMovement& c, json& j, const SceneAssets&) {
                j["step_interval"] = c.StepInterval;
                j["turn_delay"]    = c.TurnDelay;
            });
        r.RegisterComponent<Pushable>(
            "Pushable", [](Pushable& c, const json& j, SceneAssets&) { Read(j, "resistance", c.Resistance); },
            [](const Pushable& c, json& j, const SceneAssets&) { j["resistance"] = c.Resistance; });
        r.RegisterComponent<PendingMove>("PendingMove", [](PendingMove&, const json&, SceneAssets&) {},
                                         [](const PendingMove&, json&, const SceneAssets&) {});
    }
} // namespace

void RegisterBuiltinComponents(SceneRegistry& registry)
{
    RegisterCoreComponents(registry);
    RegisterVisualComponents(registry);
    RegisterAudioComponents(registry);
    registry.RegisterComponent<Collider2D>("Collider2D", ReadCollider, WriteCollider);
    RegisterControllerComponents(registry);
    RegisterStyleComponents(registry);
}
} // namespace Arcbit
