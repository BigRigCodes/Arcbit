#pragma once

#include <arcbit/save/SaveManager.h>
#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
class ScrollPanel;

// ---------------------------------------------------------------------------
// SaveSlotScreen - the list of save slots, to save into or load from.
//
//   Save  every manual slot, empty or not (SaveManager::ManualSlots)
//   Load  every slot with a save in it - manual, auto and quick - newest first
//
// Each row shows the slot, the save's name, scene, playtime and date. Picking
// one calls OnPick; the screen doesn't save or load itself (Application's
// ShowSaveScreen / ShowLoadScreen wire it, confirming overwrites). The frame
// comes from LayoutPath (.arcui: "title", "scroll", "back-btn"); the rows are
// built in code.
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

    SaveSlotScreen()
    {
        BlocksInput     = true;
        TransitionSpeed = 6.0f;
    }

    void OnEnter() override;
    void OnBackPressed() override { if (OnBack) OnBack(); }

private:
    ScrollPanel* _scroll = nullptr;

    void AddRow(f32 y, SaveSlot slot, const SaveMetadata* meta);
};
} // namespace Arcbit
