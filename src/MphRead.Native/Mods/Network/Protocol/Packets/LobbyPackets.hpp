#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Protocol/Packets/ChatPackets.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    struct VotePacket
    {
        static constexpr std::int32_t MaxRoomBytes = MatchStatePacket::MaxNameBytes;
        static constexpr std::int32_t Size = 1 + MaxRoomBytes;

        static constexpr std::uint8_t KindPropose = 0;
        static constexpr std::uint8_t KindYes = 1;
        static constexpr std::uint8_t KindNo = 2;

        std::uint8_t Kind = 0;
        std::optional<std::string> RoomKey{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static VotePacket Read(std::span<const std::uint8_t> src);
    };

    struct VoteStatePacket
    {
        static constexpr std::int32_t MaxRoomBytes = MatchStatePacket::MaxNameBytes;
        static constexpr std::int32_t MaxNameBytes = ChatPacket::MaxNameBytes;
        static constexpr std::int32_t Size = 1 + MaxRoomBytes + MaxNameBytes + 1 + 1 + 1 + 1 + 2;

        static constexpr std::uint8_t StateIdle = 0;
        static constexpr std::uint8_t StateRunning = 1;
        static constexpr std::uint8_t StatePassed = 2;
        static constexpr std::uint8_t StateFailed = 3;

        std::uint8_t State = 0;
        std::optional<std::string> RoomKey{};
        std::optional<std::string> Proposer{};
        std::uint8_t Yes = 0;
        std::uint8_t No = 0;
        std::uint8_t Eligible = 0;
        std::uint8_t Needed = 0;
        std::uint16_t Seconds = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static VoteStatePacket Read(std::span<const std::uint8_t> src);
    };

    struct MapChoicesPacket
    {
        static constexpr std::int32_t MaxChoices = 8;
        static constexpr std::int32_t MaxRoomBytes = MatchStatePacket::MaxNameBytes;
        static constexpr std::int32_t Size = 4 + MaxChoices * (MaxRoomBytes + 1);

        std::uint8_t Open = 0;

        std::uint8_t Count = 0;
        std::shared_ptr<std::vector<std::optional<std::string>>> RoomKeys{};
        std::shared_ptr<std::vector<std::uint8_t>> Votes{};
        std::uint8_t Eligible = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static MapChoicesPacket Read(std::span<const std::uint8_t> src);
    };

    struct MapPickPacket
    {
        static constexpr std::int32_t MaxRoomBytes = MatchStatePacket::MaxNameBytes;
        static constexpr std::int32_t Size = MaxRoomBytes;

        std::optional<std::string> RoomKey{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static MapPickPacket Read(std::span<const std::uint8_t> src);
    };
}
