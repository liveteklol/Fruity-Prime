#pragma once

#include "Mods/Network/Server/Core/ServerPeerIdentity.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    // Owns the validated player identity retained for a server connection.
    class ServerPeerProfile final
    {
    public:
        struct Update final
        {
            bool Changed = false;
            bool FirstName = false;
            bool AppearanceChanged = false;
        };

        [[nodiscard]] const std::string& Name() const noexcept { return _name; }
        [[nodiscard]] std::uint8_t Hunter() const noexcept { return _hunter; }
        [[nodiscard]] std::uint8_t Color() const noexcept { return _color; }

        [[nodiscard]] Update Apply(ServerPlayerIdentity identity)
        {
            if (_name == identity.Name && _hunter == identity.Hunter && _color == identity.Color)
            {
                return {};
            }

            const Update update{
                true,
                _name.empty(),
                _hunter != identity.Hunter || _color != identity.Color
            };
            _name = std::move(identity.Name);
            _hunter = identity.Hunter;
            _color = identity.Color;
            return update;
        }

    private:
        std::string _name{};
        std::uint8_t _hunter = 0;
        std::uint8_t _color = 0;
    };
}
