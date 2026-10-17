#pragma once

#include <arcbit/save/SaveManager.h>
#include <arcbit/ui/UIScreen.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Arcbit
{
class ScrollPanel;

// ---------------------------------------------------------------------------
// SaveSlotScreen - the list of save slots, to save into or load from.
//
//   Save  every manual slot, empty or not (SaveManager::ManualSlots)
//   Load  every slot with a save in it - manual, auto and quick - newest first
//
// Each row shows the save's thumbnail, the slot, the save's name, scene,
// playtime and date. Picking one calls OnPick; the screen doesn't save or
// load itself (Application's ShowSaveScreen / ShowLoadScreen wire it,
// confirming overwrites). The frame comes from LayoutPath (.arcui: "title",
// "scroll", "back-btn"); the rows are built in code.
// ---------------------------------------------------------------------------
class SaveSlotScreen : public UIScreen
{
public:
    enum class Purpose : u8
    {
        Save,
        Load,
    };

    Purpose            For   = Purpose::Load;
    const SaveManager* Saves = nullptr;
    std::string        LayoutPath = "assets/engine/ui/save_slots.arcui";

    std::function<void(SaveSlot slot, bool hasSave)> OnPick;
    std::function<void()>                            OnBack;

    // Thumbnails: loaded as the list opens, released when it closes (after
    // the render thread is done with them). Null = no pictures.
    std::function<TextureHandle(const std::filesystem::path&)> LoadThumbnail;
    std::function<void(const std::vector<TextureHandle>&)>     ReleaseThumbnails;
    SamplerHandle                                              ThumbnailSampler;

    SaveSlotScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 6.0f;
    }

    void OnEnter() override;
    void OnExit() override;
    void OnBackPressed() override { if (OnBack) OnBack(); }

private:
    ScrollPanel*               _scroll = nullptr;
    std::vector<TextureHandle> _thumbnails; // loaded for this list; released on exit

    void AddRow(f32 y, SaveSlot slot, const SaveSlotInfo* info);
    void AddThumbnail(UIWidget& row, const std::filesystem::path& path);
};
} // namespace Arcbit
