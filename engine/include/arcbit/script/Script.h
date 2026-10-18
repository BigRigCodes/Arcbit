#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Script - gives an entity Lua behavior (docs/scripting.md). Opt-in: only
// entities with one run any Lua.
//
//   "Script": { "script": "chest", "properties": { "Gold": 25 } }
//
// The module ("chest" -> assets/scripts/chest.lua) returns a table of hooks;
// the entity gets its own instance of it (`self`), with self.Properties (the
// module's Properties, these on top) and self.Saved.
// ---------------------------------------------------------------------------
struct Script
{
    std::string    Module;                                // require name, e.g. "npc.guard"
    nlohmann::json Properties = nlohmann::json::object(); // overrides of the module's Properties
    nlohmann::json Saved      = nlohmann::json::object(); // self.Saved, as of the last capture (saves)
};
} // namespace Arcbit
