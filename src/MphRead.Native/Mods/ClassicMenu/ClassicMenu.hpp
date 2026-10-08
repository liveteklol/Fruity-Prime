#pragma once

// The front screen as the DS game drew it: a debug switch on the launcher
// puts the game's own title and menus (MenuData) in place of Fruity Prime's.
// Each DS screen is drawn at its own 256x192, as the game draws it, on the
// CPU (the front screen has no game scene, and it is then the same on every
// renderer); the Qt shell shows the two pictures. MULTIPLAYER leaves for
// Fruity Prime's own multiplayer screens.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::ClassicMenu
{
    enum class Request
    {
        Multiplayer,
        Adventure
    };

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

        // Advance by real time, then draw both DS screens at their own size
        // into `top` and `bottom` (RGBA8, row-major, top row first). False
        // when inactive.
        static bool RenderScreens(double seconds, std::vector<std::uint32_t>& top, std::vector<std::uint32_t>& bottom);

        // A touch on the touch screen, in DS pixels (y down).
        static void Touch(float x, float y);
        static void Press(std::uint16_t keys);
        static void Navigate(int dx, int dy);
        [[nodiscard]] static std::optional<Request> TakeRequest();
    };
}
