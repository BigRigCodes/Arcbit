#include <arcbit/app/Window.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

// SDL is included only in this translation unit — the public header stays clean.
#include <SDL3/SDL.h>

namespace Arcbit {

// Convenience cast — SDL_Window* travels as void* through the public API.
static SDL_Window* ToSDL(void* handle) { return static_cast<SDL_Window*>(handle); }

Window::Window(const Desc& desc)
{
    ARCBIT_VERIFY(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL_Init failed");

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (desc.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    SDL_Window* win = SDL_CreateWindow(desc.Title,
        static_cast<i32>(desc.Width),
        static_cast<i32>(desc.Height),
        flags);

    ARCBIT_ASSERT(win != nullptr, "SDL_CreateWindow failed");

    _window = win;
    _width  = desc.Width;
    _height = desc.Height;

    LOG_INFO(Platform, "Window created: '{}' ({}x{})", desc.Title, desc.Width, desc.Height);
}

Window::~Window()
{
    if (_window)
        SDL_DestroyWindow(ToSDL(_window));

    SDL_Quit();
    LOG_INFO(Platform, "Window destroyed");
}

bool Window::PollEvents()
{
    // Clear the resize flag at the start of every frame — it should only be
    // true for the single frame in which the resize event was received.
    _resizedThisFrame      = false;
    _focusChangedThisFrame = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                return false;

            case SDL_EVENT_KEY_DOWN:
                // Suppress repeat events — only forward physical press transitions.
                if (!event.key.repeat && _keyEventFn)
                    _keyEventFn(static_cast<i32>(event.key.scancode), true);
                break;

            case SDL_EVENT_KEY_UP:
                if (_keyEventFn)
                    _keyEventFn(static_cast<i32>(event.key.scancode), false);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (_mouseButtonFn)
                    _mouseButtonFn(static_cast<i32>(event.button.button), true);
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (_mouseButtonFn)
                    _mouseButtonFn(static_cast<i32>(event.button.button), false);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (_gamepadButtonFn)
                    _gamepadButtonFn(static_cast<u32>(event.gbutton.which),
                                     static_cast<i32>(event.gbutton.button), true);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (_gamepadButtonFn)
                    _gamepadButtonFn(static_cast<u32>(event.gbutton.which),
                                     static_cast<i32>(event.gbutton.button), false);
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (_scrollFn)
                    _scrollFn(event.wheel.y);
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (_textInputFn)
                    _textInputFn(event.text.text);
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                _width            = static_cast<u32>(event.window.data1);
                _height           = static_cast<u32>(event.window.data2);
                _resizedThisFrame = true;
                LOG_DEBUG(Platform, "Window resized to {}x{}", _width, _height);
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                const bool focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
                _focusChangedThisFrame |= focused != _hasFocus;
                _hasFocus = focused;
                break;
            }

            default:
                break;
        }
    }

    return true; // window is still open
}

void* Window::GetNativeHandle() const
{
    return _window;
}

void Window::ToggleFullscreen()
{
    _isFullscreen = !_isFullscreen;
    SDL_SetWindowFullscreen(ToSDL(_window), _isFullscreen);
    LOG_INFO(Platform, "Fullscreen: {}", _isFullscreen ? "on" : "off");
}

void Window::SetTitle(const std::string_view title)
{
    SDL_SetWindowTitle(ToSDL(_window), std::string(title).c_str());
}

void Window::SetKeyEventCallback(std::function<void(i32, bool)> fn)
{
    _keyEventFn = std::move(fn);
}

void Window::SetMouseButtonCallback(std::function<void(i32, bool)> fn)
{
    _mouseButtonFn = std::move(fn);
}

void Window::SetScrollCallback(std::function<void(f32)> fn)
{
    _scrollFn = std::move(fn);
}

void Window::SetGamepadButtonCallback(std::function<void(u32, i32, bool)> fn)
{
    _gamepadButtonFn = std::move(fn);
}

void Window::SetTextInputCallback(std::function<void(const char*)> fn)
{
    _textInputFn = std::move(fn);
}

void Window::SetTextInputActive(const bool active)
{
    SDL_Window* win = ToSDL(_window);
    if (active) SDL_StartTextInput(win);
    else        SDL_StopTextInput(win);
}

} // namespace Arcbit
