#include <arcbit/ui/ConsoleScreen.h>

#include <arcbit/app/DebugConsole.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <format>

namespace Arcbit
{
namespace
{
    constexpr f32    PanelHeight = 420.0f;
    constexpr f32    LineHeight  = 18.0f;
    constexpr size_t ShownLines  = 200; // older lines stay in the console, just not drawn
} // namespace

// ---------------------------------------------------------------------------
// ConsoleScreen
// ---------------------------------------------------------------------------

void ConsoleScreen::OnEnter()
{
    _roots.clear();
    auto* bg                 = Add<Panel>();
    bg->SizePercent          = { 1.0f, 0.0f };
    bg->Size                 = { 0.0f, PanelHeight };
    bg->ZOrder               = 1;
    bg->SkinOverride.PanelBg = Color{ 0.04f, 0.04f, 0.07f, 0.92f };

    _scroll              = bg->AddChild<ScrollPanel>();
    _scroll->SizePercent = { 0.98f, 0.0f };
    _scroll->Size        = { 0.0f, PanelHeight - 60.0f };
    _scroll->Offset      = { 10.0f, 8.0f };
    _scroll->ZOrder      = 2;

    _input              = bg->AddChild<TextInput>();
    _input->SizePercent = { 0.98f, 0.0f };
    _input->Size        = { 0.0f, 36.0f };
    _input->Anchor      = { 0.0f, 1.0f };
    _input->Pivot       = { 0.0f, 1.0f };
    _input->Offset      = { 10.0f, -8.0f };
    _input->ZOrder      = 2;
    _input->Placeholder = "command, or Lua - try help";
    _input->OnConfirm   = [this](const std::string& line) { Submit(line); };

    _shownVersion = ~0ull;
    ShowLines();
    FocusAt(0);
}

void ConsoleScreen::OnTick(f32, const InputManager& input)
{
    if (!_input) return;
    std::erase_if(_input->Text, [](const char c) { return c == '`' || c == '~'; }); // the key that opened it
    if (GetFocusedWidget() != _input) FocusAt(0);
    if (input.JustPressed(ActionHistoryUp)) Recall(-1);
    if (input.JustPressed(ActionHistoryDown)) Recall(1);
    if (input.JustPressed(ActionComplete)) CompleteCommand();
}

void ConsoleScreen::OnUpdate(f32)
{
    if (Console && Console->GetVersion() != _shownVersion) ShowLines();
}

// The newest lines, scrolled to the bottom.
void ConsoleScreen::ShowLines()
{
    if (!_scroll || !Console) return;
    _shownVersion = Console->GetVersion();
    _scroll->ClearChildren();
    const auto& lines = Console->GetLines();
    const size_t first = lines.size() > ShownLines ? lines.size() - ShownLines : 0;
    f32          y     = 0.0f;
    for (size_t i = first; i < lines.size(); ++i, y += LineHeight) {
        auto* label                   = _scroll->AddChild<Label>();
        label->Text                   = lines[i];
        label->SizePercent            = { 1.0f, 0.0f };
        label->Size                   = { 0.0f, LineHeight };
        label->Offset                 = { 0.0f, y };
        label->ZOrder                 = 3;
        label->SkinOverride.FontScale = 0.7f;
        if (lines[i].starts_with("> ")) label->SkinOverride.TextLabel = Color{ 0.6f, 0.8f, 1.0f, 1.0f };
    }
    _scroll->ContentHeight = y;
    _scroll->ScrollOffset  = std::max(0.0f, y - _scroll->Size.Y); // the bottom
}

void ConsoleScreen::Submit(const std::string& line)
{
    _input->Text   = "";
    _historyCursor = -1;
    if (Console) Console->Execute(line);
}

// Up: older lines; Down: newer, then back to an empty line.
void ConsoleScreen::Recall(const i32 step)
{
    if (!Console || Console->GetHistory().empty()) return;
    const i32 count = static_cast<i32>(Console->GetHistory().size());
    i32       at    = _historyCursor < 0 ? count : _historyCursor;
    at              = std::clamp(at + step, 0, count);
    _historyCursor  = at == count ? -1 : at;
    _input->Text    = _historyCursor < 0 ? "" : Console->GetHistory()[_historyCursor];
}

// One match fills in; several are listed.
void ConsoleScreen::CompleteCommand()
{
    if (!Console || _input->Text.empty() || _input->Text.find(' ') != std::string::npos) return;
    const std::vector<std::string> matches = Console->Complete(_input->Text);
    if (matches.size() == 1) _input->Text = matches.front() + " ";
    else if (matches.size() > 1) {
        std::string list;
        for (const std::string& name : matches) list += name + "  ";
        Console->Print(list);
    }
}

// ---------------------------------------------------------------------------
// ErrorToasts
// ---------------------------------------------------------------------------

void ErrorToasts::Report(const std::string_view error)
{
    const std::string_view first = error.substr(0, error.find('\n'));
    _toasts.push_back({ std::string(first) });
    while (_toasts.size() > MaxShown) _toasts.pop_front();
    _changed = true;
}

void ErrorToasts::OnUpdate(const f32 dt)
{
    for (Toast& toast : _toasts) toast.Age += dt;
    const size_t before = _toasts.size();
    std::erase_if(_toasts, [this](const Toast& t) { return t.Age >= Seconds; });
    if (_changed || before != _toasts.size() || !_toasts.empty()) Rebuild(); // fading needs a redraw
    _changed = false;
}

void ErrorToasts::Rebuild()
{
    _roots.clear();
    f32 y = 8.0f;
    for (const Toast& toast : _toasts) {
        auto* box                 = Add<Panel>();
        box->Size                 = { 620.0f, 30.0f };
        box->Anchor               = { 1.0f, 0.0f };
        box->Pivot                = { 1.0f, 0.0f };
        box->Offset               = { -8.0f, y };
        box->Opacity              = std::clamp(Seconds - toast.Age, 0.0f, 1.0f);
        box->SkinOverride.PanelBg = Color{ 0.45f, 0.08f, 0.08f, 0.9f };

        auto* text                   = box->AddChild<Label>();
        text->Text                   = "Script error: " + toast.Text;
        text->Size                   = { 600.0f, 30.0f };
        text->Offset                 = { 10.0f, 6.0f };
        text->ZOrder                 = 1;
        text->SkinOverride.FontScale = 0.7f;
        y += 34.0f;
    }
}
} // namespace Arcbit
