// The Entity handle scripts hold. Slice 3 adds the component and scene API;
// here it's the handle itself: identity, validity, equality, tostring.

#include "ScriptState.h"

#include <arcbit/scene/Scene.h>

#include <format>

namespace Arcbit
{
Scene* ScriptEngine::State::Resolve(const ScriptEntity& handle) const
{
    const auto it = Scenes.find(handle.SceneSerial);
    if (it == Scenes.end()) return nullptr; // its scene unloaded
    return it->second->GetWorld().IsValid(handle.Id) ? it->second : nullptr;
}

void ScriptEngine::State::BindEntity()
{
    Lua.new_usertype<ScriptEntity>(
        "Entity", sol::no_constructor,
        "IsValid", [this](const ScriptEntity& e) { return Resolve(e) != nullptr; },
        sol::meta_function::equal_to,
        [](const ScriptEntity& a, const ScriptEntity& b) { return a.Id == b.Id && a.SceneSerial == b.SceneSerial; },
        sol::meta_function::to_string, [this](const ScriptEntity& e) {
            return std::format("Entity({}:{}{})", e.Id.Index, e.Id.Generation, Resolve(e) ? "" : ", gone");
        });
    AddGlobal("Entity", Lua["Entity"]);
}
} // namespace Arcbit
