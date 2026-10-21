#include <arcbit/dialog/DialogRunner.h>

#include <arcbit/core/Log.h>
#include <arcbit/events/EventBus.h>
#include <arcbit/save/SaveData.h>

#include <algorithm>
#include <format>

namespace Arcbit
{
namespace
{
std::optional<f64> Number(const SaveValue& value)
{
    if (const auto* i = std::get_if<i64>(&value)) return static_cast<f64>(*i);
    if (const auto* f = std::get_if<f64>(&value)) return *f;
    return std::nullopt;
}

// What a missing key reads as: the zero of the value it's tested against.
SaveValue Zero(const SaveValue& like)
{
    return std::visit([]<typename V>(const V&) -> SaveValue { return V{}; }, like);
}

bool Compare(const SaveValue& have, const DialogCompare op, const SaveValue& want)
{
    const auto a = Number(have), b = Number(want);
    if (a && b) {
        switch (op) {
            case DialogCompare::Equal: return *a == *b;
            case DialogCompare::NotEqual: return *a != *b;
            case DialogCompare::Less: return *a < *b;
            case DialogCompare::LessEqual: return *a <= *b;
            case DialogCompare::Greater: return *a > *b;
            case DialogCompare::GreaterEqual: return *a >= *b;
        }
    }
    if (have.index() != want.index()) return op == DialogCompare::NotEqual;
    if (op == DialogCompare::Equal) return have == want;
    if (op == DialogCompare::NotEqual) return have != want;
    const auto* x = std::get_if<std::string>(&have); // strings order alphabetically; nothing else orders
    const auto* y = std::get_if<std::string>(&want);
    if (!x || !y) return false;
    switch (op) {
        case DialogCompare::Less: return *x < *y;
        case DialogCompare::LessEqual: return *x <= *y;
        case DialogCompare::Greater: return *x > *y;
        default: return *x >= *y;
    }
}

// Whole numbers stay whole: 3 + 2 is 5, not 5.0.
void Add(SaveData& data, const std::string& key, const SaveValue& delta)
{
    const SaveValue  zero = i64{ 0 };
    const SaveValue* have = data.Find(key);
    const SaveValue& base = have ? *have : zero;
    const auto       a = Number(base), b = Number(delta);
    if (!a || !b) {
        LOG_WARN(Engine, "Dialog: can't add to '{}' - it isn't a number", key);
        return;
    }
    if (std::holds_alternative<i64>(base) && std::holds_alternative<i64>(delta))
        data.Set(key, std::get<i64>(base) + std::get<i64>(delta));
    else data.Set(key, *a + *b);
}

LocArg ToArg(const std::string_view name, const SaveValue& value)
{
    return std::visit(
        [name]<typename V>(const V& v) -> LocArg {
            if constexpr (std::is_same_v<V, bool>) return { name, v ? "true" : "false" };
            else if constexpr (std::is_same_v<V, Vec2>) return { name, std::format("{}, {}", v.X, v.Y) };
            else return { name, v };
        },
        value);
}

// The names of a text's {placeholders} ("{gold}", "{gold:n0}"), not {{ escapes.
std::vector<std::string_view> PlaceholderNames(const std::string_view text)
{
    std::vector<std::string_view> names;
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        if (i + 1 < text.size() && text[i + 1] == '{') {
            ++i;
            continue;
        }
        const usize close = text.find('}', i + 1);
        if (close == std::string_view::npos) break;
        const std::string_view inside = text.substr(i + 1, close - i - 1);
        names.push_back(inside.substr(0, inside.find(':')));
        i = close;
    }
    return names;
}

// A key's value: the host's own first (Read), else SaveData's.
std::optional<SaveValue> ValueOf(const DialogHost& host, const std::string_view key)
{
    if (host.Read)
        if (std::optional<SaveValue> value = host.Read(key)) return value;
    const SaveValue* saved = host.Data ? host.Data->Find(std::string(key)) : nullptr;
    return saved ? std::optional<SaveValue>(*saved) : std::nullopt;
}

bool HasItems(const DialogItems& items, const DialogHost& host)
{
    if (items.empty()) return true;
    if (!host.CountItem) {
        LOG_WARN(Engine, "Dialog: nothing to count items with - \"has\" fails");
        return false;
    }
    return std::ranges::all_of(items, [&host](const auto& item) { return host.CountItem(item.first) >= item.second; });
}
} // namespace

bool DialogRunner::Holds(const DialogCondition& condition, const DialogHost& host)
{
    for (const DialogTest& test : condition.All) {
        const std::optional<SaveValue> value = ValueOf(host, test.Key);
        if (!Compare(value ? *value : Zero(test.Value), test.Op, test.Value)) return false;
    }
    if (!HasItems(condition.Has, host)) return false;
    const auto holds = [&host](const DialogCondition& c) { return Holds(c, host); };
    if (!condition.Any.empty() && std::ranges::none_of(condition.Any, holds)) return false;
    if (std::ranges::any_of(condition.Not, holds)) return false;
    if (condition.Lua.empty()) return true;
    if (!host.Test) {
        LOG_WARN(Engine, "Dialog: no Lua to test '{}' - it fails", condition.Lua);
        return false;
    }
    return host.Test(condition.Lua);
}

void DialogRunner::Start(std::shared_ptr<const Dialog> dialog, DialogHost host, const std::string_view node)
{
    _dialog   = std::move(dialog);
    _host     = std::move(host);
    _line     = {};
    _lineStep = nullptr;
    _step     = 0;
    _state    = State::Idle;
    _choices.clear();
    _lastChoice.clear();
    if (!_dialog) {
        LOG_WARN(Engine, "Dialog: started with no dialog");
        End();
        return;
    }
    const std::string_view start = node.empty() ? std::string_view(_dialog->Start) : node;
    _node                        = _dialog->FindNode(start);
    if (!_node) LOG_ERROR(Engine, "Dialog '{}': there's no node '{}'", _dialog->Id, start);
    Walk();
}

void DialogRunner::Next()
{
    if (_state != State::Line) return;
    Follow(_node->Steps[_step].Goto);
    Walk();
}

// An option without a goto carries on as the choice step would: its goto, else the next step.
void DialogRunner::Choose(const u32 choice)
{
    if (_state != State::Choice || choice >= _choices.size()) return;
    const DialogStep&   step   = _node->Steps[_step];
    const DialogOption& option = step.Options[_choices[choice].Option];
    _lastChoice                = option.Id;
    _choices.clear();
    if (OnChoose) OnChoose(option.Id);
    Act(option.Actions);
    if (_state == State::Ended) return; // an action stopped the dialog
    Follow(option.Goto.empty() ? step.Goto : option.Goto);
    Walk();
}

void DialogRunner::Stop()
{
    if (!IsRunning()) return;
    _lastChoice.clear(); // ended early: no choice made
    End();
}

void DialogRunner::Cancel()
{
    if (!CanCancel()) return;
    if (_state == State::Choice) return Choose(*CancelChoice());
    Stop();
}

bool DialogRunner::CanCancel() const
{
    if (_state == State::Choice) return CancelChoice().has_value();
    return _state == State::Line && _dialog->Cancelable.value_or(CancelableByDefault);
}

std::optional<u32> DialogRunner::CancelChoice() const
{
    if (_state != State::Choice) return std::nullopt;
    const DialogStep& step = _node->Steps[_step];
    for (u32 i = 0; i < _choices.size(); ++i)
        if (step.Options[_choices[i].Option].Cancel) return i;
    return std::nullopt;
}

void DialogRunner::RefreshText()
{
    if (_lineStep) FillLine(*_lineStep);
    if (_state != State::Choice) return;
    for (Choice& c : _choices) c.Text = Text(_node->Steps[_step].Options[c.Option].Key);
}

// Runs steps until one shows a line or a choice, or the dialog ends.
void DialogRunner::Walk()
{
    for (u32 n = 0; n < MaxSteps; ++n) {
        if (_state == State::Ended) return;
        if (!_node || _step >= _node->Steps.size()) return End();
        const DialogStep& step = _node->Steps[_step];
        if (!Holds(step.If, _host)) {
            ++_step;
            continue;
        }
        Act(step.Actions);
        if (_state == State::Ended) return;
        if (step.Kind == DialogStepKind::Say) return ShowLine(step);
        if (step.Kind == DialogStepKind::Choice) {
            if (OfferChoice(step)) return;
            ++_step;
            continue;
        }
        const DialogOption* arm = nullptr; // a branch: the first arm that holds
        if (step.Kind == DialogStepKind::Branch) {
            const auto it = std::ranges::find_if(step.Options, [this](const DialogOption& o) { return Holds(o.If, _host); });
            if (it != step.Options.end()) {
                arm = &*it;
                Act(arm->Actions);
            }
        }
        Follow(arm && !arm->Goto.empty() ? arm->Goto : step.Goto);
    }
    LOG_ERROR(Engine, "Dialog '{}': {} steps without a line or a choice - a loop of gotos? It ends", _dialog->Id, MaxSteps);
    End();
}

void DialogRunner::Follow(const std::string& go)
{
    if (go.empty()) {
        ++_step;
        return;
    }
    _step = 0;
    _node = go == Dialog::EndNode ? nullptr : _dialog->FindNode(go);
    if (!_node && go != Dialog::EndNode) LOG_ERROR(Engine, "Dialog '{}': goto '{}' - there's no such node", _dialog->Id, go);
}

void DialogRunner::Act(const DialogActions& actions)
{
    if (actions.IsEmpty()) return;
    if (_host.Data) {
        for (const auto& [key, value] : actions.Set) _host.Data->Set(key, value);
        for (const auto& [key, delta] : actions.Add)
            if (!_host.AddTo || !_host.AddTo(key, delta)) Add(*_host.Data, key, delta);
    }
    else if (!actions.Set.empty() || !actions.Add.empty()) LOG_WARN(Engine, "Dialog '{}': no SaveData to set", _dialog->Id);
    ActItems(actions);
    if (!actions.Event.empty()) {
        EventPayload payload;
        for (const auto& [name, value] : actions.Payload)
            payload.Set(name, std::visit([](const auto& v) -> EventValue { return v; }, value));
        if (_host.Events) _host.Events->Publish(actions.Event, std::move(payload));
        else LOG_WARN(Engine, "Dialog '{}': no event bus for '{}'", _dialog->Id, actions.Event);
    }
    if (!actions.Lua.empty()) {
        if (_host.Run) _host.Run(actions.Lua);
        else LOG_WARN(Engine, "Dialog '{}': no Lua to run '{}'", _dialog->Id, actions.Lua);
    }
    if (!actions.Call.empty()) {
        if (_host.Call) _host.Call(actions.Call);
        else LOG_WARN(Engine, "Dialog '{}': nothing to call '{}' on", _dialog->Id, actions.Call);
    }
}

void DialogRunner::ActItems(const DialogActions& actions)
{
    if (actions.Give.empty() && actions.Take.empty()) return;
    if (!_host.ChangeItem) {
        LOG_WARN(Engine, "Dialog '{}': no bag to give or take items", _dialog->Id);
        return;
    }
    for (const auto& [item, count] : actions.Give) (void)_host.ChangeItem(item, count);
    for (const auto& [item, count] : actions.Take) (void)_host.ChangeItem(item, -static_cast<i64>(count));
}

void DialogRunner::ShowLine(const DialogStep& step)
{
    _lineStep = &step;
    FillLine(step);
    _state = State::Line;
    if (OnLine) OnLine(_line);
}

void DialogRunner::FillLine(const DialogStep& step)
{
    const DialogSpeaker* speaker = step.Speaker.empty() ? nullptr : _dialog->FindSpeaker(step.Speaker);
    _line                        = { .Speaker = speaker, .Text = Text(step.Key), .Key = step.Key, .Portrait = step.Portrait };
    if (!speaker) return;
    if (Loc::Has(speaker->Key)) _line.Name = Loc::Get(speaker->Key);
    if (_line.Portrait.empty()) _line.Portrait = speaker->Portrait;
    _line.Voice = speaker->Voice;
}

bool DialogRunner::OfferChoice(const DialogStep& step)
{
    _choices.clear();
    for (u32 i = 0; i < step.Options.size(); ++i)
        if (const DialogOption& option = step.Options[i]; Holds(option.If, _host))
            _choices.push_back({ .Text = Text(option.Key), .Id = option.Id, .Option = i });
    if (_choices.empty()) {
        LOG_WARN(Engine, "Dialog '{}': node '{}' step {} - no option shows; skipped", _dialog->Id, _node->Id, _step + 1);
        return false;
    }
    _state = State::Choice;
    return true;
}

void DialogRunner::End()
{
    _state = State::Ended;
    _node  = nullptr;
    _choices.clear();
}

// The key's text in the active language, its placeholders filled. A save
// key that isn't set yet reads 0, as it does in a condition: a counter
// nobody has touched ("You have {gold} gold") is the usual case.
std::string DialogRunner::Text(const std::string& key) const
{
    std::vector<LocArg> args = _host.Args;
    for (const std::string_view name : PlaceholderNames(Loc::Get(key))) {
        if (std::ranges::find(args, name, &LocArg::Name) != args.end()) continue;
        const std::optional<SaveValue> value = ValueOf(_host, name);
        args.push_back(value ? ToArg(name, *value) : LocArg(name, i64{ 0 }));
    }
    return Loc::Format(key, args);
}
} // namespace Arcbit
