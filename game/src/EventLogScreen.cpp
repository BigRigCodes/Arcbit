#include "EventLogScreen.h"

#include <arcbit/ui/Widgets.h>

#include <format>

using namespace Arcbit;

namespace
{
constexpr f32    Width     = 760.0f;
constexpr f32    RowHeight = 24.0f;
constexpr f32    Header    = 34.0f;
constexpr f32    Margin    = 16.0f;
constexpr size_t MaxChars  = 86; // a long payload is cut, not wrapped

constexpr Color EngineText = { 0.75f, 0.8f, 0.9f, 1.0f };
constexpr Color GameText   = { 1.0f, 0.85f, 0.35f, 1.0f };
} // namespace

EventHistory::EventHistory(EventBus& bus, const size_t capacity) :
    _capacity(capacity), _start(std::chrono::steady_clock::now())
{
    _all = bus.SubscribeAll([this, &bus](const Event& e) {
        const f32   seconds  = std::chrono::duration<f32>(std::chrono::steady_clock::now() - _start).count();
        std::string name     = bus.Describe(e.Id);
        const bool  isEngine = name.starts_with(EventBus::ReservedPrefix);
        _lines.push_back({ seconds, std::move(name), e.Payload.ToString(), isEngine });
        if (_lines.size() > _capacity) _lines.pop_front();
    });
}

void EventLogScreen::OnEnter()
{
    _roots.clear();
    _rows.clear();
    const size_t rows = History ? History->Capacity() : 0;

    auto* bg                 = Add<Panel>();
    bg->Size                 = { Width, Header + static_cast<f32>(rows) * RowHeight + 10.0f };
    bg->Anchor               = { 1.0f, 0.0f };
    bg->Pivot                = { 1.0f, 0.0f };
    bg->Offset               = { -Margin, Margin };
    bg->SkinOverride.PanelBg = Color{ 0.05f, 0.06f, 0.09f, 0.82f };

    auto* title   = Add<Label>();
    title->Text   = "Events (F3)";
    title->Size   = { Width - 24.0f, Header };
    title->Anchor = { 1.0f, 0.0f };
    title->Pivot  = { 1.0f, 0.0f };
    title->Offset = { -Margin - 12.0f, Margin + 4.0f };
    title->ZOrder = 1;

    for (size_t i = 0; i < rows; ++i) {
        auto* row                   = Add<Label>();
        row->Size                   = { Width - 24.0f, RowHeight };
        row->Anchor                 = { 1.0f, 0.0f };
        row->Pivot                  = { 1.0f, 0.0f };
        row->Offset                 = { -Margin - 12.0f, Margin + Header + static_cast<f32>(i) * RowHeight };
        row->ZOrder                 = 1;
        row->SkinOverride.FontScale = 0.8f;
        _rows.push_back(row);
    }
    Refresh();
}

void EventLogScreen::OnUpdate(f32 /*dt*/) { Refresh(); }

// Newest at the bottom: the last lines fill the last rows.
void EventLogScreen::Refresh()
{
    if (!History) return;
    const auto&  lines = History->Lines();
    const size_t blank = _rows.size() - std::min(lines.size(), _rows.size());
    for (size_t i = 0; i < _rows.size(); ++i) {
        Label* row = _rows[i];
        if (i < blank) {
            row->Text.clear();
            continue;
        }
        const EventHistory::Line& line = lines[lines.size() - (_rows.size() - i)];
        std::string               text = std::format("{:7.2f}  {}  {}", line.Seconds, line.Name, line.Payload);
        if (text.size() > MaxChars) text = text.substr(0, MaxChars - 3) + "...";
        row->Text                   = std::move(text);
        row->SkinOverride.TextLabel = line.Engine ? EngineText : GameText;
    }
}
