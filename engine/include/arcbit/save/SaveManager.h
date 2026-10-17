#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/save/SaveGame.h>
#include <arcbit/save/SaveSeal.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
// A save slot: one of the numbered manual slots, a rotating autosave, or the
// quicksave. Index is 1-based for Manual and Auto; unused for Quick.
struct SaveSlot
{
    enum class Kind : u8
    {
        Manual,
        Auto,
        Quick,
    };

    Kind Type  = Kind::Manual;
    u32  Index = 1;

    static SaveSlot Manual(const u32 index) { return { Kind::Manual, index }; }
    static SaveSlot Auto(const u32 index) { return { Kind::Auto, index }; }
    static SaveSlot Quick() { return { Kind::Quick, 0 }; }

    bool operator==(const SaveSlot& other) const = default;
};

// A slot with a save in it, for the slot list.
struct SaveSlotInfo
{
    SaveSlot     Slot;
    SaveMetadata Meta;
};

// ---------------------------------------------------------------------------
// SaveManager - save slots on disk, migrations, and the game's save data.
//
//   saves.Data().Set("gold", 120);
//   saves.Write(SaveSlot::Manual(1), saves.Capture(meta));
//   if (std::optional<SaveGame> game = saves.Read(SaveSlot::Manual(1))) saves.Restore(*game);
//
// Writing is crash-safe: the save goes to a temp file first, the slot's
// previous save becomes its .bak, then the temp file takes the slot's place.
// Reading a slot that's missing or unreadable (a crash mid-write, a damaged
// file) falls back to that .bak.
//
// Migrations: every save records the game's save version. Bump
// CurrentGameVersion when the shape of what you save changes, and register a
// step upgrading the previous version's JSON; reading an older save applies
// the steps in order. A save from a newer game, or one missing a step, isn't
// loaded (logged).
//
// Participants: a system with its own state (inventory, quests) registers
// write / read callbacks under a name and gets a JSON section of the save.
//
// Sealing (SaveSeal): release builds seal every save and refuse plain ones -
// a hand-written JSON save would otherwise be a free cheat. Debug builds
// write plain JSON and read both, so saves stay readable while developing.
// A sealed save that fails its check (edited, damaged, another key) counts
// as unreadable: the slot's backup loads instead. Set the game's own key with
// SetSealKey; without one, a shared engine default is used (logged).
// ---------------------------------------------------------------------------
class SaveManager
{
public:
    using WriteFn   = std::function<void(nlohmann::json& section)>;
    using ReadFn    = std::function<void(const nlohmann::json& section)>;
    using Migration = std::function<void(nlohmann::json& save)>;

    explicit SaveManager(std::filesystem::path directory = "saves") : _directory(std::move(directory)) {}

    // --- Configuration (per game) -------------------------------------------

    u32 ManualSlots        = 10;
    u32 AutoSlots          = 3;
    u32 CurrentGameVersion = 1;

#ifdef ARCBIT_DEBUG
    bool SealSaves        = false; // write plain JSON while developing
    bool AcceptPlainSaves = true;
#else
    bool SealSaves        = true;
    bool AcceptPlainSaves = false;
#endif

    // The key saves are sealed with, from a secret only the game knows (a
    // stand-in until Phase 36's project keys).
    void SetSealKey(std::string_view secret) { _key = SaveSeal::DeriveKey(secret); }

    void                                       SetDirectory(std::filesystem::path directory) { _directory = std::move(directory); }
    [[nodiscard]] const std::filesystem::path& GetDirectory() const { return _directory; }

    // --- What gets saved ----------------------------------------------------

    [[nodiscard]] SaveData&       Data() { return _data; }
    [[nodiscard]] const SaveData& Data() const { return _data; }

    // A system's own section of the save, under `name`. Registering a name
    // again replaces its callbacks.
    void RegisterParticipant(std::string name, WriteFn write, ReadFn read);

    // Upgrade a save written at game version `fromVersion` to fromVersion + 1.
    void RegisterMigration(u32 fromVersion, Migration step);

    // The current state as a save: SaveData, every participant's section,
    // the current game version, and `meta`.
    [[nodiscard]] SaveGame Capture(SaveMetadata meta) const;

    // Put a loaded save back: SaveData replaced, each participant handed its
    // section (an empty object if the save has none - a participant added
    // after the save was made).
    void Restore(const SaveGame& game);

    // --- Slots on disk ------------------------------------------------------

    bool                                  Write(SaveSlot slot, const SaveGame& game);
    [[nodiscard]] std::optional<SaveGame> Read(SaveSlot slot) const;
    [[nodiscard]] bool                    Exists(SaveSlot slot) const;
    bool                                  Delete(SaveSlot slot); // and its .bak

    // Every slot with a save in it, newest first.
    [[nodiscard]] std::vector<SaveSlotInfo> List() const;

    // The autosave to write next: the first empty one, else the oldest.
    [[nodiscard]] SaveSlot NextAutoSlot() const;

    // The slot's file ("slot_01.arcsave", "auto_2.arcsave", "quick.arcsave").
    [[nodiscard]] std::filesystem::path PathOf(SaveSlot slot) const;

private:
    struct Participant
    {
        WriteFn Write;
        ReadFn  Read;
    };

    std::filesystem::path              _directory;
    SaveData                           _data;
    std::map<std::string, Participant> _participants; // sorted: stable section order
    std::map<u32, Migration>           _migrations;   // by the version each upgrades from
    std::optional<SaveSeal::Key>       _key;          // SetSealKey; else the engine default

    [[nodiscard]] bool                  Migrate(nlohmann::json& save, const std::filesystem::path& path) const;
    [[nodiscard]] const SaveSeal::Key&  Key() const;
    [[nodiscard]] std::string           Encode(const SaveGame& game) const; // the file's bytes
    [[nodiscard]] std::optional<nlohmann::json> Decode(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<SaveSlot> AllSlots() const;
};
} // namespace Arcbit
