#pragma once

#include <arcbit/dialog/DialogRunner.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/ui/UIScreen.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Button;
class Image;
class Label;
class NineSlice;

// The player's dialog settings (Game settings): how fast text types out and
// whether lines move on by themselves.
namespace DialogSettings
{
    constexpr std::string_view TextSpeedKey   = "game.text_speed";   // "slow", "normal", "fast", "instant"
    constexpr std::string_view AutoAdvanceKey = "game.auto_advance"; // bool

    enum class TextSpeed : u8 { Slow, Normal, Fast, Instant };
    constexpr std::array<std::string_view, 4> TextSpeedNames = { "slow", "normal", "fast", "instant" };

    [[nodiscard]] TextSpeed GetTextSpeed();
    void                    SetTextSpeed(TextSpeed speed);
    [[nodiscard]] f32       CharsPerSecond(TextSpeed speed); // instant: 0 (no typewriter)
    [[nodiscard]] bool      GetAutoAdvance();
    void                    SetAutoAdvance(bool on);
} // namespace DialogSettings

// The textures a dialog box is drawn with (Application::CreateDialogScreen
// loads the engine's stock art; a game can swap any of them).
struct DialogArt
{
    TextureHandle Box;       // the text box and the choice box: a nine-slice
    TextureHandle NamePlate; // a nine-slice
    TextureHandle NextArrow;
    TextureHandle Cursor;    // beside the choice under focus
    SamplerHandle Sampler;   // nearest, for pixel art
};

// ---------------------------------------------------------------------------
// DialogScreen - the conversation box along the bottom of the screen: the
// speaker's name plate and portrait, the line typing out in the speaker's
// voice, a bobbing arrow once it's all shown, and a box of choices.
//
// Confirm (Enter, gamepad South, a click) shows the rest of a line, then
// moves on; the arrows / D-pad / mouse pick a choice. The player's Text
// speed and Auto-advance settings are read as each line starts.
//
// The layout (LayoutPath) is an .arcui a game can replace. Its widgets:
// "box" (NineSlice) holding "portrait" (Image), "text" (Label, markup),
// "next-arrow" (Image), "name-plate" (NineSlice) with "name" (Label); and
// "choices" (NineSlice), whose rows the screen builds. Meta: "text_left"
// (the text's left edge without a portrait), "choice_row" (row height).
// ---------------------------------------------------------------------------
class DialogScreen : public UIScreen
{
public:
    std::string LayoutPath = "assets/engine/ui/dialog.arcui";
    DialogArt   Art;

    // Portraits and voices by path (Application wires its texture manager
    // and audio); either may be left unset.
    std::function<TextureHandle(const std::string& path)> LoadPortrait;
    std::function<void(const std::string& path)>          PlayVoice;
    std::string DefaultVoice; // for a speaker without a "voice"; narration is silent

    // The UI's skin, to measure choices with: the choice box widens to fit
    // the longest (UIManager::GetSkin; unset, it keeps the layout's width).
    const UISkin* Skin = nullptr;

    // More actions that move a line on, beside Confirm and a click - a
    // game's own Interact key, say.
    std::vector<ActionID> AdvanceActions;

    // The conversation is over (or was stopped); the last choice's id.
    std::function<void(const std::string& lastChoice)> OnFinished;

    DialogScreen();

    // Run a dialog in this box, from node (default: its start).
    void Begin(std::shared_ptr<const Dialog> dialog, DialogHost host, std::string_view node = {});

    [[nodiscard]] DialogRunner&       GetRunner() { return _runner; }
    [[nodiscard]] const DialogRunner& GetRunner() const { return _runner; }

    void OnEnter() override;
    void OnTick(f32 dt, const InputManager& input) override;
    void OnUpdate(f32 dt) override;
    void OnBackPressed() override; // Esc / gamepad East: DialogRunner::Cancel

protected:
    void OnLocaleChanged() override;

private:
    void ShowState();
    void ShowLine();
    void ShowChoices();
    void AddChoiceRow(u32 index, const std::string& text, f32 height, f32 pad);
    void LayoutText(bool portrait);
    [[nodiscard]] f32 ChoiceBoxWidth() const;
    void ApplyArt();
    void Advance();
    void Choose(u32 index);
    void Finish();
    void UpdateAuto(f32 dt);

    DialogRunner _runner;
    bool         _built        = false; // the layout is loaded
    bool         _finished      = false;
    bool         _finishPending = false; // told next update, outside any widget's click
    i32          _pendingChoice = -1;    // likewise: a row clicked
    bool         _focusChoices = false; // focus the first choice next tick, not the one it appeared on
    bool         _ticked       = false; // OnTick ran since the last OnUpdate: the screen has input
    bool         _active       = false; // ...as of the last tick; under a menu, nothing moves on
    f32          _time         = 0.0f;  // for the arrow's bob
    f32          _autoWait     = 0.0f;  // auto-advance: left to wait once the line is shown

    NineSlice*          _box       = nullptr;
    NineSlice*          _namePlate = nullptr;
    NineSlice*          _choiceBox = nullptr;
    Label*              _text      = nullptr;
    Label*              _name      = nullptr;
    Image*              _portrait  = nullptr;
    Image*              _arrow     = nullptr;
    f32                 _arrowY    = 0.0f;
    f32                 _textLeft  = 40.0f; // the layout's, without a portrait
    f32                 _choiceW   = 380.0f; // the layout's choice box width: the least it gets
    std::vector<Button*> _rows;
    std::vector<Image*>  _cursors;
};
} // namespace Arcbit
