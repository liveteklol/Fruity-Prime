#include "InputSettings.hpp"
#include "NativeRuntime/System/Charconv.hpp"

#include "Input/GamepadAnalog.hpp"
#include "Input/GamepadOptions.hpp"
#include "Input/GamepadProfiles.hpp"
#include "Input/PadBindings.hpp"
#include "Input/PointerDevice.hpp"
#include "Input/PointerInput.hpp"
#include "Input/StylusZone.hpp"
#include "Input/TouchSettings.hpp"
#include "Mods/Network/Replay/DemoClip.hpp"
#include "Branding.hpp"
#include "../Entities/Players/PlayerEntity.hpp"
#include "Launcher/Portable/LauncherPrefs.hpp"
#include "../NativeRuntime/System/Encoding.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::BooleanTryParse;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::FileReadAllLines;
using ::MphRead::NativeRuntime::FileWriteAllLines;
using ::MphRead::NativeRuntime::Int32TryParseInvariant;
using ::MphRead::NativeRuntime::IsAndroid;
using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::StringTrimView;

namespace MphRead::Mods
{
    namespace
    {
        constexpr InputButtonType ButtonTypeKey = static_cast<InputButtonType>(0);
        constexpr InputButtonType ButtonTypeMouse = static_cast<InputButtonType>(1);
        constexpr InputButtonType ButtonTypeScrollUp = static_cast<InputButtonType>(2);
        constexpr InputButtonType ButtonTypeScrollDown = static_cast<InputButtonType>(3);

        constexpr InputKey KeyUnknown = static_cast<InputKey>(-1);
        constexpr InputKey KeyT = static_cast<InputKey>(84);
        constexpr InputKey KeyF10 = static_cast<InputKey>(299);

        constexpr InputMouseButton MouseLeft = static_cast<InputMouseButton>(0);
        constexpr InputMouseButton MouseRight = static_cast<InputMouseButton>(1);
        constexpr InputMouseButton MouseMiddle = static_cast<InputMouseButton>(2);

        struct EnumName final
        {
            std::int32_t Value;
            std::string_view Name;
        };

        constexpr EnumName KeyNames[] =
        {
            {-1, "Unknown"},
            {32, "Space"},
            {39, "Apostrophe"},
            {44, "Comma"},
            {45, "Minus"},
            {46, "Period"},
            {47, "Slash"},
            {48, "D0"},
            {49, "D1"},
            {50, "D2"},
            {51, "D3"},
            {52, "D4"},
            {53, "D5"},
            {54, "D6"},
            {55, "D7"},
            {56, "D8"},
            {57, "D9"},
            {59, "Semicolon"},
            {61, "Equal"},
            {65, "A"},
            {66, "B"},
            {67, "C"},
            {68, "D"},
            {69, "E"},
            {70, "F"},
            {71, "G"},
            {72, "H"},
            {73, "I"},
            {74, "J"},
            {75, "K"},
            {76, "L"},
            {77, "M"},
            {78, "N"},
            {79, "O"},
            {80, "P"},
            {81, "Q"},
            {82, "R"},
            {83, "S"},
            {84, "T"},
            {85, "U"},
            {86, "V"},
            {87, "W"},
            {88, "X"},
            {89, "Y"},
            {90, "Z"},
            {91, "LeftBracket"},
            {92, "Backslash"},
            {93, "RightBracket"},
            {96, "GraveAccent"},
            {256, "Escape"},
            {257, "Enter"},
            {258, "Tab"},
            {259, "Backspace"},
            {260, "Insert"},
            {261, "Delete"},
            {262, "Right"},
            {263, "Left"},
            {264, "Down"},
            {265, "Up"},
            {266, "PageUp"},
            {267, "PageDown"},
            {268, "Home"},
            {269, "End"},
            {280, "CapsLock"},
            {281, "ScrollLock"},
            {282, "NumLock"},
            {283, "PrintScreen"},
            {284, "Pause"},
            {290, "F1"},
            {291, "F2"},
            {292, "F3"},
            {293, "F4"},
            {294, "F5"},
            {295, "F6"},
            {296, "F7"},
            {297, "F8"},
            {298, "F9"},
            {299, "F10"},
            {300, "F11"},
            {301, "F12"},
            {302, "F13"},
            {303, "F14"},
            {304, "F15"},
            {305, "F16"},
            {306, "F17"},
            {307, "F18"},
            {308, "F19"},
            {309, "F20"},
            {310, "F21"},
            {311, "F22"},
            {312, "F23"},
            {313, "F24"},
            {314, "F25"},
            {320, "KeyPad0"},
            {321, "KeyPad1"},
            {322, "KeyPad2"},
            {323, "KeyPad3"},
            {324, "KeyPad4"},
            {325, "KeyPad5"},
            {326, "KeyPad6"},
            {327, "KeyPad7"},
            {328, "KeyPad8"},
            {329, "KeyPad9"},
            {330, "KeyPadDecimal"},
            {331, "KeyPadDivide"},
            {332, "KeyPadMultiply"},
            {333, "KeyPadSubtract"},
            {334, "KeyPadAdd"},
            {335, "KeyPadEnter"},
            {336, "KeyPadEqual"},
            {340, "LeftShift"},
            {341, "LeftControl"},
            {342, "LeftAlt"},
            {343, "LeftSuper"},
            {344, "RightShift"},
            {345, "RightControl"},
            {346, "RightAlt"},
            {347, "RightSuper"},
            {348, "Menu"}
        };

        constexpr EnumName KeyAliases[] =
        {
            {348, "LastKey"}
        };

        constexpr EnumName MouseButtonNames[] =
        {
            {0, "Button1"},
            {1, "Button2"},
            {2, "Button3"},
            {3, "Button4"},
            {4, "Button5"},
            {5, "Button6"},
            {6, "Button7"},
            {7, "Button8"}
        };

        constexpr EnumName MouseButtonAliases[] =
        {
            {0, "Left"},
            {1, "Right"},
            {2, "Middle"},
            {7, "Last"}
        };

        char AsciiLower(char value)
        {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value + ('a' - 'A'))
                : value;
        }

        template <typename TEnum>
        std::int32_t EnumValue(TEnum value) noexcept
        {
            using Underlying = std::underlying_type_t<TEnum>;
            return static_cast<std::int32_t>(static_cast<Underlying>(value));
        }

        std::string EnumToString(std::int32_t value,
            const EnumName* names, std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (names[i].Value == value)
                {
                    return std::string(names[i].Name);
                }
            }
            return std::to_string(value);
        }

        bool TryParseNamedEnum(std::string_view value,
            const EnumName* names, std::size_t count,
            const EnumName* aliases, std::size_t aliasCount,
            std::int32_t& parsed)
        {
            value = StringTrimView(value);
            if (value.empty())
            {
                return false;
            }

            const char first = value.front();
            if ((first >= '0' && first <= '9') || first == '+' || first == '-')
            {
                return Int32TryParseInvariant(value, parsed);
            }

            std::uint32_t combined = 0;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t comma = value.find(',', start);
                const std::size_t length = comma == std::string_view::npos
                    ? std::string_view::npos
                    : comma - start;
                const std::string_view part =
                    StringTrimView(value.substr(start, length));
                if (part.empty())
                {
                    return false;
                }

                bool found = false;
                for (std::size_t i = 0; i < count && !found; ++i)
                {
                    if (part == names[i].Name)
                    {
                        combined |= static_cast<std::uint32_t>(names[i].Value);
                        found = true;
                    }
                }
                for (std::size_t i = 0; i < aliasCount && !found; ++i)
                {
                    if (part == aliases[i].Name)
                    {
                        combined |= static_cast<std::uint32_t>(aliases[i].Value);
                        found = true;
                    }
                }
                if (!found)
                {
                    return false;
                }
                if (comma == std::string_view::npos)
                {
                    break;
                }
                start = comma + 1;
            }

            parsed = static_cast<std::int32_t>(combined);
            return true;
        }

        std::string KeyToString(InputKey key)
        {
            return EnumToString(EnumValue(key), KeyNames, std::size(KeyNames));
        }

        bool TryParseKey(std::string_view value, InputKey& parsed)
        {
            std::int32_t numeric = 0;
            if (!TryParseNamedEnum(value, KeyNames, std::size(KeyNames),
                KeyAliases, std::size(KeyAliases), numeric))
            {
                return false;
            }
            parsed = static_cast<InputKey>(numeric);
            return true;
        }

        std::string MouseButtonToString(InputMouseButton button)
        {
            return EnumToString(EnumValue(button),
                MouseButtonNames, std::size(MouseButtonNames));
        }

        bool TryParseMouseButton(std::string_view value,
            InputMouseButton& parsed)
        {
            std::int32_t numeric = 0;
            if (!TryParseNamedEnum(value,
                MouseButtonNames, std::size(MouseButtonNames),
                MouseButtonAliases, std::size(MouseButtonAliases), numeric))
            {
                return false;
            }
            parsed = static_cast<InputMouseButton>(numeric);
            return true;
        }

        std::string BoolToLower(bool value)
        {
            return value ? "true" : "false";
        }

        bool IsAsciiUpper(char value) noexcept
        {
            return value >= 'A' && value <= 'Z';
        }

        bool IsAsciiDigit(char value) noexcept
        {
            return value >= '0' && value <= '9';
        }

    }

    float InputSettings::_mouseSensitivity = 1.0F;
    bool InputSettings::_invertMouseY = false;
    bool InputSettings::_invertMouseX = false;
    bool InputSettings::_scrollAllWeapons = true;
    InputKey InputSettings::_chatKey = KeyT;
    InputKey InputSettings::_clipKey = KeyF10;
    bool InputSettings::_creating = false;
    std::unique_ptr<Entities::PlayerControls> InputSettings::_current{};
    std::optional<std::array<InputBindingProperty, 35>> InputSettings::_bindings{};

    const std::array<std::string_view, 16> InputSettings::_order =
    {
        "MoveUp", "MoveDown", "MoveLeft", "MoveRight",
        "Jump", "Boost", "Shoot", "Zoom",
        "Morph", "AltAttack", "NextWeapon", "PrevWeapon",
        "WeaponMenu", "ScanVisor", "Pause", "AdventureMapLegend"
    };

    float InputSettings::MouseSensitivity() noexcept
    {
        return _mouseSensitivity;
    }

    void InputSettings::MouseSensitivity(float value) noexcept
    {
        _mouseSensitivity = value;
    }

    bool InputSettings::InvertMouseY() noexcept
    {
        return _invertMouseY;
    }

    void InputSettings::InvertMouseY(bool value) noexcept
    {
        _invertMouseY = value;
    }

    bool InputSettings::InvertMouseX() noexcept
    {
        return _invertMouseX;
    }

    void InputSettings::InvertMouseX(bool value) noexcept
    {
        _invertMouseX = value;
    }

    bool InputSettings::ClassicAim()
    {
        return Current().NativeAim();
    }

    void InputSettings::ClassicAim(bool value)
    {
        auto& controls = Current();
        controls.SetNativeAim(value);
        // DS aim rules on the 60 Hz simulation: every pointer delta lands on
        // the step it arrives in; only the strict -nativeaim cadence waits.
        if (value) controls.NativeControl() = {};
    }

    bool InputSettings::ScrollAllWeapons() noexcept
    {
        return _scrollAllWeapons;
    }

    void InputSettings::ScrollAllWeapons(bool value) noexcept
    {
        _scrollAllWeapons = value;
    }

    InputKey InputSettings::ChatKey() noexcept
    {
        return _chatKey;
    }

    void InputSettings::ChatKey(InputKey value) noexcept
    {
        _chatKey = value;
    }

    InputKey InputSettings::ClipKey() noexcept
    {
        return _clipKey;
    }

    void InputSettings::ClipKey(InputKey value)
    {
        if (_clipKey != value)
        {
            Network::DemoClip::Purge();
        }
        _clipKey = value;
    }

    float InputSettings::GamepadDeadZone()
    {
        return Input::GamepadOptions::LeftInner();
    }

    void InputSettings::GamepadDeadZone(float value)
    {
        const float finite = Input::GamepadAnalog::Finite(value, 0, 0.9F);
        Input::GamepadOptions::RightInner(finite);
        Input::GamepadOptions::LeftInner(finite);
    }

    float InputSettings::GamepadLookSensitivity()
    {
        return Input::GamepadOptions::LookX();
    }

    void InputSettings::GamepadLookSensitivity(float value)
    {
        const float finite = Input::GamepadAnalog::Finite(value, 0.1F, 5);
        Input::GamepadOptions::LookY(finite);
        Input::GamepadOptions::LookX(finite);
    }

    bool InputSettings::GamepadInvertY()
    {
        return Input::GamepadOptions::InvertY();
    }

    void InputSettings::GamepadInvertY(bool value)
    {
        Input::GamepadOptions::InvertY(value);
    }

    Entities::PlayerControls& InputSettings::Current()
    {
        if (_current == nullptr)
        {
            _creating = true;
            auto controls = Entities::PlayerControls::GetDefault();
            _current = std::make_unique<Entities::PlayerControls>(
                std::move(controls));
            _creating = false;
        }
        return *_current;
    }

    const std::array<InputBindingProperty, 35>& InputSettings::Bindings()
    {
        if (!_bindings.has_value())
        {
            _bindings = FindBindings();
        }
        return *_bindings;
    }

    std::array<InputBindingProperty, 35> InputSettings::FindBindings()
    {
        std::array<InputBindingProperty, 35> all =
        {{
            {"MoveLeft", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.MoveLeft(); }},
            {"MoveRight", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.MoveRight(); }},
            {"MoveUp", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.MoveUp(); }},
            {"MoveDown", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.MoveDown(); }},
            {"RolltLeft", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.RolltLeft(); }},
            {"RollRight", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.RollRight(); }},
            {"RollUp", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.RollUp(); }},
            {"RollDown", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.RollDown(); }},
            {"AimLeft", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AimLeft(); }},
            {"AimRight", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AimRight(); }},
            {"AimUp", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AimUp(); }},
            {"AimDown", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AimDown(); }},
            {"Shoot", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Shoot(); }},
            {"Zoom", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Zoom(); }},
            {"Jump", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Jump(); }},
            {"Morph", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Morph(); }},
            {"Boost", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Boost(); }},
            {"AltAttack", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AltAttack(); }},
            {"ScanVisor", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.ScanVisor(); }},
            {"Scan", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Scan(); }},
            {"NextWeapon", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.NextWeapon(); }},
            {"PrevWeapon", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.PrevWeapon(); }},
            {"WeaponMenu", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.WeaponMenu(); }},
            {"PowerBeam", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.PowerBeam(); }},
            {"Missile", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Missile(); }},
            {"VoltDriver", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.VoltDriver(); }},
            {"Battlehammer", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Battlehammer(); }},
            {"Imperialist", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Imperialist(); }},
            {"Judicator", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Judicator(); }},
            {"Magmaul", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Magmaul(); }},
            {"ShockCoil", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.ShockCoil(); }},
            {"OmegaCannon", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.OmegaCannon(); }},
            {"AffinitySlot", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AffinitySlot(); }},
            {"Pause", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.Pause(); }},
            {"AdventureMapLegend", [](Entities::PlayerControls& c) -> Entities::Keybind& { return c.AdventureMapLegend(); }, "HudOverlay"}
        }};

        const auto orderIndex = [](std::string_view name)
        {
            const auto found = std::find(
                _order.begin(), _order.end(), name);
            return found == _order.end()
                ? static_cast<std::ptrdiff_t>(_order.size())
                : std::distance(_order.begin(), found);
        };

        std::stable_sort(all.begin(), all.end(),
            [&](const InputBindingProperty& left,
                const InputBindingProperty& right)
            {
                return orderIndex(left.Name) < orderIndex(right.Name);
            });
        return all;
    }

    Entities::Keybind& InputSettings::Bind(
        const InputBindingProperty& property)
    {
        return property.GetValue(Current());
    }

    std::string InputSettings::Describe(const Entities::Keybind& bind)
    {
        const InputButtonType type = bind.Type();
        if (type == ButtonTypeMouse)
        {
            const InputMouseButton button = bind.MouseButton();
            if (button == MouseLeft)
            {
                return "Mouse left";
            }
            if (button == MouseRight)
            {
                return "Mouse right";
            }
            if (button == MouseMiddle)
            {
                return "Mouse middle";
            }
            return "Mouse " + std::to_string(
                ::MphRead::NativeRuntime::UncheckedAdd(EnumValue(button), std::int32_t{1}));
        }
        if (type == ButtonTypeScrollUp)
        {
            return "Scroll up";
        }
        if (type == ButtonTypeScrollDown)
        {
            return "Scroll down";
        }
        return bind.Key() == KeyUnknown ? "unbound" : KeyName(bind.Key());
    }

    std::string InputSettings::KeyName(InputKey key)
    {
        const std::string name = KeyToString(key);
        if (name.size() == 2 && name[0] == 'D' && IsAsciiDigit(name[1]))
        {
            return std::string(1, name[1]);
        }

        std::string result;
        result.reserve(name.size() + 4);
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (i > 0 && IsAsciiUpper(name[i])
                && !IsAsciiUpper(name[i - 1]))
            {
                result.push_back(' ');
                result.push_back(AsciiLower(name[i]));
            }
            else
            {
                result.push_back(name[i]);
            }
        }
        return result;
    }

    std::string InputSettings::ActionName(
        const InputBindingProperty& property)
    {
        std::string name;
        if (property.Name == "Pause")
        {
            name = "Scoreboard";
        }
        else if (property.Name == "RolltLeft")
        {
            name = "Roll left";
        }
        else
        {
            name = std::string(property.Name);
        }

        std::string result;
        result.reserve(name.size() + 4);
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (i > 0 && IsAsciiUpper(name[i])
                && !IsAsciiUpper(name[i - 1]))
            {
                result.push_back(' ');
                result.push_back(AsciiLower(name[i]));
            }
            else if (i == 0 && name[i] >= 'a' && name[i] <= 'z')
            {
                result.push_back(
                    static_cast<char>(name[i] - ('a' - 'A')));
            }
            else
            {
                result.push_back(name[i]);
            }
        }
        return result;
    }

    void InputSettings::Rebind(const InputBindingProperty& property,
        InputButtonType type, InputKey key, InputMouseButton button)
    {
        Entities::Keybind& bind = Bind(property);
        bind.SetType(type);
        bind.SetKey(type == ButtonTypeKey ? key : KeyUnknown);
        bind.SetMouseButton(button);
    }

    void InputSettings::Apply(Entities::PlayerControls& controls)
    {
        if (_creating || _current == nullptr)
        {
            return;
        }

        for (const InputBindingProperty& property : Bindings())
        {
            const Entities::Keybind& source = property.GetValue(*_current);
            Entities::Keybind& target = property.GetValue(controls);
            target.SetType(source.Type());
            target.SetKey(source.Key());
            target.SetMouseButton(source.MouseButton());
        }
        controls.SetScrollAllWeapons(ScrollAllWeapons());
        controls.SetNativeAim(_current->NativeAim());
        controls.NativeControl() = _current->NativeControl();
    }

    void InputSettings::ApplyToPlayers()
    {
        try
        {
            const auto& players = Entities::PlayerEntity::Players();
            for (std::size_t i = 0; i < players.size(); ++i)
            {
                const auto& player = players[i];
                if (!player)
                {
                    // C# throws NullReferenceException here and the outer
                    // catch ends the push. Throwing is the mechanical native
                    // equivalent; silently skipping would change ordering.
                    throw std::runtime_error("Player entry was null.");
                }
                Apply(player->Controls());
            }
        }
        catch (...)
        {
        }
    }

    std::filesystem::path InputSettings::Path()
    {
        return std::filesystem::path(Launcher::LauncherPrefs::Directory())
            / "controls.txt";
    }

    void InputSettings::Load()
    {
        Input::GamepadProfiles::Initialize();
        if (!FileExists(PathToUtf8(Path())))
        {
            return;
        }

        try
        {
            std::optional<bool> stylusMode;
            std::optional<bool> legacyGuard;
            const std::vector<std::string> lines = FileReadAllLines(PathToUtf8(Path()));
            for (const std::string& raw : lines)
            {
                const std::string line = ::MphRead::NativeRuntime::StringTrim(raw);
                const std::size_t split = line.find('=');
                if (line.empty() || line[0] == '#'
                    || split == std::string::npos || split == 0)
                {
                    continue;
                }

                const std::string key =
                    ::MphRead::NativeRuntime::StringTrim(std::string_view(line).substr(0, split));
                const std::string value =
                    ::MphRead::NativeRuntime::StringTrim(std::string_view(line).substr(split + 1));

                if (key == "sensitivity")
                {
                    float parsed = 0.0F;
                    if (::MphRead::NativeRuntime::SingleTryParseInvariant(value, parsed))
                    {
                        MouseSensitivity(MathClamp(parsed, 0.01F, 10.0F));
                    }
                    continue;
                }

                bool boolean = false;
                if (key == "invert_y" && BooleanTryParse(value, boolean))
                {
                    InvertMouseY(boolean);
                    continue;
                }
                if (key == "aim_style")
                {
                    ClassicAim(StringEqualsOrdinalIgnoreCase(value, "classic"));
                    continue;
                }
                if (key == "invert_x" && BooleanTryParse(value, boolean))
                {
                    InvertMouseX(boolean);
                    continue;
                }
                if (key == "pointer_jump_guard"
                    && BooleanTryParse(value, boolean))
                {
                    legacyGuard = boolean;
                    Input::PointerInput::GuardJumps(boolean);
                }
                if (key == "stylus_mode" && BooleanTryParse(value, boolean))
                {
                    stylusMode = boolean;
                }
                if (key == "stylus_zone" && BooleanTryParse(value, boolean))
                {
                    Input::StylusZone::Enabled(boolean && !IsAndroid());
                }
                if (key == "stylus_zone_opacity")
                {
                    float opacity = 0.0F;
                    if (::MphRead::NativeRuntime::SingleTryParseInvariant(value, opacity))
                    {
                        Input::StylusZone::Opacity(
                            MathClamp(opacity, 0.02F, 1.0F));
                    }
                }
                if (key == "stylus_zone_rect")
                {
                    std::array<std::string_view, 3> parts{};
                    std::size_t partCount = 0;
                    std::size_t start = 0;
                    while (partCount < parts.size())
                    {
                        const std::size_t comma = value.find(',', start);
                        if (comma == std::string::npos)
                        {
                            parts[partCount++] =
                                std::string_view(value).substr(start);
                            start = std::string::npos;
                            break;
                        }
                        parts[partCount++] =
                            std::string_view(value).substr(start, comma - start);
                        start = comma + 1;
                    }
                    const bool exactlyThree = partCount == 3
                        && start == std::string::npos;
                    float left = 0.0F;
                    float top = 0.0F;
                    float width = 0.0F;
                    if (exactlyThree
                        && ::MphRead::NativeRuntime::SingleTryParseInvariant(parts[0], left)
                        && ::MphRead::NativeRuntime::SingleTryParseInvariant(parts[1], top)
                        && ::MphRead::NativeRuntime::SingleTryParseInvariant(parts[2], width))
                    {
                        Input::StylusZone::SetRect(left, top, width);
                    }
                }
                if (key == "scroll_all_weapons"
                    && BooleanTryParse(value, boolean))
                {
                    ScrollAllWeapons(boolean);
                    continue;
                }
                if (key == "gamepad")
                {
                    continue;
                }
                if (Input::PadBindings::TryLoad(key, value))
                {
                    continue;
                }
                if (key == "clip_key")
                {
                    if (StringEqualsOrdinalIgnoreCase(value, "none"))
                    {
                        _clipKey = KeyUnknown;
                    }
                    else
                    {
                        InputKey parsed = _clipKey;
                        if (TryParseKey(value, parsed))
                        {
                            _clipKey = parsed;
                        }
                    }
                    continue;
                }
                if (key == "clip_seconds")
                {
                    std::int32_t seconds = 0;
                    if (Int32TryParseInvariant(value, seconds))
                    {
                        Network::DemoClip::Seconds(seconds);
                        continue;
                    }
                }
                if (Input::TouchSettings::ReadSetting(key, value))
                {
                    continue;
                }
                if (key == "gamepad_deadzone")
                {
                    float deadZone = 0.0F;
                    if (::MphRead::NativeRuntime::SingleTryParseInvariant(value, deadZone))
                    {
                        GamepadDeadZone(deadZone);
                        continue;
                    }
                }
                if (key == "gamepad_look")
                {
                    float look = 0.0F;
                    if (::MphRead::NativeRuntime::SingleTryParseInvariant(value, look))
                    {
                        GamepadLookSensitivity(look);
                        continue;
                    }
                }
                if (key == "gamepad_invert_y"
                    && BooleanTryParse(value, boolean))
                {
                    GamepadInvertY(boolean);
                    continue;
                }
                if (key == "chat_key")
                {
                    if (StringEqualsOrdinalIgnoreCase(value, "none"))
                    {
                        ChatKey(KeyUnknown);
                    }
                    else
                    {
                        InputKey chatKey = KeyUnknown;
                        if (TryParseKey(value, chatKey))
                        {
                            ChatKey(chatKey);
                        }
                    }
                    continue;
                }

                const auto& bindings = Bindings();
                const auto property = std::find_if(
                    bindings.begin(), bindings.end(),
                    [&](const InputBindingProperty& item)
                    {
                        return item.PersistedName() == key;
                    });
                if (property != bindings.end())
                {
                    ParseBind(*property, value);
                }
            }
            // Only an explicit stylus_mode turns stylus mode on. pointer_jump_guard
            // was written true by every older build (it was the default), so
            // reading it as the stylus switch released the cursor for everybody.
            (void)legacyGuard;
            Input::PointerInput::StylusMode(!IsAndroid() && stylusMode.has_value() && *stylusMode);
            Input::GamepadOptions::Load(lines);
            Input::PadBindings::LoadSlots(lines);
            for (auto line = lines.rbegin(); line != lines.rend(); ++line)
            {
                if (line->starts_with("gamepad_preset="))
                {
                    const std::string preset = line->substr(15);
                    if (preset == "Default" || preset == "Bumper Jumper" || preset == "Southpaw"
                        || preset == "Classic" || preset == "Custom")
                    {
                        Input::PadBindings::Preset(preset);
                    }
                    break;
                }
            }
        }
        catch (...)
        {
        }
    }

    void InputSettings::ParseBind(
        const InputBindingProperty& property, std::string_view value)
    {
        const std::size_t split = value.find(':');
        const std::string type = ::MphRead::NativeRuntime::StringTrim(split == std::string_view::npos
                ? value
                : value.substr(0, split));
        const std::string name = split == std::string_view::npos
            ? std::string()
            : ::MphRead::NativeRuntime::StringTrim(value.substr(split + 1));

        if (type == "ScrollUp")
        {
            Rebind(property, ButtonTypeScrollUp, KeyUnknown, MouseLeft);
        }
        else if (type == "ScrollDown")
        {
            Rebind(property, ButtonTypeScrollDown, KeyUnknown, MouseLeft);
        }
        else if (type == "Mouse")
        {
            InputMouseButton button = MouseLeft;
            if (TryParseMouseButton(name, button))
            {
                Rebind(property, ButtonTypeMouse, KeyUnknown, button);
            }
        }
        else if (type == "Key")
        {
            InputKey key = KeyUnknown;
            if (TryParseKey(name, key))
            {
                Rebind(property, ButtonTypeKey, key, MouseLeft);
            }
        }
    }

    void InputSettings::Save()
    {
        try
        {
            std::vector<std::string> lines =
            {
                "# " + std::string(Branding::Name)
                    + " controls. Delete a line to go back to the default.",
                "sensitivity=" + ::MphRead::NativeRuntime::ToStringInvariant(MouseSensitivity(), "0.###"),
                "invert_y=" + BoolToLower(InvertMouseY()),
                "invert_x=" + BoolToLower(InvertMouseX()),
                "aim_style=" + std::string(ClassicAim() ? "classic" : "modern"),
                "scroll_all_weapons=" + BoolToLower(ScrollAllWeapons()),
                "stylus_mode=" + BoolToLower(Input::PointerInput::StylusMode()),
                "pointer_jump_guard="
                    + BoolToLower(Input::PointerInput::GuardJumps()),
                // What was asked for, not what is in force: the zone's
                // switch survives stylus mode being turned off and on.
                "stylus_zone="
                    + BoolToLower(Input::StylusZone::Wanted()),
                "stylus_zone_opacity="
                    + ::MphRead::NativeRuntime::ToStringInvariant(Input::StylusZone::Opacity(), "0.###"),
                "stylus_zone_rect="
                    + ::MphRead::NativeRuntime::ToStringInvariant(Input::StylusZone::Left(), "0.####") + ","
                    + ::MphRead::NativeRuntime::ToStringInvariant(Input::StylusZone::Top(), "0.####") + ","
                    + ::MphRead::NativeRuntime::ToStringInvariant(Input::StylusZone::Width(), "0.####"),
                "chat_key="
                    + (ChatKey() == KeyUnknown
                        ? std::string("none")
                        : KeyToString(ChatKey())),
                "clip_key="
                    + (ClipKey() == KeyUnknown
                        ? std::string("none")
                        : KeyToString(ClipKey())),
                "clip_seconds="
                    + std::to_string(Network::DemoClip::Seconds()),
                "gamepad_deadzone="
                    + ::MphRead::NativeRuntime::ToStringInvariant(GamepadDeadZone()),
                "gamepad_look="
                    + ::MphRead::NativeRuntime::ToStringInvariant(GamepadLookSensitivity()),
                "gamepad_invert_y="
                    + BoolToLower(GamepadInvertY())
            };

            Input::PadBindings::Write(lines);

            Input::TouchSettings::WriteSettings(lines);

            for (const InputBindingProperty& property : Bindings())
            {
                const Entities::Keybind& bind = Bind(property);
                std::string value;
                if (bind.Type() == ButtonTypeMouse)
                {
                    value = "Mouse:" + MouseButtonToString(bind.MouseButton());
                }
                else if (bind.Type() == ButtonTypeScrollUp)
                {
                    value = "ScrollUp";
                }
                else if (bind.Type() == ButtonTypeScrollDown)
                {
                    value = "ScrollDown";
                }
                else
                {
                    value = "Key:" + KeyToString(bind.Key());
                }
                lines.push_back(std::string(property.PersistedName()) + "=" + value);
            }
            Input::GamepadOptions::Write(lines);
            lines.push_back("gamepad_preset=" + Input::PadBindings::Preset());
            // Retain keys from newer versions and extensions when updating known settings.
            std::set<std::string> keys;
            for (const std::string& line : lines)
            {
                const std::size_t split = line.find('=');
                if (split != std::string::npos)
                {
                    keys.insert(::MphRead::NativeRuntime::StringTrim(line.substr(0, split)));
                }
            }
            if (FileExists(PathToUtf8(Path())))
            {
                for (const std::string& original : FileReadAllLines(PathToUtf8(Path())))
                {
                    const std::size_t split = original.find('=');
                    if (split != std::string::npos && split > 0
                        && !keys.contains(::MphRead::NativeRuntime::StringTrim(original.substr(0, split))))
                    {
                        lines.push_back(original);
                    }
                }
            }

            FileWriteAllLines(PathToUtf8(Path()), lines);
        }
        catch (...)
        {
        }
    }

    void InputSettings::Reset()
    {
        _creating = true;
        auto controls = Entities::PlayerControls::GetDefault();
        _current = std::make_unique<Entities::PlayerControls>(
            std::move(controls));
        _creating = false;
        MouseSensitivity(1.0F);
        InvertMouseY(false);
        InvertMouseX(false);
        ScrollAllWeapons(true);
        ChatKey(KeyT);
        ClipKey(KeyF10);
        Network::DemoClip::Seconds(10);
        Input::PadBindings::Reset();
        Input::TouchSettings::Reset();
        Input::PointerInput::StylusMode(false);
        Input::PointerInput::GuardJumps(true);
        Input::StylusZone::Enabled(false);
        Input::PointerDevice::Reset();
        Input::GamepadOptions::Reset();
        GamepadDeadZone(0.2F);
        GamepadLookSensitivity(1.0F);
        GamepadInvertY(false);
    }
}
