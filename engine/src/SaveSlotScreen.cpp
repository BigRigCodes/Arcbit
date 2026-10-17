#include <arcbit/ui/SaveSlotScreen.h>

#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>
#include <arcbit/ui/Widgets.h>

#include <ctime>
#include <format>
#include <map>

namespace Arcbit
{
namespace
{
    constexpr f32 RowH   = 62.0f;
    constexpr f32 RowGap = 8.0f;

    std::string SlotName(const SaveSlot slot)
    {
        switch (slot.Type) {
            case SaveSlot::Kind::Auto: return std::format("{} {}", Loc::Get("ui.saves.auto"), slot.Index);
            case SaveSlot::Kind::Quick: return Loc::Get("ui.saves.quick");
            default: return std::format("{} {}", Loc::Get("ui.saves.manual"), slot.Index);
        }
    }

    // 1:05:09 - hours aren't padded, so a long game just grows.
    std::string FormatPlaytime(const f64 seconds)
    {
        const i64 total = static_cast<i64>(seconds);
        return std::format("{}:{:02}:{:02}", total / 3600, total / 60 % 60, total % 60);
    }

    // Local time, "2026-10-17 13:42".
    std::string FormatDate(const i64 unixSeconds)
    {
        const std::time_t time = unixSeconds;
        std::tm           local{};
        if (localtime_s(&local, &time) != 0) return {};
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                           local.tm_hour, local.tm_min);
    }

    std::string Details(const SaveMetadata& meta)
    {
        return std::format("{}      {} {}      {}", meta.Scene, Loc::Get("ui.saves.played"), FormatPlaytime(meta.Playtime),
                           FormatDate(meta.SavedAt));
    }
} // namespace

void SaveSlotScreen::OnEnter()
{
    _roots.clear();
    if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "SaveSlotScreen: '{}' not found", LayoutPath);
    if (auto* title = FindWidget<Label>("title"))
        title->Text = Loc::Get(For == Purpose::Save ? "ui.saves.save_title" : "ui.saves.load_title");
    if (auto* back = FindWidget<Button>("back-btn")) back->OnClick = [this] { if (OnBack) OnBack(); };

    _scroll = FindWidget<ScrollPanel>("scroll");
    if (!_scroll || !Saves) return;

    const std::vector<SaveSlotInfo> saves = Saves->List();
    f32                             y     = 4.0f;
    if (For == Purpose::Save) {
        std::map<u32, const SaveMetadata*> manual;
        for (const SaveSlotInfo& info : saves)
            if (info.Slot.Type == SaveSlot::Kind::Manual) manual[info.Slot.Index] = &info.Meta;
        for (u32 i = 1; i <= Saves->ManualSlots; ++i, y += RowH + RowGap)
            AddRow(y, SaveSlot::Manual(i), manual.contains(i) ? manual[i] : nullptr);
    }
    else {
        for (const SaveSlotInfo& info : saves) {
            AddRow(y, info.Slot, &info.Meta);
            y += RowH + RowGap;
        }
        if (saves.empty()) {
            auto* none   = _scroll->AddChild<Label>();
            none->Text   = Loc::Get("ui.saves.none");
            none->Align  = TextAlign::Center;
            none->Size   = { _scroll->Size.X - _scroll->ScrollbarWidth, 30.0f };
            none->Offset = { 0.0f, 20.0f };
            none->ZOrder = 2;
        }
    }
    _scroll->ContentHeight = y;
    FocusAt(0);
}

// A row is a button holding two labels: the slot and the save's name, then
// the scene, playtime and date (or "Empty").
void SaveSlotScreen::AddRow(const f32 y, const SaveSlot slot, const SaveMetadata* meta)
{
    const f32 rowW = _scroll->Size.X - _scroll->ScrollbarWidth - 8.0f;
    auto*     row  = _scroll->AddChild<Button>();
    row->Size      = { rowW, RowH };
    row->Offset    = { 0.0f, y };
    row->ZOrder    = 2;
    row->OnClick   = [this, slot, hasSave = meta != nullptr] { if (OnPick) OnPick(slot, hasSave); };

    auto* name   = row->AddChild<Label>();
    name->Text   = meta ? std::format("{}  -  {}", SlotName(slot), meta->Name) : SlotName(slot);
    name->Size   = { rowW - 28.0f, 24.0f };
    name->Offset = { 14.0f, 8.0f };
    name->ZOrder = 3;

    auto* details                     = row->AddChild<Label>();
    details->Text                     = meta ? Details(*meta) : Loc::Get("ui.saves.empty");
    details->Size                     = { rowW - 28.0f, 20.0f };
    details->Offset                   = { 14.0f, 34.0f };
    details->ZOrder                   = 3;
    details->SkinOverride.FontScale   = 0.75f;
    details->SkinOverride.TextLabel   = Color{ 0.7f, 0.7f, 0.75f, 1.0f };
}
} // namespace Arcbit
