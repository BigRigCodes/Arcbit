#pragma once

#include <arcbit/core/Loc.h>
#include <arcbit/ui/UIWidget.h>
#include <arcbit/ui/UISkin.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit {

struct FramePacket;
class  InputManager;

// ---------------------------------------------------------------------------
// UIScreen — a full-screen root widget container.
//
// Screens are stacked by UIManager. The top-most screen receives input; all
// visible screens are collected each frame.  Push transitions fade in; Pop
// transitions fade out before the screen is removed from the stack.
// ---------------------------------------------------------------------------
// Where a screen sits in the UI stack, bottom to top. A screen is pushed on
// top of its own layer - so a menu opened while an overlay is up goes beneath
// the overlay, and a HUD a scene pushes never covers a menu.
enum class UILayer : u8
{
    Hud,     // always-there game info: health, coins
    Menu,    // menus and their sub-screens (settings); UIManager::OpenMenu swaps them
    Overlay, // above menus: debug overlays, the loading screen
    System,  // above everything: the splash
};

class UIScreen
{
public:
    virtual ~UIScreen() = default;

    virtual void OnEnter() {}
    virtual void OnExit()  {}

    // Fired by UIManager when Escape or gamepad East is pressed and no
    // text-consuming widget is focused. Override to implement back navigation.
    virtual void OnBackPressed() {}

    // Called once per game tick before widget UpdateTree.
    // Override in screens that need direct input access (e.g. InputRebindScreen).
    // Not called on the tick the screen is pushed, so the key press that
    // opened it is never seen here.
    virtual void OnTick(f32 /*dt*/, const InputManager& /*input*/) {}

    // Called once per game tick for every screen on the stack - including
    // display-only ones that never take input (TakesInput = false), which
    // never get OnTick. Animate and refresh what the screen shows here; read
    // input in OnTick.
    virtual void OnUpdate(f32 /*dt*/) {}

    // Rate at which transition opacity changes (units/second). 0 = instant.
    f32 TransitionSpeed = 3.0f;

    // When true, UIManager::HasBlockingScreen() returns true while this screen
    // is on the stack. Use for pause menus / dialogs; leave false for HUDs.
    bool BlocksInput = false;

    // When true, Application suppresses OnUpdate, OnRender, and scene
    // update/render while this screen is on the stack. Use for splash screens
    // and any screen that must appear before game content is visible.
    bool BlocksGame = false;

    // Where the screen sits in the stack (see UILayer).
    UILayer Layer = UILayer::Menu;

    // False for display-only screens (HUDs, an event log): keys, buttons and
    // the mouse go to the topmost screen that takes input, so a menu beneath
    // stays usable.
    bool TakesInput = true;

    // For BlocksGame screens: true while the screen fully hides the game, so
    // a scene can load beneath it (the main thread stalls while it does) with
    // no visible hitch. A splash says yes only while its image is fully
    // shown, not while fading. Application holds scene swaps until every
    // game-blocking screen agrees.
    [[nodiscard]] virtual bool CoversLoading() const { return true; }

    // Add a root-level widget. Returns raw ptr; ownership stays here.
    template<typename T, typename... Args>
    T* Add(Args&&... args)
    {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = w.get();
        _roots.push_back(std::move(w));
        return ptr;
    }

    // Add a pre-constructed root widget (used by UILoader).
    UIWidget* AddRaw(std::unique_ptr<UIWidget> w);

    // Parse a .arcui JSON file and populate this screen's widget tree.
    // Clears existing roots and meta before loading. Returns false on error.
    bool LoadLayout(std::string_view path);

    // Find a named widget anywhere in the tree; returns nullptr if not found
    // or if the widget is not of type T.
    template<typename T>
    [[nodiscard]] T* FindWidget(const std::string_view name)
    {
        for (auto& r : _roots)
            if (auto* w = r->FindDescendant(name))
                return dynamic_cast<T*>(w);
        return nullptr;
    }

    // Typed accessors for the "meta" section of a loaded .arcui file.
    // Returns def if the key is absent or of the wrong type.
    [[nodiscard]] f32         GetMetaF32(std::string_view key, f32 def = 0.0f) const;
    [[nodiscard]] std::string GetMetaStr(std::string_view key, std::string_view def = "") const;

    // Called by UILoader to populate meta entries after parsing.
    void SetMetaF32(std::string_view key, f32 value);
    void SetMetaStr(std::string_view key, std::string_view value);

    // Lua handlers a loaded layout names on its widgets ("on_click": "OnBuy",
    // "on_change": "OnVolume"). A ScriptedScreen wires them to its script
    // (the layout's "script"); every other screen ignores them.
    struct ScriptHandler
    {
        UIWidget*   Widget = nullptr; // in this screen's tree, as loaded
        std::string Event;            // "click" or "change"
        std::string Function;
    };
    void                                            AddScriptHandler(ScriptHandler handler) { _scriptHandlers.push_back(std::move(handler)); }
    [[nodiscard]] const std::vector<ScriptHandler>& GetScriptHandlers() const { return _scriptHandlers; }

    // If the language changed since this screen last looked, its widgets
    // re-read their text keys, then OnLocaleChanged runs. UIManager calls it
    // for every screen, every tick.
    void RefreshLocale();

    void Update(f32 dt, UIRect screenRect, Vec2 mousePos,
                bool mouseDown, bool mouseJustDown, bool mouseJustUp, f32 scrollDelta = 0.0f);

    void Collect(FramePacket& packet, UIRect screenRect, const UISkin& skin,
                 TextureHandle whiteTex, SamplerHandle whiteSampler);

    // --- Text input routing -------------------------------------------------

    // Forward typed characters / control keys to the currently focused widget.
    void DispatchTextInput(std::string_view chars);
    void DispatchControlKey(UIControlKey key);

    // --- Focus navigation ---------------------------------------------------

    void FocusNext();
    void FocusPrev();
    void ActivateFocused();
    void ClearFocus();

    // The focused widget; null if none - or if it has been destroyed since
    // (a screen rebuilding its layout), so focus never points at freed memory.
    [[nodiscard]] UIWidget* GetFocusedWidget() const;

    // The focused widget's place in the focus order (-1 if none), and focusing
    // by place (clamped) - for keeping focus where it was across a rebuild.
    [[nodiscard]] i32 GetFocusIndex() const;
    void              FocusAt(i32 index);

    // --- Visibility / transition state --------------------------------------

    [[nodiscard]] bool IsVisible()       const { return _visible; }
    void               SetVisible(bool v)      { _visible = v; }

    [[nodiscard]] bool IsFadingOut()          const { return _transitionState == TransitionState::FadingOut; }
    [[nodiscard]] f32  GetTransitionOpacity() const { return _transitionOpacity; }

protected:
    // The language changed and the widgets' keys are re-read: rebuild text
    // the screen composed itself (Loc::Format results, lists of names).
    virtual void OnLocaleChanged() {}

    std::vector<std::unique_ptr<UIWidget>> _roots;
    u64                                    _locGeneration = Loc::GetGeneration(); // what RefreshLocale last saw

    // Close the gaps hidden buttons leave in a menu column: the visible
    // widgets named in `names`, in order, go from startY every `gap`, and
    // `panel` ends `bottom` below the last one.
    void StackButtons(UIWidget* panel, std::initializer_list<std::string_view> names, f32 startY, f32 gap,
                      f32 bottom);

private:
    enum class TransitionState { Idle, FadingIn, FadingOut };

    std::vector<ScriptHandler>                   _scriptHandlers; // LoadLayout
    std::unordered_map<std::string, f32>         _metaF32;
    std::unordered_map<std::string, std::string>  _metaStr;

    bool            _visible           = true;
    bool            _skipInputThisFrame = false; // true on the frame the screen was pushed
    TransitionState _transitionState   = TransitionState::Idle;
    f32             _transitionOpacity = 1.0f;
    mutable UIWidget* _focusedWidget = nullptr;
    mutable u64       _focusCheckedAt = 0; // UIWidget::DestroyedCount() when focus was last known valid
    mutable u64       _focusedSerial  = 0; // the focused widget's serial: tells it from one reusing its address

    void AdvanceTransition(f32 dt);
    void ApplyFocus(UIWidget* widget);

    std::vector<UIWidget*> GatherFocusables() const;
    static void CollectFocusables(UIWidget& w, std::vector<UIWidget*>& out);
    static bool InTree(const UIWidget& w, const UIWidget* target); // pointer compare only

    // Hit-test the widget tree for the deepest Focusable widget under pos.
    [[nodiscard]] UIWidget* FindFocusableHit(UIWidget& w, UIRect parent, Vec2 pos) const;

    friend class UIManager;
};

} // namespace Arcbit
