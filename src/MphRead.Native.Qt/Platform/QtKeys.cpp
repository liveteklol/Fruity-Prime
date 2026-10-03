#include "QtKeys.hpp"

#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>

#include <array>
#include <cstdint>

namespace
{
    // GLFW key codes.
    enum : int
    {
        Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        D0 = 48, Semicolon = 59, Equal = 61, A = 65, LeftBracket = 91, Backslash = 92,
        RightBracket = 93, Grave = 96, World2 = 162,
        Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
        Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267,
        Home = 268, End = 269, CapsLock = 280, ScrollLock = 281, NumLock = 282,
        PrintScreen = 283, Pause = 284, F1 = 290, F11 = 300, F13 = 302,
        Kp0 = 320, Kp1 = 321, Kp2 = 322, Kp3 = 323, Kp4 = 324, Kp5 = 325, Kp6 = 326,
        Kp7 = 327, Kp8 = 328, Kp9 = 329, KpDecimal = 330, KpDivide = 331,
        KpMultiply = 332, KpSubtract = 333, KpAdd = 334, KpEnter = 335, KpEqual = 336,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
        RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347, Menu = 348
    };

    // PC set-1 scan codes 0x00-0x58, which are also Linux evdev codes 0-88.
    constexpr std::array<int, 0x59> BaseTable = []
    {
        std::array<int, 0x59> t{};
        for (int& v : t)
        {
            v = -1;
        }
        t[0x01] = Escape;
        for (int i = 0; i < 9; ++i)
        {
            t[0x02 + i] = D0 + 1 + i;
        }
        t[0x0B] = D0;
        t[0x0C] = Minus; t[0x0D] = Equal; t[0x0E] = Backspace; t[0x0F] = Tab;
        const char row1[] = "QWERTYUIOP";
        for (int i = 0; i < 10; ++i)
        {
            t[0x10 + i] = row1[i];
        }
        t[0x1A] = LeftBracket; t[0x1B] = RightBracket; t[0x1C] = Enter; t[0x1D] = LeftControl;
        const char row2[] = "ASDFGHJKL";
        for (int i = 0; i < 9; ++i)
        {
            t[0x1E + i] = row2[i];
        }
        t[0x27] = Semicolon; t[0x28] = Apostrophe; t[0x29] = Grave; t[0x2A] = LeftShift;
        t[0x2B] = Backslash;
        const char row3[] = "ZXCVBNM";
        for (int i = 0; i < 7; ++i)
        {
            t[0x2C + i] = row3[i];
        }
        t[0x33] = Comma; t[0x34] = Period; t[0x35] = Slash; t[0x36] = RightShift;
        t[0x37] = KpMultiply; t[0x38] = LeftAlt; t[0x39] = Space; t[0x3A] = CapsLock;
        for (int i = 0; i < 10; ++i)
        {
            t[0x3B + i] = F1 + i;
        }
        t[0x45] = NumLock; t[0x46] = ScrollLock;
        t[0x47] = Kp7; t[0x48] = Kp8; t[0x49] = Kp9; t[0x4A] = KpSubtract;
        t[0x4B] = Kp4; t[0x4C] = Kp5; t[0x4D] = Kp6; t[0x4E] = KpAdd;
        t[0x4F] = Kp1; t[0x50] = Kp2; t[0x51] = Kp3; t[0x52] = Kp0; t[0x53] = KpDecimal;
        t[0x56] = World2; t[0x57] = F11; t[0x58] = F11 + 1;
        return t;
    }();

    [[nodiscard]] int FromBase(unsigned code) noexcept
    {
        return code < BaseTable.size() ? BaseTable[code] : -1;
    }

    // Linux evdev codes above the set-1 range.
    [[maybe_unused]] [[nodiscard]] int FromEvdev(unsigned code) noexcept
    {
        if (code < 89)
        {
            return FromBase(code);
        }
        switch (code)
        {
        case 96: return KpEnter;
        case 97: return RightControl;
        case 98: return KpDivide;
        case 99: return PrintScreen;
        case 100: return RightAlt;
        case 102: return Home;
        case 103: return Up;
        case 104: return PageUp;
        case 105: return Left;
        case 106: return Right;
        case 107: return End;
        case 108: return Down;
        case 109: return PageDown;
        case 110: return Insert;
        case 111: return Delete;
        case 117: return KpEqual;
        case 119: return Pause;
        case 125: return LeftSuper;
        case 126: return RightSuper;
        case 127: return Menu;
        default:
            if (code >= 183 && code <= 194)
            {
                return F13 + static_cast<int>(code - 183);
            }
            return -1;
        }
    }

    // Windows scan codes, with the extended-key bit as 0x100 (as Qt gives it).
    [[maybe_unused]] [[nodiscard]] int FromWindows(unsigned code) noexcept
    {
        if (code == 0x45)
        {
            return Pause;
        }
        if (code < 0x100)
        {
            if (code >= 0x64 && code <= 0x6E)
            {
                return F13 + static_cast<int>(code - 0x64);
            }
            return FromBase(code);
        }
        switch (code)
        {
        case 0x11C: return KpEnter;
        case 0x11D: return RightControl;
        case 0x135: return KpDivide;
        case 0x137: return PrintScreen;
        case 0x138: return RightAlt;
        case 0x145: return NumLock;
        case 0x147: return Home;
        case 0x148: return Up;
        case 0x149: return PageUp;
        case 0x14B: return Left;
        case 0x14D: return Right;
        case 0x14F: return End;
        case 0x150: return Down;
        case 0x151: return PageDown;
        case 0x152: return Insert;
        case 0x153: return Delete;
        case 0x15B: return LeftSuper;
        case 0x15C: return RightSuper;
        case 0x15D: return Menu;
        default: return -1;
        }
    }

    // Qt's key, for platforms without scan codes here (macOS, Android, iOS).
    [[nodiscard]] int FromQtKey(const QKeyEvent& event) noexcept
    {
        const int key = event.key();
        const bool keypad = event.modifiers().testFlag(Qt::KeypadModifier);
        if (keypad && key >= Qt::Key_0 && key <= Qt::Key_9)
        {
            return Kp0 + (key - Qt::Key_0);
        }
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
        {
            return A + (key - Qt::Key_A);
        }
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
        {
            return D0 + (key - Qt::Key_0);
        }
        if (key >= Qt::Key_F1 && key <= Qt::Key_F25)
        {
            return F1 + (key - Qt::Key_F1);
        }
        switch (key)
        {
        case Qt::Key_Space: return Space;
        case Qt::Key_Apostrophe: return Apostrophe;
        case Qt::Key_Comma: return Comma;
        case Qt::Key_Minus: return keypad ? KpSubtract : Minus;
        case Qt::Key_Period: return keypad ? KpDecimal : Period;
        case Qt::Key_Slash: return keypad ? KpDivide : Slash;
        case Qt::Key_Semicolon: return Semicolon;
        case Qt::Key_Equal: return keypad ? KpEqual : Equal;
        case Qt::Key_BracketLeft: return LeftBracket;
        case Qt::Key_Backslash: return Backslash;
        case Qt::Key_BracketRight: return RightBracket;
        case Qt::Key_QuoteLeft: return Grave;
        case Qt::Key_Asterisk: return KpMultiply;
        case Qt::Key_Plus: return KpAdd;
        case Qt::Key_Escape: return Escape;
        case Qt::Key_Return: return Enter;
        case Qt::Key_Enter: return keypad ? KpEnter : Enter;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: return Tab;
        case Qt::Key_Backspace: return Backspace;
        case Qt::Key_Insert: return Insert;
        case Qt::Key_Delete: return Delete;
        case Qt::Key_Right: return Right;
        case Qt::Key_Left: return Left;
        case Qt::Key_Down: return Down;
        case Qt::Key_Up: return Up;
        case Qt::Key_PageUp: return PageUp;
        case Qt::Key_PageDown: return PageDown;
        case Qt::Key_Home: return Home;
        case Qt::Key_End: return End;
        case Qt::Key_CapsLock: return CapsLock;
        case Qt::Key_ScrollLock: return ScrollLock;
        case Qt::Key_NumLock: return NumLock;
        case Qt::Key_Print: return PrintScreen;
        case Qt::Key_Pause: return Pause;
        case Qt::Key_Shift: return LeftShift;
        case Qt::Key_Control: return LeftControl;
        case Qt::Key_Alt: return LeftAlt;
        case Qt::Key_AltGr: return RightAlt;
        case Qt::Key_Meta:
        case Qt::Key_Super_L: return LeftSuper;
        case Qt::Key_Super_R: return RightSuper;
        case Qt::Key_Menu: return Menu;
        default: return -1;
        }
    }
}

namespace MphRead::Qt
{
    int GlfwKey(const QKeyEvent& event) noexcept
    {
        const unsigned scan = event.nativeScanCode();
        if (scan != 0)
        {
            static const QString platform = QGuiApplication::platformName();
            int key = -1;
#if defined(_WIN32)
            key = FromWindows(scan);
#elif defined(__linux__) && !defined(__ANDROID__)
            // xcb and wayland both hand over XKB keycodes: evdev + 8.
            if (platform == QLatin1String("xcb") || platform.startsWith(QLatin1String("wayland")))
            {
                key = scan >= 8 ? FromEvdev(scan - 8) : -1;
            }
#endif
            (void)platform;
            if (key >= 0)
            {
                return key;
            }
        }
        return FromQtKey(event);
    }
}
