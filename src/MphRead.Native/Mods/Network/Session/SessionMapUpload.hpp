#pragma once

#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/NetRole.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    // The client's half of handing a .pk3 to a server that lacks it: when the
    // server asks for a level this player proposed, find the pack in the map
    // folder that holds it and send what is asked for, nothing more (see
    // MapTransferPackets.hpp). The pack is read once and kept for the
    // transfer; a level not found is remembered so a repeated request costs
    // nothing.
    class SessionMapUpload final
    {
    public:
        static constexpr std::uint32_t MaxBytes = 48u * 1024u * 1024u;

        static void Answer(SessionConnection& connection, NetRole role,
            std::span<const std::uint8_t> payload);

        SessionMapUpload() = delete;

    private:
        struct Held
        {
            std::string RoomKey{};
            bool Found = false;
            std::vector<std::uint8_t> Data{};
            std::array<std::uint8_t, 32> Sha256{};
        };

        [[nodiscard]] static Held Load(const std::string& roomKey);
    };
}
