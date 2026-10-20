#include <arcbit/input/BindingUpdate.h>

#include <arcbit/core/Loc.h>
#include <arcbit/input/InputManager.h>

#include <algorithm>

namespace Arcbit
{
namespace
{
bool IsGamepad(const Binding& b)
{
    return b.BindingType == Binding::Type::GamepadButton || b.BindingType == Binding::Type::GamepadAxis;
}

bool Contains(const std::vector<Binding>& list, const Binding& b) { return std::ranges::find(list, b) != list.end(); }

bool SameSet(const std::vector<Binding>& a, const std::vector<Binding>& b)
{
    return a.size() == b.size() && std::ranges::all_of(a, [&b](const Binding& x) { return Contains(b, x); });
}

const std::vector<Binding>& DefaultsOf(const BindingMergeInput& in, const std::string& action)
{
    static const std::vector<Binding> none;
    const auto it = std::ranges::find(in.Defaults, action, &std::pair<std::string, std::vector<Binding>>::first);
    return it != in.Defaults.end() ? it->second : none;
}

// The action already holding b, if any.
const std::string* HolderOf(const ActionBindingMap& placed, const Binding& b)
{
    for (const auto& [action, bindings] : placed)
        if (Contains(bindings, b)) return &action;
    return nullptr;
}

// A reset applies once: the default changed since the player's file was saved.
bool ResetDue(const BindingMergeInput& in, const std::string& action, const std::vector<Binding>& defaults)
{
    if (!in.Resets.contains(action) || !in.SavedDefaults) return false;
    const auto was = in.SavedDefaults->find(action);
    return was != in.SavedDefaults->end() && !SameSet(was->second, defaults);
}

// The update moved the holder's default off `taken` and it kept `taken`:
// its new default, if free and on taken's device, is what the update freed
// for the newcomer. (An older file doesn't say what the defaults were; then
// any default the holder doesn't have counts.)
std::optional<Binding> FreedFor(const BindingMergeInput& in, const ActionBindingMap& placed, const std::string& holder,
                                const Binding& taken)
{
    const std::vector<Binding>& now = DefaultsOf(in, holder);
    if (Contains(now, taken)) return std::nullopt; // still its default: nothing moved
    if (in.SavedDefaults) {
        const auto was = in.SavedDefaults->find(holder);
        if (was == in.SavedDefaults->end() || !Contains(was->second, taken)) return std::nullopt; // the player's choice
    }
    const std::vector<Binding>& has = placed.at(holder);
    for (const Binding& b : now)
        if (SameDevice(b, taken) && !Contains(has, b) && !HolderOf(placed, b)) return b;
    return std::nullopt;
}

BindingChange Place(const BindingMergeInput& in, const std::string& action, const BindingChange::Kind what,
                    ActionBindingMap& placed)
{
    BindingChange         change{ .Action = action, .What = what };
    std::vector<Binding>& mine = placed[action]; // map references stay valid as others are added
    for (const Binding& b : DefaultsOf(in, action)) {
        const std::string* holder = HolderOf(placed, b);
        if (!holder) {
            mine.push_back(b);
            continue;
        }
        if (*holder == action) continue; // a default listed twice
        change.Taken.emplace_back(b, *holder);
        if (const auto freed = FreedFor(in, placed, *holder, b)) mine.push_back(*freed);
    }
    if (mine.empty() && what == BindingChange::Kind::Reset) // nowhere to go: better its old keys than none
        for (const Binding& b : in.Saved.at(action))
            if (!HolderOf(placed, b)) mine.push_back(b);
    change.Got = mine;
    return change;
}
} // namespace

bool SameDevice(const Binding& a, const Binding& b) { return IsGamepad(a) == IsGamepad(b); }

std::string DescribeBindingChange(const BindingChange& change, const InputManager& input)
{
    const auto name = [&input](const std::string& action) {
        const std::string_view display = input.GetActionDisplayName(MakeAction(action));
        return display.empty() ? action : Loc::Get(display); // display names may be keys
    };
    const std::string action = name(change.Action);
    if (change.Got.empty()) return Loc::Format("ui.bindings_notice.unbound", { { "action", action } });

    std::string keys;
    for (const Binding& b : change.Got) keys += (keys.empty() ? "" : ", ") + InputManager::BindingToString(b);
    const char* key  = change.What == BindingChange::Kind::New ? "ui.bindings_notice.new" : "ui.bindings_notice.reset";
    std::string line = Loc::Format(key, { { "action", action }, { "keys", keys } });
    for (const auto& [binding, holder] : change.Taken)
        line += " " + Loc::Format("ui.bindings_notice.taken",
                                  { { "keys", InputManager::BindingToString(binding) }, { "action", name(holder) } });
    return line;
}

BindingMergeResult MergeBindings(const BindingMergeInput& in)
{
    BindingMergeResult out;
    std::vector<std::pair<const std::string*, BindingChange::Kind>> pending; // placed after every kept one
    for (const auto& [action, defaults] : in.Defaults) {
        const auto saved = in.Saved.find(action);
        if (saved == in.Saved.end()) pending.emplace_back(&action, BindingChange::Kind::New);
        else if (ResetDue(in, action, defaults)) pending.emplace_back(&action, BindingChange::Kind::Reset);
        else out.Bindings[action] = saved->second;
    }
    for (const auto& [action, what] : pending) out.Changes.push_back(Place(in, *action, what, out.Bindings));
    return out;
}
} // namespace Arcbit
