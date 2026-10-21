#include <arcbit/dialog/Dialog.h>

#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <set>

namespace Arcbit
{
using Json = nlohmann::ordered_json;

namespace
{
// The fields a step may have, beyond the ones its kind adds.
constexpr std::array StepFields   = { "if", "goto", "set", "add", "event", "lua", "call", "id", "key" };
constexpr std::array SayFields    = { "say", "speaker", "portrait" };
constexpr std::array OptionFields = { "id", "key", "text", "if", "goto", "set", "add", "event", "lua", "call", "cancel" };
constexpr std::array ArmFields    = { "if", "goto", "set", "add", "event", "lua", "call" };
constexpr std::array TopFields    = { "speaker", "speakers", "start", "world", "cancelable", "nodes" };

template<size_t N>
bool Has(const std::array<const char*, N>& fields, const std::string_view name)
{
    return std::ranges::any_of(fields, [name](const char* f) { return name == f; });
}

// Reads one file, collecting what's wrong with it.
struct Parser
{
    std::string_view          Id;
    std::string               Prefix; // "dialog.elder"
    std::vector<DialogIssue>& Issues;
    std::string               DefaultSpeaker;
    std::set<std::string>     Keys;   // every text key so far: duplicates are errors

    template<typename... A>
    void Error(std::format_string<A...> format, A&&... args)
    {
        Issues.push_back({ true, std::format(format, std::forward<A>(args)...) });
    }

    template<typename... A>
    void Warn(std::format_string<A...> format, A&&... args)
    {
        Issues.push_back({ false, std::format(format, std::forward<A>(args)...) });
    }

    // Misspelled fields are easy to make and silent otherwise.
    template<size_t... N>
    void CheckFields(const Json& object, const std::string& where, const std::array<const char*, N>&... allowed)
    {
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!(Has(allowed, it.key()) || ...)) Warn("{}: unknown field '{}'", where, it.key());
    }

    std::string              ReadString(const Json& j, const char* field, const std::string& where);
    std::optional<bool>      ReadBool(const Json& j, const char* field, const std::string& where);
    std::optional<SaveValue> ReadValue(const Json& j, const std::string& where);
    DialogCondition          ReadCondition(const Json& j, const std::string& where);
    void                     ReadTest(const std::string& key, const Json& j, DialogCondition& out, const std::string& where);
    void                     ReadActions(const Json& j, DialogActions& out, const std::string& where);
    void                     ReadPairs(const Json& j, std::vector<std::pair<std::string, SaveValue>>& out, bool numbers,
                                       const std::string& where);
    std::string              MakeKey(const Json& j, const std::string& node, u32& counter, const std::string& where);
    DialogOption             ReadOption(const Json& j, const std::string& node, u32& counter, bool arm, const std::string& where);
    void                     ReadCommon(const Json& j, DialogCondition& cond, DialogActions& actions, std::string& go,
                                        const std::string& where);
    void                     ReadSay(const Json& j, DialogStep& step, const std::string& node, u32& counter, const std::string& where);
    void                     ReadNodes(const Json& j, Dialog& dialog);
    DialogStep               ReadStep(const Json& j, const std::string& node, u32& counter, const std::string& where);
    void                     ReadOptions(const Json& list, DialogStep& step, const std::string& node, u32& counter,
                                         const std::string& where);
    void                     ReadSpeakers(const Json& j, Dialog& dialog);
};

std::optional<SaveValue> Parser::ReadValue(const Json& j, const std::string& where)
{
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return j.get<i64>();
    if (j.is_number()) return j.get<f64>();
    if (j.is_string()) return j.get<std::string>();
    Error("{}: a value must be a bool, number or string", where);
    return std::nullopt;
}

std::optional<DialogCompare> ReadCompare(const std::string_view op)
{
    constexpr std::array<std::pair<std::string_view, DialogCompare>, 6> ops = {
        { { "==", DialogCompare::Equal },     { "!=", DialogCompare::NotEqual },
          { "<", DialogCompare::Less },       { "<=", DialogCompare::LessEqual },
          { ">", DialogCompare::Greater },    { ">=", DialogCompare::GreaterEqual } }
    };
    for (const auto& [name, compare] : ops)
        if (name == op) return compare;
    return std::nullopt;
}

// "gold": [">=", 5], or "met_elder": true (equal).
void Parser::ReadTest(const std::string& key, const Json& j, DialogCondition& out, const std::string& where)
{
    DialogTest test{ .Key = key };
    const Json* value = &j;
    if (j.is_array()) {
        const auto op = j.size() == 2 && j[0].is_string() ? ReadCompare(j[0].get<std::string>()) : std::nullopt;
        if (!op) return Error("{}: '{}' must be [\"op\", value] with op one of == != < <= > >=", where, key);
        test.Op = *op;
        value   = &j[1];
    }
    if (auto v = ReadValue(*value, std::format("{} '{}'", where, key))) {
        test.Value = std::move(*v);
        out.All.push_back(std::move(test));
    }
}

DialogCondition Parser::ReadCondition(const Json& j, const std::string& where)
{
    DialogCondition out;
    if (!j.is_object()) {
        Error("{}: \"if\" must be an object", where);
        return out;
    }
    for (const auto& [key, value] : j.items()) {
        if (key == "lua") {
            if (value.is_string()) out.Lua = value.get<std::string>();
            else Error("{}: \"lua\" must be a string", where);
        }
        else if (key == "not") out.Not.push_back(ReadCondition(value, where + " not"));
        else if (key == "any") {
            if (!value.is_array()) Error("{}: \"any\" must be a list of conditions", where);
            else for (const Json& c : value) out.Any.push_back(ReadCondition(c, where + " any"));
        }
        else ReadTest(key, value, out, where);
    }
    return out;
}

void Parser::ReadPairs(const Json& j, std::vector<std::pair<std::string, SaveValue>>& out, const bool numbers,
                       const std::string& where)
{
    if (!j.is_object()) return Error("{} must be an object", where);
    for (const auto& [key, value] : j.items()) {
        if (numbers && !value.is_number()) {
            Error("{}: '{}' must be a number", where, key);
            continue;
        }
        if (auto v = ReadValue(value, std::format("{} '{}'", where, key))) out.emplace_back(key, std::move(*v));
    }
}

void Parser::ReadActions(const Json& j, DialogActions& out, const std::string& where)
{
    if (const auto it = j.find("set"); it != j.end()) ReadPairs(*it, out.Set, false, where + " set");
    if (const auto it = j.find("add"); it != j.end()) ReadPairs(*it, out.Add, true, where + " add");
    if (const auto it = j.find("event"); it != j.end()) {
        if (it->is_string()) out.Event = it->get<std::string>();
        else if (it->is_object() && it->contains("name") && (*it)["name"].is_string()) {
            out.Event = (*it)["name"].get<std::string>();
            if (const auto payload = it->find("payload"); payload != it->end())
                ReadPairs(*payload, out.Payload, false, where + " event payload");
        }
        else Error("{}: \"event\" must be a name or {{ \"name\", \"payload\" }}", where);
    }
    for (const auto& [field, target] : { std::pair{ "lua", &out.Lua }, std::pair{ "call", &out.Call } })
        if (const auto it = j.find(field); it != j.end()) {
            if (it->is_string()) *target = it->get<std::string>();
            else Error("{}: \"{}\" must be a string", where, field);
        }
}

// An explicit "key"; else dialog.<id>.<node>.<id>; else the node's next number.
std::string Parser::MakeKey(const Json& j, const std::string& node, u32& counter, const std::string& where)
{
    std::string key;
    if (const auto it = j.find("key"); it != j.end() && it->is_string()) key = it->get<std::string>();
    else if (const auto id = j.find("id"); id != j.end() && id->is_string())
        key = std::format("{}.{}.{}", Prefix, node, id->get<std::string>());
    else key = std::format("{}.{}.{}", Prefix, node, ++counter);
    if (!Keys.insert(key).second) Error("{}: the text key '{}' is used twice", where, key);
    return key;
}

std::string Parser::ReadString(const Json& j, const char* field, const std::string& where)
{
    const auto it = j.find(field);
    if (it == j.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    Error("{}: \"{}\" must be a string", where, field);
    return {};
}

std::optional<bool> Parser::ReadBool(const Json& j, const char* field, const std::string& where)
{
    const auto it = j.find(field);
    if (it == j.end() || it->is_null()) return std::nullopt;
    if (it->is_boolean()) return it->get<bool>();
    Error("{}: \"{}\" must be true or false", where, field);
    return std::nullopt;
}

// What steps and options share: "if", actions, "goto".
void Parser::ReadCommon(const Json& j, DialogCondition& cond, DialogActions& actions, std::string& go,
                        const std::string& where)
{
    if (const auto it = j.find("if"); it != j.end()) cond = ReadCondition(*it, where);
    ReadActions(j, actions, where);
    go = ReadString(j, "goto", where);
}

DialogOption Parser::ReadOption(const Json& j, const std::string& node, u32& counter, const bool arm,
                                const std::string& where)
{
    DialogOption option;
    if (!j.is_object()) {
        Error("{}: an option must be an object", where);
        return option;
    }
    ReadCommon(j, option.If, option.Actions, option.Goto, where);
    if (arm) { // a branch's arm: no text
        CheckFields(j, where, ArmFields);
        return option;
    }
    option.English = ReadString(j, "text", where);
    if (option.English.empty()) Error("{}: an option needs \"text\"", where);
    option.Id  = ReadString(j, "id", where);
    option.Key    = MakeKey(j, node, counter, where);
    option.Cancel = ReadBool(j, "cancel", where).value_or(false);
    CheckFields(j, where, OptionFields);
    return option;
}

void Parser::ReadOptions(const Json& list, DialogStep& step, const std::string& node, u32& counter,
                         const std::string& where)
{
    const bool arm = step.Kind == DialogStepKind::Branch;
    if (!list.is_array() || list.empty()) return Error("{}: \"{}\" must be a list of options", where, arm ? "branch" : "choice");
    for (size_t i = 0; i < list.size(); ++i)
        step.Options.push_back(ReadOption(list[i], node, counter, arm, std::format("{} option {}", where, i + 1)));
}

// A line. "speaker": an id, or null for narration; left out, the dialog's "speaker".
void Parser::ReadSay(const Json& j, DialogStep& step, const std::string& node, u32& counter, const std::string& where)
{
    step.Kind    = DialogStepKind::Say;
    step.English = ReadString(j, "say", where);
    step.Key     = MakeKey(j, node, counter, where);
    step.Speaker = j.contains("speaker") ? ReadString(j, "speaker", where) : DefaultSpeaker;
    step.Portrait = ReadString(j, "portrait", where);
    CheckFields(j, where, StepFields, SayFields);
}

DialogStep Parser::ReadStep(const Json& j, const std::string& node, u32& counter, const std::string& where)
{
    DialogStep step;
    if (j.is_string()) { // shorthand: a line by the dialog's speaker
        step.Kind    = DialogStepKind::Say;
        step.English = j.get<std::string>();
        step.Speaker = DefaultSpeaker;
        step.Key     = MakeKey(Json::object(), node, counter, where);
        return step;
    }
    if (!j.is_object()) {
        Error("{}: a step must be a line of text or an object", where);
        return step;
    }
    ReadCommon(j, step.If, step.Actions, step.Goto, where);
    if (j.contains("say")) ReadSay(j, step, node, counter, where);
    else if (const auto it = j.find("choice"); it != j.end()) {
        step.Kind = DialogStepKind::Choice;
        ReadOptions(*it, step, node, counter, where);
        CheckFields(j, where, StepFields, std::array{ "choice" });
    }
    else if (const auto arms = j.find("branch"); arms != j.end()) {
        step.Kind = DialogStepKind::Branch;
        ReadOptions(*arms, step, node, counter, where);
        CheckFields(j, where, StepFields, std::array{ "branch" });
    }
    else {
        if (step.Actions.IsEmpty() && step.Goto.empty())
            Error("{}: a step needs \"say\", \"choice\", \"branch\", an action or a \"goto\"", where);
        CheckFields(j, where, StepFields);
    }
    return step;
}

void Parser::ReadSpeakers(const Json& j, Dialog& dialog)
{
    if (!j.is_object()) return Error("\"speakers\" must be an object of id: {{ name, portrait, voice }}");
    for (const auto& [id, s] : j.items()) {
        const std::string where = std::format("speaker '{}'", id);
        if (!s.is_object()) {
            Error("{}: must be an object", where);
            continue;
        }
        DialogSpeaker speaker{ .Id = id };
        speaker.English  = ReadString(s, "name", where);
        speaker.Key      = s.contains("key") ? ReadString(s, "key", where) : std::format("{}.speaker.{}", Prefix, id);
        speaker.Portrait = ReadString(s, "portrait", where);
        speaker.Voice    = ReadString(s, "voice", where);
        CheckFields(s, where, std::array{ "name", "key", "portrait", "voice" });
        dialog.Speakers.push_back(std::move(speaker));
    }
}

void Parser::ReadNodes(const Json& j, Dialog& dialog)
{
    if (!j.is_object()) return Error("\"nodes\" must be an object of name: [steps]");
    for (const auto& [name, steps] : j.items()) {
        if (name == Dialog::EndNode || name == "speaker") { // "end" is a goto's; "speaker" names' keys
            Error("'{}' can't be a node's name", name);
            continue;
        }
        if (!steps.is_array()) {
            Error("node '{}' must be a list of steps", name);
            continue;
        }
        DialogNode node{ .Id = name };
        u32        counter = 0;
        for (size_t i = 0; i < steps.size(); ++i)
            node.Steps.push_back(ReadStep(steps[i], name, counter, std::format("node '{}' step {}", name, i + 1)));
        dialog.Nodes.push_back(std::move(node));
    }
}

// Every goto names a node, every line's speaker exists.
void CheckTargets(const Dialog& dialog, Parser& p)
{
    const auto target = [&](const std::string& go, const std::string& where) {
        if (!go.empty() && go != Dialog::EndNode && !dialog.FindNode(go)) p.Error("{}: goto '{}' - there's no such node", where, go);
    };
    if (!dialog.FindNode(dialog.Start)) p.Error("the start node '{}' doesn't exist", dialog.Start);
    for (const DialogNode& node : dialog.Nodes)
        for (size_t i = 0; i < node.Steps.size(); ++i) {
            const DialogStep& step  = node.Steps[i];
            const std::string where = std::format("node '{}' step {}", node.Id, i + 1);
            target(step.Goto, where);
            for (const DialogOption& option : step.Options) target(option.Goto, where);
            if (step.Kind == DialogStepKind::Say && !step.Speaker.empty() && !dialog.FindSpeaker(step.Speaker))
                p.Error("{}: there's no speaker '{}'", where, step.Speaker);
        }
}

// A node no goto leads to (from the start) is likely a mistake - though a
// game can start a dialog at any node.
void CheckReachable(const Dialog& dialog, Parser& p)
{
    std::set<std::string_view>     reached;
    std::vector<const DialogNode*> todo;
    const auto visit = [&](const std::string_view id) {
        if (const DialogNode* node = dialog.FindNode(id); node && reached.insert(id).second) todo.push_back(node);
    };
    visit(dialog.Start);
    while (!todo.empty()) {
        const DialogNode* node = todo.back();
        todo.pop_back();
        for (const DialogStep& step : node->Steps) {
            visit(step.Goto);
            for (const DialogOption& option : step.Options) visit(option.Goto);
        }
    }
    for (const DialogNode& node : dialog.Nodes)
        if (!reached.contains(node.Id)) p.Warn("node '{}': nothing leads to it from '{}'", node.Id, dialog.Start);
}
} // namespace

std::optional<Dialog> ParseDialog(const Json& json, const std::string_view id, std::vector<DialogIssue>& issues)
{
    Parser p{ .Id = id, .Prefix = DialogKeyPrefix(id), .Issues = issues };
    if (!json.is_object()) {
        p.Error("a dialog must be a JSON object");
        return std::nullopt;
    }
    Dialog dialog{ .Id = std::string(id) };
    p.CheckFields(json, "dialog", TopFields);
    if (const auto it = json.find("speakers"); it != json.end()) p.ReadSpeakers(*it, dialog);
    p.DefaultSpeaker = p.ReadString(json, "speaker", "dialog");
    const std::string world = p.ReadString(json, "world", "dialog");
    if (world != "" && world != "run" && world != "pause") p.Error("\"world\" must be \"run\" or \"pause\"");
    dialog.PauseWorld = world == "pause";
    dialog.Cancelable = p.ReadBool(json, "cancelable", "dialog");
    if (const auto it = json.find("nodes"); it != json.end()) p.ReadNodes(*it, dialog);
    if (dialog.Nodes.empty()) {
        p.Error("a dialog needs \"nodes\"");
        return std::nullopt;
    }
    dialog.Start = p.ReadString(json, "start", "dialog");
    if (dialog.Start.empty()) dialog.Start = dialog.Nodes.front().Id;
    CheckTargets(dialog, p);
    CheckReachable(dialog, p);
    return dialog;
}

std::optional<Dialog> LoadDialog(const std::filesystem::path& file, const std::string_view id, std::vector<DialogIssue>& issues)
{
    std::ifstream in(file);
    if (!in.is_open()) {
        issues.push_back({ true, std::format("can't open '{}'", file.string()) });
        return std::nullopt;
    }
    try {
        return ParseDialog(Json::parse(in, nullptr, true, true), id, issues); // comments allowed
    } catch (const nlohmann::json::exception& e) {
        issues.push_back({ true, std::format("parse error: {}", e.what()) });
        return std::nullopt;
    }
}

std::string DialogKeyPrefix(const std::string_view id)
{
    std::string prefix = "dialog.";
    for (const char c : id) prefix += c == '/' || c == '\\' ? '.' : c;
    return prefix;
}

const DialogNode* Dialog::FindNode(const std::string_view id) const
{
    const auto it = std::ranges::find(Nodes, id, &DialogNode::Id);
    return it != Nodes.end() ? &*it : nullptr;
}

const DialogSpeaker* Dialog::FindSpeaker(const std::string_view id) const
{
    const auto it = std::ranges::find(Speakers, id, &DialogSpeaker::Id);
    return it != Speakers.end() ? &*it : nullptr;
}

std::vector<std::pair<std::string, std::string>> Dialog::GetStrings() const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const DialogSpeaker& speaker : Speakers)
        if (!speaker.English.empty()) out.emplace_back(speaker.Key, speaker.English);
    for (const DialogNode& node : Nodes)
        for (const DialogStep& step : node.Steps) {
            if (step.Kind == DialogStepKind::Say) out.emplace_back(step.Key, step.English);
            if (step.Kind == DialogStepKind::Choice)
                for (const DialogOption& option : step.Options) out.emplace_back(option.Key, option.English);
        }
    return out;
}
} // namespace Arcbit
