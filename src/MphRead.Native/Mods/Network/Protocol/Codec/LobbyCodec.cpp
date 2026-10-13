#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/LobbyPackets.hpp"
#include "Mods/Network/Protocol/Packets/ChatPackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    void VotePacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        dest[0] = Kind;
        ChatPacket::WriteAscii(Slice(dest, 1, MaxRoomBytes), RoomKey);
    }
    VotePacket VotePacket::Read(std::span<const std::uint8_t> src)
    {
        VotePacket packet;
        packet.Kind = At(src, 0);
        packet.RoomKey = ChatPacket::ReadAscii(Slice(src, 1, MaxRoomBytes));
        return packet;
    }
    void VoteStatePacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        dest[0] = State;
        ChatPacket::WriteAscii(Slice(dest, 1, MaxRoomBytes), RoomKey);
        ChatPacket::WriteAscii(Slice(dest, 1 + MaxRoomBytes, MaxNameBytes), Proposer);
        constexpr std::size_t at = 1 + MaxRoomBytes + MaxNameBytes;
        dest[at] = Yes;
        dest[at + 1] = No;
        dest[at + 2] = Eligible;
        dest[at + 3] = Needed;
        W16(Slice(dest, at + 4, 2), Seconds);
    }
    VoteStatePacket VoteStatePacket::Read(std::span<const std::uint8_t> src)
    {
        constexpr std::size_t at = 1 + MaxRoomBytes + MaxNameBytes;
        VoteStatePacket packet;
        packet.State = At(src, 0);
        packet.RoomKey = ChatPacket::ReadAscii(Slice(src, 1, MaxRoomBytes));
        packet.Proposer = ChatPacket::ReadAscii(Slice(src, 1 + MaxRoomBytes, MaxNameBytes));
        packet.Yes = At(src, at);
        packet.No = At(src, at + 1);
        packet.Eligible = At(src, at + 2);
        packet.Needed = At(src, at + 3);
        packet.Seconds = R16(Slice(src, at + 4, 2));
        return packet;
    }
    void MapChoicesPacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        const std::int32_t count = std::clamp(static_cast<std::int32_t>(Count), 0, MaxChoices);
        dest[0] = static_cast<std::uint8_t>(count);
        dest[1] = Eligible;
        dest[2] = Open;
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(4 + i * (MaxRoomBytes + 1));
            const std::size_t index = static_cast<std::size_t>(i);
            ChatPacket::WriteAscii(Slice(dest, at, MaxRoomBytes),
                RoomKeys != nullptr && index < RoomKeys->size() ? (*RoomKeys)[index] : std::optional<std::string>(std::string()));
            dest[at + MaxRoomBytes] = Votes != nullptr && index < Votes->size() ? (*Votes)[index] : static_cast<std::uint8_t>(0);
        }
    }
    MapChoicesPacket MapChoicesPacket::Read(std::span<const std::uint8_t> src)
    {
        const std::int32_t count = std::clamp(static_cast<std::int32_t>(At(src, 0)), 0, MaxChoices);
        auto keys = std::make_shared<std::vector<std::optional<std::string>>>(static_cast<std::size_t>(count));
        auto votes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(4 + i * (MaxRoomBytes + 1));
            (*keys)[static_cast<std::size_t>(i)] = ChatPacket::ReadAscii(Slice(src, at, MaxRoomBytes));
            (*votes)[static_cast<std::size_t>(i)] = At(src, at + MaxRoomBytes);
        }
        MapChoicesPacket packet;
        packet.Count = static_cast<std::uint8_t>(count);
        packet.RoomKeys = std::move(keys);
        packet.Votes = std::move(votes);
        packet.Eligible = At(src, 1);
        packet.Open = At(src, 2);
        return packet;
    }
    void MapPickPacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        ChatPacket::WriteAscii(Slice(dest, 0, MaxRoomBytes), RoomKey);
    }
    MapPickPacket MapPickPacket::Read(std::span<const std::uint8_t> src)
    {
        MapPickPacket packet;
        packet.RoomKey = ChatPacket::ReadAscii(Slice(src, 0, MaxRoomBytes));
        return packet;
    }
}
