#include <arcbit/ui/DialogScreen.h>

#include <arcbit/core/Log.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/ui/UIManager.h>
#include <arcbit/ui/Widgets.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Arcbit
{
namespace DialogSettings
{
TextSpeed GetTextSpeed()
{
    const std::string name = Settings::Get<std::string>(TextSpeedKey, "normal");
    const auto        it   = std::ranges::find(TextSpeedNames, name);
    return it != TextSpeedNames.end() ? static_cast<TextSpeed>(it - TextSpeedNames.begin()) : TextSpeed::Normal;
}

void SetTextSpeed(const TextSpeed speed)
{
    Settings::Set<std::string>(TextSpeedKey, std::string(TextSpeedNames[static_cast<size_t>(speed)]));
}

f32 CharsPerSecond(const TextSpeed speed)
{
    switch (speed) {
        case TextSpeed::Slow: return 20.0f;
        case TextSpeed::Fast: return 90.0f;
        case TextSpeed::Instant: return 0.0f;
        default: return 40.0f;
    }
}

bool GetAutoAdvance() { return Settings::Get<bool>(AutoAdvanceKey, false); }
void SetAutoAdvance(const bool on) { Settings::Set<bool>(AutoAdvanceKey, on); }
} // namespace DialogSettings

namespace
{
// Auto-advance: how long a shown line stays - longer lines longer.
f32 ReadingTime(const std::string& text) { return std::min(1.2f + 0.04f * static_cast<f32>(text.size()), 5.0f); }

constexpr Color Clear{ 0.0f, 0.0f, 0.0f, 0.0f };
constexpr Color RowHot{ 0.15f, 0.19f, 0.31f, 0.12f };
constexpr Color RowPressed{ 0.15f, 0.19f, 0.31f, 0.22f };
} // namespace

DialogScreen::DialogScreen()
{
    BlocksInput     = true;          // the player stands still to talk
    Layer           = UILayer::Hud;  // under menus: pausing mid-conversation works
    TransitionSpeed = 8.0f;
}

void DialogScreen::Begin(std::shared_ptr<const Dialog> dialog, DialogHost host, const std::string_view node)
{
    _finished      = false;
    _finishPending = false;
    _pendingChoice = -1;
    _runner.Start(std::move(dialog), std::move(host), node);
    if (_built) ShowState();
}

void DialogScreen::OnEnter()
{
    if (!_built) {
        _roots.clear();
        if (!LoadLayout(LayoutPath)) LOG_WARN(UI, "DialogScreen: '{}' not found", LayoutPath);
        _box       = FindWidget<NineSlice>("box");
        _namePlate = FindWidget<NineSlice>("name-plate");
        _choiceBox = FindWidget<NineSlice>("choices");
        _text      = FindWidget<Label>("text");
        _name      = FindWidget<Label>("name");
        _portrait  = FindWidget<Image>("portrait");
        _arrow     = FindWidget<Image>("next-arrow");
        if (_arrow) _arrowY = _arrow->Offset.Y;
        if (_text) _textLeft = _text->Offset.X;
        if (_choiceBox) _choiceW = _choiceBox->Size.X;
        ApplyArt();
        _built = true;
    }
    ShowState();
}

void DialogScreen::ApplyArt()
{
    for (NineSlice* frame : { _box, _choiceBox })
        if (frame) frame->Texture = Art.Box, frame->Sampler = Art.Sampler;
    if (_namePlate) _namePlate->Texture = Art.NamePlate, _namePlate->Sampler = Art.Sampler;
    if (_arrow) _arrow->Texture = Art.NextArrow, _arrow->Sampler = Art.Sampler;
}

// Confirm, a click or a game's advance action: the rest of the line, then the next.
void DialogScreen::OnTick(f32 /*dt*/, const InputManager& input)
{
    _ticked = true;
    if (_focusChoices) {
        FocusAt(0);
        _focusChoices = false;
    }
    if (_runner.GetState() != DialogRunner::State::Line) return;
    bool advance = input.JustPressed(UIManager::ActionConfirm) || input.IsMouseButtonJustUp(MouseButton::Left);
    for (const ActionID action : AdvanceActions) advance = advance || input.JustPressed(action);
    if (advance) Advance();
}

void DialogScreen::OnUpdate(const f32 dt)
{
    _time += dt;
    _active = std::exchange(_ticked, false);
    if (_pendingChoice >= 0) { // a row's click, applied outside the widget update it came from
        const u32 choice = static_cast<u32>(_pendingChoice);
        _pendingChoice   = -1;
        Choose(choice);
    }
    if (_finishPending) return Finish();
    if (_arrow) {
        _arrow->Visible  = _runner.GetState() == DialogRunner::State::Line && _text && !_text->IsRevealing();
        _arrow->Offset.Y = _arrowY + std::round(std::sin(_time * 5.0f) * 3.0f); // whole pixels: pixel art
    }
    for (size_t i = 0; i < _rows.size(); ++i) _cursors[i]->Visible = _rows[i]->IsFocused() || _rows[i]->IsHovered();
    UpdateAuto(dt);
}

void DialogScreen::UpdateAuto(const f32 dt)
{
    if (!_active || _runner.GetState() != DialogRunner::State::Line || !_text || _text->IsRevealing()) return;
    if (!DialogSettings::GetAutoAdvance()) return;
    _autoWait -= dt;
    if (_autoWait <= 0.0f) Advance();
}

void DialogScreen::OnLocaleChanged()
{
    _runner.RefreshText();
    if (_text) _text->Text = _runner.GetLine().Text, _text->SkipReveal(); // the new language shows whole
    if (_name) _name->Text = _runner.GetLine().Name;
    for (size_t i = 0; i < _rows.size() && i < _runner.GetChoices().size(); ++i)
        if (auto* label = dynamic_cast<Label*>(_rows[i]->FindDescendant("choice-text"))) label->Text = _runner.GetChoices()[i].Text;
}

void DialogScreen::Advance()
{
    if (_text && _text->IsRevealing()) return _text->SkipReveal();
    _runner.Next();
    ShowState();
}

void DialogScreen::Choose(const u32 index)
{
    if (_choiceBox) _choiceBox->Visible = false;
    if (_choiceBox) _choiceBox->ClearChildren();
    _rows.clear();
    _cursors.clear();
    _runner.Choose(index);
    ShowState();
}

void DialogScreen::ShowState()
{
    switch (_runner.GetState()) {
        case DialogRunner::State::Line: return ShowLine();
        case DialogRunner::State::Choice: return ShowChoices();
        default: _finishPending = !_finished; // told next update, not from inside a widget's click
    }
}

void DialogScreen::ShowLine()
{
    const DialogRunner::Line& line = _runner.GetLine();
    if (_choiceBox) _choiceBox->Visible = false;
    if (_namePlate) _namePlate->Visible = !line.Name.empty();
    if (_name) _name->Text = line.Name;
    if (_portrait) {
        _portrait->Texture = LoadPortrait && !line.Portrait.empty() ? LoadPortrait(line.Portrait) : TextureHandle::Invalid();
        _portrait->Sampler = Art.Sampler;
        _portrait->Visible = _portrait->Texture.IsValid();
    }
    LayoutText(_portrait && _portrait->Visible);
    if (!_text) return;
    _text->Text   = line.Text;
    const f32 cps = DialogSettings::CharsPerSecond(DialogSettings::GetTextSpeed());
    if (cps <= 0.0f) _text->StopReveal(); // instant
    else {
        _text->StartReveal(cps);
        const std::string voice   = line.Voice.empty() && line.Speaker ? DefaultVoice : line.Voice;
        _text->GetReveal().OnBlip = [this, voice](u32) {
            if (PlayVoice && !voice.empty()) PlayVoice(voice);
        };
    }
    _autoWait = ReadingTime(line.Text);
}

// The text starts past the portrait when there is one.
void DialogScreen::LayoutText(const bool portrait)
{
    if (!_text || !_box) return;
    const f32 left  = portrait ? _portrait->Offset.X + _portrait->Size.X + 24.0f : GetMetaF32("text_left", _textLeft);
    _text->Offset.X = left;
    _text->Size.X   = _box->Size.X - left - GetMetaF32("text_right", 40.0f);
}

// The line stays up; the choices stack in their box above it.
void DialogScreen::ShowChoices()
{
    if (!_choiceBox) return;
    constexpr f32 Pad     = 22.0f;
    const f32     row     = GetMetaF32("choice_row", 44.0f);
    const auto&   choices = _runner.GetChoices();
    _choiceBox->ClearChildren();
    _rows.clear();
    _cursors.clear();
    _choiceBox->Size   = { ChoiceBoxWidth(), Pad * 2.0f + row * static_cast<f32>(choices.size()) };
    for (u32 i = 0; i < choices.size(); ++i) AddChoiceRow(i, choices[i].Text, row, Pad);
    _choiceBox->Visible = true;
    _focusChoices       = true;
}

// The layout's width, or wider for the longest choice (the box grows leftward).
f32 DialogScreen::ChoiceBoxWidth() const
{
    if (!Skin || !_text) return _choiceW;
    const UISkin skin = _text->GetEffectiveSkin(*Skin);
    if (!skin.Font) return _choiceW;
    f32 widest = 0.0f;
    for (const DialogRunner::Choice& choice : _runner.GetChoices())
        widest = std::max(widest, LayoutRichText(*skin.Font, choice.Text, { .Scale = skin.FontScale, .Style = skin.Text }).Size.X);
    constexpr f32 Frame = 22.0f * 2.0f + 38.0f + 16.0f; // the box's padding, the cursor, a little air
    return std::max(_choiceW, std::ceil(widest + Frame));
}

void DialogScreen::AddChoiceRow(const u32 index, const std::string& text, const f32 height, const f32 pad)
{
    auto* row                       = _choiceBox->AddChild<Button>();
    row->Size                       = { _choiceBox->Size.X - pad * 2.0f, height };
    row->Offset                     = { pad, pad + height * static_cast<f32>(index) };
    row->ZOrder                     = _choiceBox->ZOrder + 1;
    row->TabOrder                   = index;
    row->SkinOverride.ButtonNormal  = Clear;
    row->SkinOverride.ButtonHovered = RowHot;
    row->SkinOverride.ButtonPressed = RowPressed;
    row->OnClick                    = [this, index] { _pendingChoice = static_cast<i32>(index); };

    auto* cursor    = row->AddChild<Image>();
    cursor->Texture = Art.Cursor;
    cursor->Sampler = Art.Sampler;
    cursor->Size    = { 21.0f, 30.0f };
    cursor->Anchor  = cursor->Pivot = { 0.0f, 0.5f };
    cursor->Offset  = { 6.0f, 0.0f };
    cursor->ZOrder  = row->ZOrder + 1;
    cursor->Visible = false;

    auto* label                   = row->AddChild<Label>();
    label->Name                   = "choice-text";
    label->Markup                 = true;
    label->Text                   = text;
    label->Size                   = { row->Size.X - 40.0f, height - 8.0f };
    label->Anchor                 = label->Pivot = { 0.0f, 0.5f };
    label->Offset                 = { 38.0f, 0.0f };
    label->ZOrder                 = row->ZOrder + 1;
    label->SkinOverride.TextLabel = _text && _text->SkinOverride.TextLabel ? *_text->SkinOverride.TextLabel : Color{ 0.15f, 0.17f, 0.26f, 1.0f };
    if (_text) label->SkinOverride.FontScale = _text->SkinOverride.FontScale; // choices read like the line
    _rows.push_back(row);
    _cursors.push_back(cursor);
}

void DialogScreen::Finish()
{
    _finishPending = false;
    if (_finished) return;
    _finished = true;
    if (OnFinished) OnFinished(_runner.GetLastChoice());
}
} // namespace Arcbit
