#pragma once

#include <arcbit/input/InputTypes.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Arcbit
{
class InputManager;

// ---------------------------------------------------------------------------
// Bindings across game updates (docs/input.md): what a player's saved
// bindings become when the game's defaults have changed since they were
// saved. The player's keys always win:
//
//   - An action the player had keeps its bindings, rebound or not, whatever
//     its default is now.
//   - A new action takes each of its default bindings that's free. One that
//     another action holds goes to that action's new default instead, if
//     the update moved that action's default away and it kept the old one,
//     and the new one is free (Interact moved E -> F and new Melee on E:
//     Interact keeps E, Melee gets F). Otherwise it stays unbound.
//   - An action the game resets (Settings::ResetBindingOnUpdate) whose
//     default changed since the save is placed like a new one; if none of
//     its new defaults can go anywhere, it keeps its old bindings.
//
// "Free" compares like bindings: keys and mouse buttons with each other,
// gamepad buttons and axes with each other.
// ---------------------------------------------------------------------------

using ActionBindingMap = std::map<std::string, std::vector<Binding>>;

// What one action got, for the notice.
struct BindingChange
{
    enum class Kind : u8 { New, Reset };

    std::string          Action;
    Kind                 What = Kind::New;
    std::vector<Binding> Got;                                 // its bindings now; empty: none yet
    std::vector<std::pair<Binding, std::string>> Taken;       // defaults it didn't get, and who has them
};

struct BindingMergeInput
{
    // The game's defaults now, in registration order.
    std::vector<std::pair<std::string, std::vector<Binding>>> Defaults;
    ActionBindingMap                  Saved;          // the player's, from the settings file
    std::optional<ActionBindingMap>   SavedDefaults;  // the defaults they were based on; nullopt: an older file
    std::set<std::string>             Resets;         // actions the game puts back to their new default
};

struct BindingMergeResult
{
    ActionBindingMap           Bindings; // every action's bindings to apply
    std::vector<BindingChange> Changes;  // in registration order; empty: nothing to tell
};

[[nodiscard]] BindingMergeResult MergeBindings(const BindingMergeInput& input);

// One line of the notice, localized: "New: Melee is F (E is your
// Interact)", "Melee has no key yet - set it in Controls". Action names are
// the input's display names (Loc keys or text).
[[nodiscard]] std::string DescribeBindingChange(const BindingChange& change, const InputManager& input);

// Whether two bindings are on the same kind of device (keyboard and mouse,
// or gamepad).
[[nodiscard]] bool SameDevice(const Binding& a, const Binding& b);
} // namespace Arcbit
