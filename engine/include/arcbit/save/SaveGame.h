#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/save/SaveData.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Arcbit
{
// What a slot shows before it's loaded: the slot list's line for it.
struct SaveMetadata
{
    std::string Name;           // display name; the UI's to fill ("Village - Day 3")
    std::string Scene;          // the scene the player was in
    i64         SavedAt  = 0;   // unix time, seconds
    f64         Playtime = 0.0; // seconds of unpaused play

    bool operator==(const SaveMetadata& other) const = default;
};

// ---------------------------------------------------------------------------
// SaveGame - one save, as a document: versions, metadata, the game's
// SaveData, and a section per save participant (inventory, quests, and in a
// later slice the world state).
//
//   {
//       "format": 1,          // the engine's save format
//       "game_version": 3,    // the game's own save version (migrations)
//       "meta":     { "name": "...", "scene": "Village", "saved_at": 1760000000, "playtime": 4521.5 },
//       "data":     { "gold": 120, "quest.dragon": "started" },
//       "sections": { "inventory": { ... } }
//   }
// ---------------------------------------------------------------------------
struct SaveGame
{
    static constexpr u32 FormatVersion = 1;

    u32            GameVersion = 1;
    SaveMetadata   Meta;
    SaveData       Data;
    nlohmann::json Sections = nlohmann::json::object(); // participant name -> its JSON

    [[nodiscard]] nlohmann::json ToJson() const;

    // Null (and logged) if the document isn't a save this build can read: not
    // an object, or a newer format. Migrating an older game version is
    // SaveManager's job, before this.
    [[nodiscard]] static std::optional<SaveGame> FromJson(const nlohmann::json& json);
};
} // namespace Arcbit
