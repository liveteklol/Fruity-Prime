#pragma once

#include "Formats/Enums.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    // Owns the local player's identity fields shared with the session and lobby.
    class SessionPlayerIdentity final
    {
    public:
        [[nodiscard]] ::MphRead::Hunter SelectedHunter() const noexcept { return _hunter; }
        [[nodiscard]] std::int32_t Color() const noexcept { return _color; }
        [[nodiscard]] const std::string& Name() const noexcept { return _name; }
        [[nodiscard]] bool NeedsRefresh(std::span<const std::string> nicknames,
            std::int32_t slot) const noexcept
        {
            return slot >= 0 && static_cast<std::size_t>(slot) < nicknames.size()
                && nicknames[static_cast<std::size_t>(slot)] != _name;
        }

        void SetHunter(::MphRead::Hunter hunter) noexcept { _hunter = hunter; }
        void SetColor(std::int32_t color) noexcept { _color = color; }
        void SetName(std::string name) { _name = std::move(name); }

    private:
        ::MphRead::Hunter _hunter = ::MphRead::Hunter::Samus;
        std::int32_t _color = 0;
        std::string _name = "Player";
    };
}
