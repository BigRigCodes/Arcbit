#include <arcbit/settings/Settings.h>
#include <arcbit/input/InputManager.h>
#include <arcbit/input/InputTypes.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

GraphicsSettings Settings::Graphics{};
AudioSettings    Settings::Audio{};
bool             Settings::s_dirty   = false;

Settings::ChangeListener Settings::s_changeListener;
std::string      Settings::s_filePath;

std::unordered_map<std::string, Settings::Value> Settings::s_store;

void Settings::MarkDirty(const std::string_view section)
{
    s_dirty = true;
    if (!section.empty() && s_changeListener) s_changeListener(section, {});
}

// ---------------------------------------------------------------------------
// Input section
//
// The "input" JSON node is kept separately from the variant store because
// it has a nested structure (action name → array of binding objects) that
// doesn't fit cleanly into a flat key/value map. It's merged into the
// document only during WriteFile().
// ---------------------------------------------------------------------------

static nlohmann::json s_InputSection;   // populated by SaveInputBindings / ReadFile

// ---------------------------------------------------------------------------
// Enum ↔ string lookup tables
//
// Used when serializing / deserializing input bindings. The names match the
// C++ enum value names exactly (e.g. Key::A → "A", GamepadAxis::LeftX → "LeftX")
// so the JSON file is human-readable and easy to edit by hand.
// ---------------------------------------------------------------------------

namespace {

// Key — only the entries needed for typical game input are listed first;
// the full set is included so any binding survives a round-trip through JSON.
constexpr std::pair<std::string_view, Key> KeyNames[] = {
    // Letters
    { "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D },
    { "E", Key::E }, { "F", Key::F }, { "G", Key::G }, { "H", Key::H },
    { "I", Key::I }, { "J", Key::J }, { "K", Key::K }, { "L", Key::L },
    { "M", Key::M }, { "N", Key::N }, { "O", Key::O }, { "P", Key::P },
    { "Q", Key::Q }, { "R", Key::R }, { "S", Key::S }, { "T", Key::T },
    { "U", Key::U }, { "V", Key::V }, { "W", Key::W }, { "X", Key::X },
    { "Y", Key::Y }, { "Z", Key::Z },
    // Number row
    { "Num0", Key::Num0 }, { "Num1", Key::Num1 }, { "Num2", Key::Num2 },
    { "Num3", Key::Num3 }, { "Num4", Key::Num4 }, { "Num5", Key::Num5 },
    { "Num6", Key::Num6 }, { "Num7", Key::Num7 }, { "Num8", Key::Num8 },
    { "Num9", Key::Num9 },
    // Function keys
    { "F1",  Key::F1  }, { "F2",  Key::F2  }, { "F3",  Key::F3  },
    { "F4",  Key::F4  }, { "F5",  Key::F5  }, { "F6",  Key::F6  },
    { "F7",  Key::F7  }, { "F8",  Key::F8  }, { "F9",  Key::F9  },
    { "F10", Key::F10 }, { "F11", Key::F11 }, { "F12", Key::F12 },
    // Navigation
    { "Left",     Key::Left     }, { "Right",    Key::Right    },
    { "Up",       Key::Up       }, { "Down",     Key::Down     },
    { "Home",     Key::Home     }, { "End",      Key::End      },
    { "PageUp",   Key::PageUp   }, { "PageDown", Key::PageDown },
    { "Insert",   Key::Insert   }, { "Delete",   Key::Delete   },
    // Modifiers
    { "LeftShift",  Key::LeftShift  }, { "RightShift",  Key::RightShift  },
    { "LeftCtrl",   Key::LeftCtrl   }, { "RightCtrl",   Key::RightCtrl   },
    { "LeftAlt",    Key::LeftAlt    }, { "RightAlt",    Key::RightAlt    },
    { "LeftSuper",  Key::LeftSuper  }, { "RightSuper",  Key::RightSuper  },
    // Special
    { "Space",       Key::Space       }, { "Enter",      Key::Enter      },
    { "Escape",      Key::Escape      }, { "Tab",        Key::Tab        },
    { "Backspace",   Key::Backspace   }, { "CapsLock",   Key::CapsLock   },
    { "NumLock",     Key::NumLock     }, { "ScrollLock", Key::ScrollLock },
    { "PrintScreen", Key::PrintScreen }, { "Pause",      Key::Pause      },
    // Punctuation
    { "Comma",        Key::Comma        }, { "Period",      Key::Period      },
    { "Slash",        Key::Slash        }, { "Backslash",   Key::Backslash   },
    { "Semicolon",    Key::Semicolon    }, { "Apostrophe",  Key::Apostrophe  },
    { "Grave",        Key::Grave        }, { "LeftBracket", Key::LeftBracket },
    { "RightBracket", Key::RightBracket }, { "Minus",       Key::Minus       },
    { "Equals",       Key::Equals       },
    // Numpad
    { "Kp0", Key::Kp0 }, { "Kp1", Key::Kp1 }, { "Kp2", Key::Kp2 },
    { "Kp3", Key::Kp3 }, { "Kp4", Key::Kp4 }, { "Kp5", Key::Kp5 },
    { "Kp6", Key::Kp6 }, { "Kp7", Key::Kp7 }, { "Kp8", Key::Kp8 },
    { "Kp9", Key::Kp9 },
    { "KpPlus",     Key::KpPlus     }, { "KpMinus",    Key::KpMinus    },
    { "KpMultiply", Key::KpMultiply }, { "KpDivide",   Key::KpDivide   },
    { "KpEnter",    Key::KpEnter    }, { "KpPeriod",   Key::KpPeriod   },
};

constexpr std::pair<std::string_view, MouseButton> MouseButtonNames[] = {
    { "Left", MouseButton::Left }, { "Middle", MouseButton::Middle },
    { "Right", MouseButton::Right }, { "X1", MouseButton::X1 }, { "X2", MouseButton::X2 },
};

constexpr std::pair<std::string_view, GamepadButton> GamepadButtonNames[] = {
    { "South",         GamepadButton::South         },
    { "East",          GamepadButton::East          },
    { "West",          GamepadButton::West          },
    { "North",         GamepadButton::North         },
    { "LeftShoulder",  GamepadButton::LeftShoulder  },
    { "RightShoulder", GamepadButton::RightShoulder },
    { "LeftStick",     GamepadButton::LeftStick     },
    { "RightStick",    GamepadButton::RightStick    },
    { "Start",         GamepadButton::Start         },
    { "Back",          GamepadButton::Back          },
    { "Guide",         GamepadButton::Guide         },
    { "DPadUp",        GamepadButton::DPadUp        },
    { "DPadDown",      GamepadButton::DPadDown      },
    { "DPadLeft",      GamepadButton::DPadLeft      },
    { "DPadRight",     GamepadButton::DPadRight     },
};

constexpr std::pair<std::string_view, AxisHalf> AxisHalfNames[] = {
    { "Full",     AxisHalf::Full     },
    { "Positive", AxisHalf::Positive },
    { "Negative", AxisHalf::Negative },
};

constexpr std::pair<std::string_view, GamepadAxis> GamepadAxisNames[] = {
    { "LeftX",        GamepadAxis::LeftX        },
    { "LeftY",        GamepadAxis::LeftY        },
    { "RightX",       GamepadAxis::RightX       },
    { "RightY",       GamepadAxis::RightY       },
    { "LeftTrigger",  GamepadAxis::LeftTrigger  },
    { "RightTrigger", GamepadAxis::RightTrigger },
};

// Generic reverse-lookup: searches a constexpr array for a string and returns
// the matching enum value, or defaultVal if not found.
template<typename TEnum, std::size_t N>
TEnum FindByName(const std::pair<std::string_view, TEnum> (&table)[N],
                 std::string_view name, TEnum defaultVal)
{
    for (auto& [n, v] : table)
        if (n == name) return v;
    return defaultVal;
}

template<typename TEnum, std::size_t N>
std::string_view FindName(const std::pair<std::string_view, TEnum> (&table)[N],
                          TEnum value)
{
    for (auto& [n, v] : table)
        if (v == value) return n;
    return "Unknown";
}

// Serialize a single Binding to a JSON object.
nlohmann::json SerializeBinding(const Binding& b)
{
    nlohmann::json obj;
    switch (b.BindingType)
    {
        case Binding::Type::Key:
            obj["type"] = "Key";
            obj["key"]  = FindName(KeyNames, b.BoundKey);
            break;
        case Binding::Type::MouseButton:
            obj["type"]   = "MouseButton";
            obj["button"] = FindName(MouseButtonNames, b.BoundMouse);
            break;
        case Binding::Type::GamepadButton:
            obj["type"]   = "GamepadButton";
            obj["button"] = FindName(GamepadButtonNames, b.BoundButton);
            break;
        case Binding::Type::GamepadAxis:
            obj["type"]     = "GamepadAxis";
            obj["axis"]     = FindName(GamepadAxisNames, b.BoundAxis);
            obj["deadzone"] = b.Deadzone;
            if (b.Half != AxisHalf::Full) obj["half"] = FindName(AxisHalfNames, b.Half);
            break;
    }
    return obj;
}

// Deserialize a JSON object back into a Binding.
// Returns false and leaves out unchanged if the object is malformed.
bool DeserializeBinding(const nlohmann::json& obj, Binding& out)
{
    if (!obj.contains("type") || !obj["type"].is_string())
        return false;

    const std::string type = obj["type"].get<std::string>();

    if (type == "Key")
    {
        if (!obj.contains("key")) return false;
        const Key k = FindByName(KeyNames, obj["key"].get<std::string>(), Key::Unknown);
        if (k == Key::Unknown) return false;
        out = Binding::FromKey(k);
        return true;
    }
    if (type == "MouseButton")
    {
        if (!obj.contains("button")) return false;
        out = Binding::FromMouseButton(
            FindByName(MouseButtonNames, obj["button"].get<std::string>(), MouseButton::Left));
        return true;
    }
    if (type == "GamepadButton")
    {
        if (!obj.contains("button")) return false;
        out = Binding::FromGamepadButton(
            FindByName(GamepadButtonNames, obj["button"].get<std::string>(), GamepadButton::South));
        return true;
    }
    if (type == "GamepadAxis")
    {
        if (!obj.contains("axis")) return false;
        const f32      deadzone = obj.value("deadzone", 0.15f);
        const AxisHalf half     = FindByName(AxisHalfNames, obj.value("half", std::string("Full")), AxisHalf::Full);
        out = Binding::FromGamepadAxis(
            FindByName(GamepadAxisNames, obj["axis"].get<std::string>(), GamepadAxis::LeftX),
            half, deadzone);
        return true;
    }

    return false;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Settings::Init(const std::string_view path)
{
    s_filePath = std::string(path);
    s_dirty    = false;
    s_store.clear();
    s_InputSection = {};

    ReadFile();
    LOG_INFO(Engine, "Settings initialized from '{}'", s_filePath);
}

void Settings::Shutdown()
{
    if (s_dirty)
        Flush();

    s_store.clear();
    s_InputSection = {};
}

void Settings::Flush()
{
    SyncToStore();
    WriteFile();
    s_dirty = false;
    LOG_INFO(Engine, "Settings saved to '{}'", s_filePath);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

void Settings::ReadFile()
{
    std::ifstream file(s_filePath);
    if (!file.is_open())
    {
        // First run — no file yet. Defaults from the structs are already set.
        LOG_INFO(Engine, "Settings file '{}' not found, using defaults", s_filePath);
        return;
    }

    nlohmann::json doc;
    try
    {
        file >> doc;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        LOG_WARN(Engine, "Failed to parse settings file: {}", e.what());
        return;
    }

    // --- Graphics ---
    if (doc.contains("graphics") && doc["graphics"].is_object())
    {
        const auto& g    = doc["graphics"];
        Graphics.VSync            = g.value("vsync",             Graphics.VSync);
        Graphics.ResolutionWidth  = g.value("resolution_width",  Graphics.ResolutionWidth);
        Graphics.ResolutionHeight = g.value("resolution_height", Graphics.ResolutionHeight);
        Graphics.Fullscreen       = g.value("fullscreen",        Graphics.Fullscreen);
        Graphics.FpsLimit         = g.value("fps_limit",         Graphics.FpsLimit);
        Graphics.ShowFps          = g.value("show_fps",          Graphics.ShowFps);
        Graphics.ShowDebugInfo    = g.value("show_debug_info",   Graphics.ShowDebugInfo);
    }

    // --- Audio ---
    if (doc.contains("audio") && doc["audio"].is_object())
    {
        const auto& a = doc["audio"];
        Audio.MasterVolume = a.value("master_volume", Audio.MasterVolume);
        Audio.MusicVolume  = a.value("music_volume",  Audio.MusicVolume);
        Audio.SfxVolume    = a.value("sfx_volume",    Audio.SfxVolume);
    }

    // --- Input ---
    if (doc.contains("input") && doc["input"].is_object())
        s_InputSection = doc["input"];

    // --- Custom key/value store ---
    if (doc.contains("custom") && doc["custom"].is_object())
    {
        for (const auto& [key, val] : doc["custom"].items())
        {
            // Unsigned first: nlohmann reports every non-negative integer as
            // both unsigned and integer, and reading a large u32 as i32 wraps.
            // Get<T> converts between the numeric types on the way out.
            if      (val.is_boolean())        s_store[key] = val.get<bool>();
            else if (val.is_number_unsigned()) s_store[key] = val.get<u32>();
            else if (val.is_number_integer())  s_store[key] = val.get<i32>();
            else if (val.is_number_float())    s_store[key] = val.get<f32>();
            else if (val.is_string())          s_store[key] = val.get<std::string>();
            // Nested objects / arrays are not supported in the flat store.
        }
    }
}

void Settings::WriteFile()
{
    nlohmann::json doc;

    // --- Graphics ---
    doc["graphics"]["vsync"]             = Graphics.VSync;
    doc["graphics"]["resolution_width"]  = Graphics.ResolutionWidth;
    doc["graphics"]["resolution_height"] = Graphics.ResolutionHeight;
    doc["graphics"]["fullscreen"]        = Graphics.Fullscreen;
    doc["graphics"]["fps_limit"]         = Graphics.FpsLimit;
    doc["graphics"]["show_fps"]          = Graphics.ShowFps;
    doc["graphics"]["show_debug_info"]   = Graphics.ShowDebugInfo;

    // --- Audio ---
    doc["audio"]["master_volume"] = Audio.MasterVolume;
    doc["audio"]["music_volume"]  = Audio.MusicVolume;
    doc["audio"]["sfx_volume"]    = Audio.SfxVolume;

    // --- Input ---
    if (!s_InputSection.is_null())
        doc["input"] = s_InputSection;

    // --- Custom store ---
    for (const auto& [key, val] : s_store)
    {
        std::visit([&](auto&& v) { doc["custom"][key] = v; }, val);
    }

    std::ofstream file(s_filePath);
    if (!file.is_open())
    {
        LOG_ERROR(Engine, "Failed to open '{}' for writing", s_filePath);
        return;
    }

    file << doc.dump(4); // 4-space indent for human readability
}

// ---------------------------------------------------------------------------
// Sync stubs
//
// SyncToStore / SyncFromStore exist so the Application (Phase 11) can push
// runtime changes (e.g. the user toggled fullscreen) back into the store
// before writing. Currently the structs ARE the source of truth — these are
// no-ops until a use case requires the reverse direction.
// ---------------------------------------------------------------------------

void Settings::SyncFromStore() {}
void Settings::SyncToStore()   {}

// ---------------------------------------------------------------------------
// Input binding persistence
// ---------------------------------------------------------------------------

void Settings::SaveInputBindings(const InputManager& input)
{
    nlohmann::json bindings = nlohmann::json::object();

    for (const ActionID id : input.GetAllActions())
    {
        const std::string_view name = input.GetActionName(id);
        if (name.empty() || name.starts_with("UI_"))
            continue; // skip unnamed and internal UI_ actions

        nlohmann::json bindingArray = nlohmann::json::array();
        for (const Binding& b : input.GetBindings(id))
            bindingArray.push_back(SerializeBinding(b));

        bindings[std::string(name)] = std::move(bindingArray);
    }

    LOG_INFO(Engine, "Saved {} input bindings", bindings.size());
    s_InputSection["bindings"] = std::move(bindings);
    s_dirty = true;
}

bool Settings::LoadInputBindings(InputManager& input)
{
    if (!s_InputSection.contains("bindings") || !s_InputSection["bindings"].is_object())
        return false;

    bool anyLoaded = false;

    for (const ActionID id : input.GetAllActions())
    {
        const std::string_view name = input.GetActionName(id);
        if (name.empty() || name.starts_with("UI_"))
            continue; // internal UI_ actions always use their default bindings

        const std::string nameStr(name);
        if (!s_InputSection["bindings"].contains(nameStr))
            continue;

        const auto& arr = s_InputSection["bindings"][nameStr];
        if (!arr.is_array())
            continue;

        // Replace existing bindings with the saved ones.
        input.ClearBindings(id);
        for (const auto& obj : arr)
        {
            if (Binding b; DeserializeBinding(obj, b))
            {
                // Re-add using the appropriate typed bind method so InputManager's
                // internal state stays consistent.
                switch (b.BindingType)
                {
                    case Binding::Type::Key:
                        input.BindKey(id, b.BoundKey);
                        break;
                    case Binding::Type::MouseButton:
                        input.BindMouseButton(id, b.BoundMouse);
                        break;
                    case Binding::Type::GamepadButton:
                        input.BindGamepadButton(id, b.BoundButton);
                        break;
                    case Binding::Type::GamepadAxis:
                        input.BindGamepadAxis(id, b.BoundAxis, b.Half, b.Deadzone);
                        break;
                }
                LOG_DEBUG(Engine, "Loaded binding for action '{}': {}", name, obj.dump());
                anyLoaded = true;
            }
        }
    }

    if (anyLoaded)
        LOG_INFO(Engine, "Loaded {} input bindings", s_InputSection["bindings"].size());

    return anyLoaded;
}

} // namespace Arcbit
