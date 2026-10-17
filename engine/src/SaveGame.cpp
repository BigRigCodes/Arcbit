#include <arcbit/save/SaveGame.h>

#include <arcbit/core/Log.h>

namespace Arcbit
{
nlohmann::json SaveGame::ToJson() const
{
    return {
        { "format", FormatVersion },
        { "game_version", GameVersion },
        { "meta", { { "name", Meta.Name }, { "scene", Meta.Scene }, { "saved_at", Meta.SavedAt }, { "playtime", Meta.Playtime } } },
        { "data", Data.ToJson() },
        { "sections", Sections.is_object() ? Sections : nlohmann::json::object() },
    };
}

std::optional<SaveGame> SaveGame::FromJson(const nlohmann::json& json)
{
    if (!json.is_object()) {
        LOG_ERROR(Engine, "SaveGame: a save must be a JSON object");
        return std::nullopt;
    }
    try {
        const u32 format = json.value("format", 0u);
        if (format < 1 || format > FormatVersion) {
            LOG_ERROR(Engine, "SaveGame: save format {} (this build reads up to {})", format, FormatVersion);
            return std::nullopt;
        }
        SaveGame game;
        game.GameVersion        = json.value("game_version", 1u);
        const nlohmann::json& m = json.value("meta", nlohmann::json::object());
        game.Meta               = { m.value("name", ""), m.value("scene", ""), m.value("saved_at", i64{ 0 }),
                                    m.value("playtime", 0.0) };
        game.Data.FromJson(json.value("data", nlohmann::json::object()));
        game.Sections = json.value("sections", nlohmann::json::object());
        return game;
    }
    catch (const std::exception& e) {
        LOG_ERROR(Engine, "SaveGame: malformed save - {}", e.what());
        return std::nullopt;
    }
}
} // namespace Arcbit
