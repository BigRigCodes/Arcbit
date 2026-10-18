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
    constexpr f32 ThumbH = RowH - 8.0f;
    constexpr f32 ThumbW = ThumbH * 16.0f / 9.0f;
    constexpr f32 TextX  = ThumbW + 18.0f; // labels start right of the thumbnail

    std::string SlotName(const SaveSlot slot)
    {
        switch (slot.Type) {
            case SaveSlot::Kind::Auto: return Loc::Format("ui.saves.auto", { { "index", slot.Index } });
            case SaveSlot::Kind::Quick: return Loc::Get("ui.saves.quick");
            default: return Loc::Format("ui.saves.manual", { { "index", slot.Index } });
        }
    }

    // 1:05:09 - hours aren't padded, so a long game just grows.
    std::string FormatPlaytime(const f64 seconds)
    {
        const i64 total = static_cast<i64>(seconds);
        return std::format("{}:{:02}:{:02}", total / 3600, total / 60 % 60, total % 60);
    }

    std::string Details(const SaveMetadata& meta)
    {
        return Loc::Format("ui.saves.details", { { "scene", meta.Scene },
                                                 { "time", FormatPlaytime(meta.Playtime) },
                                                 { "saved", meta.SavedAt } });
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
        std::map<u32, const SaveSlotInfo*> manual;
        for (const SaveSlotInfo& info : saves)
            if (info.Slot.Type == SaveSlot::Kind::Manual) manual[info.Slot.Index] = &info;
        for (u32 i = 1; i <= Saves->ManualSlots; ++i, y += RowH + RowGap)
            AddRow(y, SaveSlot::Manual(i), manual.contains(i) ? manual[i] : nullptr);
    }
    else {
        for (const SaveSlotInfo& info : saves) {
            AddRow(y, info.Slot, &info);
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

// A row is a button holding the thumbnail and two labels: the slot and the
// save's name, then the scene, playtime and date (or "Empty").
void SaveSlotScreen::AddRow(const f32 y, const SaveSlot slot, const SaveSlotInfo* info)
{
    const SaveMetadata* meta = info ? &info->Meta : nullptr;
    const f32           rowW = _scroll->Size.X - _scroll->ScrollbarWidth - 8.0f;
    auto*               row  = _scroll->AddChild<Button>();
    row->Size                = { rowW, RowH };
    row->Offset              = { 0.0f, y };
    row->ZOrder              = 2;
    row->OnClick             = [this, slot, hasSave = meta != nullptr] { if (OnPick) OnPick(slot, hasSave); };
    AddThumbnail(*row, info ? info->Thumbnail : std::filesystem::path{});

    auto* name   = row->AddChild<Label>();
    name->Text   = meta ? std::format("{}  -  {}", SlotName(slot), meta->Name) : SlotName(slot);
    name->Size   = { rowW - TextX - 14.0f, 24.0f };
    name->Offset = { TextX, 8.0f };
    name->ZOrder = 3;

    auto* details                   = row->AddChild<Label>();
    details->Text                   = meta ? Details(*meta) : Loc::Get("ui.saves.empty");
    details->Size                   = { rowW - TextX - 14.0f, 20.0f };
    details->Offset                 = { TextX, 34.0f };
    details->ZOrder                 = 3;
    details->SkinOverride.FontScale = 0.75f;
    details->SkinOverride.TextLabel = Color{ 0.7f, 0.7f, 0.75f, 1.0f };
}

// The save's picture, or a dark frame where it would be (an empty slot, a
// save made before thumbnails, no loader).
void SaveSlotScreen::AddThumbnail(UIWidget& row, const std::filesystem::path& path)
{
    auto* frame                 = row.AddChild<Panel>();
    frame->Size                 = { ThumbW, ThumbH };
    frame->Offset               = { 4.0f, 4.0f };
    frame->ZOrder               = 3;
    frame->SkinOverride.PanelBg = Color{ 0.0f, 0.0f, 0.0f, 0.35f };
    if (path.empty() || !LoadThumbnail) return;

    const TextureHandle texture = LoadThumbnail(path);
    if (!texture.IsValid()) return;
    _thumbnails.push_back(texture);
    auto* picture    = frame->AddChild<Image>();
    picture->Texture = texture;
    picture->Sampler = ThumbnailSampler;
    picture->Size    = { ThumbW, ThumbH };
    picture->ZOrder  = 4;
}

void SaveSlotScreen::OnExit()
{
    if (!_thumbnails.empty() && ReleaseThumbnails) ReleaseThumbnails(_thumbnails);
    _thumbnails.clear();
}
} // namespace Arcbit
