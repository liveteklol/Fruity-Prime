#pragma once

// The front screen as the DS game drew it: a debug switch on the launcher
// puts the game's own title and menus (MenuData) in place of Fruity Prime's,
// on one screen (OneScreen), with every page the launcher has rebuilt from
// the ROM's pieces (Compose) and answered by the launcher (Host). Drawn on
// the CPU into one picture of the window's shape, so it is the same on
// every renderer; the Qt shell shows it.

#include "Host.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::ClassicMenu
{
    class Facade final
    {
    public:
        Facade() = delete;

        [[nodiscard]] static bool Active() noexcept;
        // Starts the menus at the title (reading the game's files), or stops
        // them; false (and inactive) when they cannot be read. Only the
        // owner that started them stops them: a rebuilt UI's old item going
        // away after the new one started must not switch the new one off.
        static bool SetActive(bool active, const void* owner);
        [[nodiscard]] static bool Owns(const void* owner) noexcept;
        [[nodiscard]] static const std::string& LastError() noexcept;

        static constexpr int ScreenWidth = 256;
        static constexpr int ScreenHeight = 192;

        // What the launcher answers with. Not owned; null to detach.
        static void SetHost(Host* host);

        // Advance by real time, then draw the one screen into `pixels`
        // (RGBA8, row-major, top row first) at `width` x `height`. False when
        // inactive.
        static bool Render(double seconds, int width, int height, std::vector<std::uint32_t>& pixels);
        // Advance `ticks` menu ticks (two DS frames each), then draw the two
        // DS screens as the DS shows them: 256 x 384, the top screen above,
        // backdrop and all. The frames compared against the game itself
        // (-classicframes). False when inactive.
        static bool RenderDs(int ticks, std::vector<std::uint32_t>& pixels);
        // The size the last picture was drawn at (the shell scales it to the item).
        static void DrawnSize(int& width, int& height);
        // The pointer, in the last picture's pixels: moved, or pressed.
        static void Pointer(float x, float y, bool press);
        static void Press(std::uint16_t keys);
        static void Navigate(int dx, int dy);
        // A real keyboard, while the DS keyboard waits for words.
        static bool Type(const std::string& text, bool backspace, bool enter);
        // A key binding waits for its key: keys belong to the settings.
        [[nodiscard]] static bool Listening();
        // The launcher moved on: "room" (a lobby opened), "pause", "end", "front".
        static void Show(const std::string& what);
        // Drawn over a running match: the picture has no backdrop.
        [[nodiscard]] static bool OverGame();
        // A touch on the DS touch screen, in DS pixels (y down): the check script.
        static void Touch(float x, float y);
        // Straight to a page (the check script); "servers", "settings", ... or a number.
        static void Visit(const std::string& page);
        // The page, every item's state and what it drew in the last picture,
        // as text (-classicframes' describe step).
        [[nodiscard]] static std::string Describe();
    };
}
