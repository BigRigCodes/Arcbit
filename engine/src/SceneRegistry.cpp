#include <arcbit/scene/SceneRegistry.h>

#include <algorithm>

namespace Arcbit
{
void SceneRegistry::Add(ComponentEntry entry)
{
    const auto it = std::ranges::find(_components, entry.Name, &ComponentEntry::Name);
    if (it != _components.end()) *it = std::move(entry);
    else _components.push_back(std::move(entry));
}

void SceneRegistry::RegisterSystem(std::string name, InstallFn install)
{
    const auto it = std::ranges::find(_systems, name, &std::pair<std::string, InstallFn>::first);
    if (it != _systems.end()) it->second = std::move(install);
    else _systems.emplace_back(std::move(name), std::move(install));
}

const SceneRegistry::ComponentEntry* SceneRegistry::FindComponent(const std::string_view name) const
{
    const auto it = std::ranges::find(_components, name, &ComponentEntry::Name);
    return it != _components.end() ? &*it : nullptr;
}

const SceneRegistry::InstallFn* SceneRegistry::FindSystem(const std::string_view name) const
{
    const auto it = std::ranges::find(_systems, name, &std::pair<std::string, InstallFn>::first);
    return it != _systems.end() ? &it->second : nullptr;
}

void DropDefaults(nlohmann::json& fields, const nlohmann::json& defaults)
{
    for (auto it = fields.begin(); it != fields.end();) {
        const auto d = defaults.find(it.key());
        if (d != defaults.end() && *d == *it) it = fields.erase(it);
        else ++it;
    }
}
} // namespace Arcbit
