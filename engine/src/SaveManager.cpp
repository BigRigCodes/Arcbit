#include <arcbit/save/SaveManager.h>

#include <arcbit/core/Log.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>

namespace Arcbit
{
namespace
{
    std::filesystem::path WithExtension(std::filesystem::path path, const char* extension)
    {
        path += extension;
        return path;
    }

    std::optional<nlohmann::json> ParseFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;
        nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded()) return std::nullopt;
        return json;
    }
} // namespace

// ---------------------------------------------------------------------------
// What gets saved
// ---------------------------------------------------------------------------

void SaveManager::RegisterParticipant(std::string name, WriteFn write, ReadFn read)
{
    _participants.insert_or_assign(std::move(name), Participant{ std::move(write), std::move(read) });
}

void SaveManager::RegisterMigration(const u32 fromVersion, Migration step)
{
    _migrations.insert_or_assign(fromVersion, std::move(step));
}

SaveGame SaveManager::Capture(SaveMetadata meta) const
{
    SaveGame game;
    game.GameVersion = CurrentGameVersion;
    game.Meta        = std::move(meta);
    game.Data        = _data;
    for (const auto& [name, participant] : _participants) {
        nlohmann::json section = nlohmann::json::object();
        if (participant.Write) participant.Write(section);
        game.Sections[name] = std::move(section);
    }
    return game;
}

void SaveManager::Restore(const SaveGame& game)
{
    _data = game.Data;
    for (const auto& [name, participant] : _participants) {
        if (!participant.Read) continue;
        const auto it = game.Sections.find(name);
        participant.Read(it != game.Sections.end() ? *it : nlohmann::json::object());
    }
}

// ---------------------------------------------------------------------------
// Slots on disk
// ---------------------------------------------------------------------------

std::filesystem::path SaveManager::PathOf(const SaveSlot slot) const
{
    switch (slot.Type) {
        case SaveSlot::Kind::Auto: return _directory / std::format("auto_{}.arcsave", slot.Index);
        case SaveSlot::Kind::Quick: return _directory / "quick.arcsave";
        default: return _directory / std::format("slot_{:02}.arcsave", slot.Index);
    }
}

// The save goes to a temp file; the slot's current save becomes its .bak;
// then the temp file is renamed into place. A crash at any point leaves
// either the new save or the old one (as .bak) readable.
bool SaveManager::Write(const SaveSlot slot, const SaveGame& game)
{
    const std::filesystem::path path = PathOf(slot);
    const std::filesystem::path temp = WithExtension(path, ".tmp");
    const std::filesystem::path bak  = WithExtension(path, ".bak");
    std::error_code             ec;
    std::filesystem::create_directories(_directory, ec);
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << game.ToJson().dump(2);
        if (!file.flush()) {
            LOG_ERROR(Engine, "SaveManager: can't write '{}'", temp.string());
            return false;
        }
    }
    if (std::filesystem::exists(path)) std::filesystem::rename(path, bak, ec);
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        LOG_ERROR(Engine, "SaveManager: can't put '{}' in place - {}", path.string(), ec.message());
        return false;
    }
    LOG_INFO(Engine, "Saved '{}'", path.string());
    return true;
}

// The slot's file, or - when it's missing or won't parse (a crash between
// Write's renames, a damaged file) - its backup. A save that parses but is
// refused (a newer game, a missing migration) doesn't fall back: an older
// backup loading instead would silently lose progress.
std::optional<SaveGame> SaveManager::Read(const SaveSlot slot) const
{
    std::filesystem::path         from = PathOf(slot);
    std::optional<nlohmann::json> json = ParseFile(from);
    if (!json) {
        const std::filesystem::path bak = WithExtension(from, ".bak");
        json                            = ParseFile(bak);
        if (!json) return std::nullopt;
        LOG_WARN(Engine, "SaveManager: '{}' missing or unreadable - loading its backup", from.string());
        from = bak;
    }
    if (!Migrate(*json, from)) return std::nullopt;
    return SaveGame::FromJson(*json);
}

// Apply the registered steps from the save's game version up to the current
// one. A save from a newer game, or a gap in the steps, isn't loaded.
bool SaveManager::Migrate(nlohmann::json& save, const std::filesystem::path& path) const
{
    if (!save.is_object()) return false;
    u32 version = save.value("game_version", 1u);
    if (version > CurrentGameVersion) {
        LOG_ERROR(Engine, "SaveManager: '{}' is from a newer game (save version {}, this is {})", path.string(),
                  version, CurrentGameVersion);
        return false;
    }
    for (; version < CurrentGameVersion; ++version) {
        const auto step = _migrations.find(version);
        if (step == _migrations.end()) {
            LOG_ERROR(Engine, "SaveManager: no migration from save version {} for '{}'", version, path.string());
            return false;
        }
        step->second(save);
        save["game_version"] = version + 1;
    }
    return true;
}

bool SaveManager::Exists(const SaveSlot slot) const
{
    std::error_code ec;
    return std::filesystem::exists(PathOf(slot), ec) || std::filesystem::exists(WithExtension(PathOf(slot), ".bak"), ec);
}

bool SaveManager::Delete(const SaveSlot slot)
{
    std::error_code ec;
    const bool      removed = std::filesystem::remove(PathOf(slot), ec);
    std::filesystem::remove(WithExtension(PathOf(slot), ".bak"), ec);
    return removed;
}

std::vector<SaveSlot> SaveManager::AllSlots() const
{
    std::vector<SaveSlot> slots;
    for (u32 i = 1; i <= ManualSlots; ++i) slots.push_back(SaveSlot::Manual(i));
    for (u32 i = 1; i <= AutoSlots; ++i) slots.push_back(SaveSlot::Auto(i));
    slots.push_back(SaveSlot::Quick());
    return slots;
}

std::vector<SaveSlotInfo> SaveManager::List() const
{
    std::vector<SaveSlotInfo> infos;
    for (const SaveSlot slot : AllSlots())
        if (Exists(slot))
            if (const std::optional<SaveGame> game = Read(slot)) infos.push_back({ slot, game->Meta });
    std::ranges::stable_sort(infos, [](const SaveSlotInfo& a, const SaveSlotInfo& b) { return a.Meta.SavedAt > b.Meta.SavedAt; });
    return infos;
}

SaveSlot SaveManager::NextAutoSlot() const
{
    SaveSlot oldest     = SaveSlot::Auto(1);
    i64      oldestTime = std::numeric_limits<i64>::max();
    for (u32 i = 1; i <= AutoSlots; ++i) {
        const SaveSlot slot = SaveSlot::Auto(i);
        if (!Exists(slot)) return slot;
        const std::optional<SaveGame> game = Read(slot);
        const i64                     time = game ? game->Meta.SavedAt : std::numeric_limits<i64>::min(); // unreadable: reuse first
        if (time < oldestTime) {
            oldestTime = time;
            oldest     = slot;
        }
    }
    return oldest;
}
} // namespace Arcbit
