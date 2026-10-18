#pragma once

#include <arcbit/core/Types.h>

#include <functional>
#include <string_view>

#include <string>

#include <vector>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Window
//
// RAII wrapper around an SDL_Window. Owns the SDL video subsystem for its
// lifetime — SDL_Init is called in the constructor, SDL_Quit in the destructor.
//
// SDL3 is the single cross-platform windowing layer for all backends:
//   - Vulkan backend calls SDL_Vulkan_CreateSurface on the SDL_Window*.
//   - A future D3D12 backend would extract the HWND via SDL_GetPointerProperty.
// Neither backend type requires its own window abstraction — the variation
// point is in RenderDevice, not here.
//
// SDL types are intentionally kept out of this header so consumers don't need
// to include SDL3/SDL.h. GetNativeHandle() returns the SDL_Window* as void*.
//
// Usage:
//   Window::Desc desc{ .Title = "My Game", .Width = 1280, .Height = 720 };
//   Window window(desc);
//
//   while (window.PollEvents())
//   {
//       if (window.WasResizedThisFrame()) { /* recreate swapchain */ }
//       // build and submit FramePacket ...
//   }
// ---------------------------------------------------------------------------
class Window
{
public:
    struct Desc
    {
        const char* Title     = "Arcbit";
        u32         Width     = 1280;
        u32         Height    = 720;
        bool        Resizable = true;
        i32         Display   = -1; // which monitor (0 = the first) to center on; -1 = the OS's choice
    };

    explicit Window(const Desc& desc);

    // The user's languages, most preferred first, as locale codes ("fr-CA",
    // "ja") - from the OS settings; empty if it doesn't say.
    [[nodiscard]] static std::vector<std::string> GetPreferredLanguages();
    ~Window();

    // Non-copyable — owns an OS window handle.
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // ---------------------------------------------------------------------------
    // Per-frame update
    //
    // Drains the SDL event queue. Returns false when the window has been closed
    // (SDL_EVENT_QUIT received) or Escape was pressed, signalling the game loop
    // to exit. Window dimensions and the resize flag are updated here.
    //
    // Must be called from the thread that created the window (SDL requirement).
    // ---------------------------------------------------------------------------
    bool PollEvents();

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------

    // Current client area dimensions. Updated inside PollEvents on resize.
    [[nodiscard]] constexpr u32 GetWidth()  const { return _width; }
    [[nodiscard]] constexpr u32 GetHeight() const { return _height; }

    // Toggle between windowed and borderless fullscreen. F11 also triggers this
    // automatically inside PollEvents — no game code required.
    void ToggleFullscreen();

    // Replace the title bar text.
    void SetTitle(std::string_view title);

    // True if a SDL_EVENT_WINDOW_RESIZED event was processed in the most recent
    // PollEvents call. Cleared at the start of every PollEvents call, so it is
    // only ever true for one frame — the frame the resize was detected.
    [[nodiscard]] constexpr bool WasResizedThisFrame() const { return _resizedThisFrame; }

    // Keyboard focus: whether the window has it, and whether that changed in
    // the most recent PollEvents call (alt-tab away or back).
    [[nodiscard]] constexpr bool HasFocus() const { return _hasFocus; }
    [[nodiscard]] constexpr bool WasFocusChangedThisFrame() const { return _focusChangedThisFrame; }

    // Returns the underlying SDL_Window* as void* so that render backends can
    // cast it back without this header pulling in SDL3/SDL.h.
    // RenderDevice and SwapchainDesc accept this as NativeWindowHandle.
    [[nodiscard]] void* GetNativeHandle() const;

    // ---------------------------------------------------------------------------
    // Input event routing
    //
    // PollEvents fires these callbacks for each raw input event it encounters
    // while draining the SDL event queue. Parameters use opaque ints to keep
    // SDL3/SDL.h out of this header — InputManager casts them back internally.
    //
    //   Key callback:     (SDL_Scancode as i32, isDown)
    //   Mouse callback:   (SDL button index as i32, isDown)
    //   Gamepad callback: (SDL_JoystickID as u32, SDL_GamepadButton as i32, isDown)
    //
    // Key repeat events (held key auto-fire) are suppressed — only physical
    // press/release transitions are forwarded.
    // ---------------------------------------------------------------------------
    void SetKeyEventCallback     (std::function<void(i32 scancode, bool down)>           fn);
    void SetMouseButtonCallback  (std::function<void(i32 button,   bool down)>           fn);
    void SetGamepadButtonCallback(std::function<void(u32 joystickId, i32 button, bool down)> fn);
    void SetScrollCallback       (std::function<void(f32 deltaY)>                        fn);

    // Fires for each SDL_EVENT_TEXT_INPUT event — UTF-8 composed characters.
    void SetTextInputCallback    (std::function<void(const char* utf8)>                  fn);

    // Enable or disable SDL text input mode. When active, the OS routes
    // composed characters through SDL_EVENT_TEXT_INPUT; on Windows this also
    // activates TSF which can intercept Enter. Disable when no text widget is
    // focused so Enter reliably reaches SDL_EVENT_KEY_DOWN.
    void SetTextInputActive(bool active);

private:
    // SDL_Window* stored as void* to avoid including SDL3/SDL.h in this header.
    // Cast back to SDL_Window* in Window.cpp where SDL is included.
    void* _window                = nullptr;

    u32   _width                 = 0;
    u32   _height                = 0;
    bool  _resizedThisFrame      = false;
    bool  _hasFocus              = true;
    bool  _focusChangedThisFrame = false;
    bool  _isFullscreen          = false;

    std::function<void(i32, bool)>            _keyEventFn;
    std::function<void(i32, bool)>            _mouseButtonFn;
    std::function<void(u32, i32, bool)>       _gamepadButtonFn;
    std::function<void(f32)>                  _scrollFn;
    std::function<void(const char*)>          _textInputFn;
};

} // namespace Arcbit
