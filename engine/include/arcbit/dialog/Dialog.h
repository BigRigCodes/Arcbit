#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/save/SaveData.h>

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// A conversation, read from an .arcdialog file (docs/dialog.md):
//
//   { "speaker": "elder",
//     "speakers": { "elder": { "name": "Elder Maren", "portrait": "...", "voice": "..." } },
//     "nodes": {
//       "greet": [
//         "Welcome, traveler.",
//         { "say": "Back again?", "if": { "met_elder": true } },
//         { "choice": [ { "id": "map", "text": "A map, please. (5 gold)", "if": { "gold": [">=", 5] },
//                         "add": { "gold": -5 }, "goto": "map" },
//                       { "id": "bye", "text": "Goodbye.", "goto": "end" } ] } ],
//       "map": [ { "say": "Here. Mind the cliffs.", "set": { "has_map": true } } ] } }
//
// A node is a list of steps run in order: a line ("say"), a choice, a
// branch, or just actions. Any step can carry an "if" (skipped unless it
// holds), actions (set, add, event, lua, call) and a "goto" (a node, or "end").
// Running off a node's end ends the conversation.
//
// The English text lives in the file. Every line, option and speaker name
// gets a Loc key from its place - dialog.<id>.<node>.<n>, or .<step id> - or
// an explicit "key", and DialogLibrary adds the English to Loc, so the
// translations go in the usual locale files.
// ---------------------------------------------------------------------------

enum class DialogCompare : u8 { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

// One test of a SaveData value. A missing key reads as the value's kind's
// zero: false, 0 or "".
struct DialogTest
{
    std::string   Key;
    DialogCompare Op = DialogCompare::Equal;
    SaveValue     Value;
};

// Items and how many, for "has", "give" and "take".
using DialogItems = std::vector<std::pair<std::string, u32>>;

// An "if": every test holds, the party has every item in Has, at least one
// of Any (when there are any), none of Not, and the Lua expression is true
// (when there is one).
struct DialogCondition
{
    std::vector<DialogTest>      All;
    DialogItems                  Has;
    std::vector<DialogCondition> Any;
    std::vector<DialogCondition> Not;
    std::string                  Lua;

    [[nodiscard]] bool IsEmpty() const { return All.empty() && Has.empty() && Any.empty() && Not.empty() && Lua.empty(); }
};

// What a step or an option does when it runs, in this order.
struct DialogActions
{
    std::vector<std::pair<std::string, SaveValue>> Set;
    std::vector<std::pair<std::string, SaveValue>> Add;   // numbers, added to what's there
    std::string                                    Event; // published with Payload
    std::vector<std::pair<std::string, SaveValue>> Payload;
    std::string                                    Lua;   // a statement
    std::string                                    Call;  // a method on the speaker entity's script
    DialogItems                                    Give;  // into the party's bag
    DialogItems                                    Take;  // out of it

    [[nodiscard]] bool IsEmpty() const
    {
        return Set.empty() && Add.empty() && Event.empty() && Lua.empty() && Call.empty() && Give.empty() && Take.empty();
    }
};

// A choice's option, or one arm of a branch (which has no text).
struct DialogOption
{
    std::string     Id;      // what the dialog's end reports as the choice ("id")
    std::string     Key;     // Loc key of the text
    std::string     English;
    std::string     Goto;    // empty: carry on after the choice
    DialogCondition If;      // hidden unless it holds
    DialogActions   Actions;
    bool            Cancel = false; // "cancel": what backing out (Esc) picks
};

enum class DialogStepKind : u8 { Say, Choice, Branch, Do };

struct DialogStep
{
    DialogStepKind  Kind = DialogStepKind::Do;
    DialogCondition If;
    DialogActions   Actions;
    std::string     Goto; // after the step: a node, "end", or empty for the next step

    // Say
    std::string Speaker; // a speakers id; empty: narration
    std::string Key;
    std::string English;
    std::string Portrait; // overrides the speaker's for this line

    // Choice and Branch
    std::vector<DialogOption> Options;
};

struct DialogNode
{
    std::string             Id;
    std::vector<DialogStep> Steps;
};

struct DialogSpeaker
{
    std::string Id;
    std::string Key; // the name's Loc key
    std::string English;
    std::string Portrait;
    std::string Voice; // a sound played as the speaker's text appears
};

struct Dialog
{
    static constexpr std::string_view EndNode = "end"; // "goto": "end"

    std::string                Id;    // its path in the dialog folder: "elder", "town/guard"
    std::string                Start; // the first node
    bool                       PauseWorld = false; // "world": "pause"; default "run"
    std::optional<bool>        Cancelable; // "cancelable": backing out of a line ends it; unset: the project's
    std::vector<DialogSpeaker> Speakers;
    std::vector<DialogNode>    Nodes; // in file order

    [[nodiscard]] const DialogNode*    FindNode(std::string_view id) const;
    [[nodiscard]] const DialogSpeaker* FindSpeaker(std::string_view id) const;

    // Every Loc key with its English: names, lines, options.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> GetStrings() const;
};

// Something wrong with a dialog file. Errors are mistakes the conversation
// trips over (a goto to no node); warnings are likely ones (a node nothing
// leads to, a misspelled field).
struct DialogIssue
{
    bool        Error = true;
    std::string Message;
};

// Read a dialog from its JSON, checking it as it goes. A dialog with errors
// still loads, as far as it can; nullopt only if it has no nodes at all.
[[nodiscard]] std::optional<Dialog> ParseDialog(const nlohmann::ordered_json& json, std::string_view id,
                                                std::vector<DialogIssue>& issues);
[[nodiscard]] std::optional<Dialog> LoadDialog(const std::filesystem::path& file, std::string_view id,
                                               std::vector<DialogIssue>& issues);

// The Loc key prefix of a dialog id: "town/guard" -> "dialog.town.guard".
[[nodiscard]] std::string DialogKeyPrefix(std::string_view id);
} // namespace Arcbit
