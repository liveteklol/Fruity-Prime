#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Codec/PacketText.hpp"
#include "Mods/Network/Protocol/Packets/DirectoryPackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    std::int32_t HostRequestPacket::Length() const noexcept
    {
        const std::int32_t count = std::min(
            Rotation.has_value() ? static_cast<std::int32_t>(Rotation->size()) : 0, MaxRotation);
        return Size + 1 + count * RotationEntrySize + 4;
    }
    HostRequestPacket HostRequestPacket::ZeroInitialized()
    {
        HostRequestPacket packet;
        packet.RoomKey.reset();
        packet.ServerName.reset();
        packet.AllowJoinInProgress = false;
        packet.RequireReady = false;
        return packet;
    }
    void HostRequestPacket::Write(std::span<std::uint8_t> dest) const
    {
        At(dest, 0) = Protocol;
        At(dest, 1) = MaxPlayers;
        At(dest, 2) = Mode;
        W16(Slice(dest, 3), TimeLimit);
        W16(Slice(dest, 5), PointGoal);
        NetText::Write(Slice(dest, 7, MaxRoomBytes), RoomKey);
        NetText::Write(Slice(dest, 7 + MaxRoomBytes, MaxNameBytes), ServerName);
        const std::int32_t count = std::min(
            Rotation.has_value() ? static_cast<std::int32_t>(Rotation->size()) : 0, MaxRotation);
        At(dest, Size) = static_cast<std::uint8_t>(count);
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(Size + 1 + i * RotationEntrySize);
            const auto& entry = (*Rotation)[static_cast<std::size_t>(i)];
            NetText::Write(Slice(dest, at, MaxRoomBytes), entry.first);
            At(dest, at + MaxRoomBytes) = static_cast<std::uint8_t>(entry.second);
        }
        const std::size_t tail = static_cast<std::size_t>(Size + 1 + count * RotationEntrySize);
        At(dest, tail) = static_cast<std::uint8_t>(Policy);
        At(dest, tail + 1) = AllowJoinInProgress ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        At(dest, tail + 2) = RequireReady ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        At(dest, tail + 3) = static_cast<std::uint8_t>(Format);
    }
    HostRequestPacket HostRequestPacket::Read(std::span<const std::uint8_t> src)
    {
        if (src.size() < static_cast<std::size_t>(Size + 5) || src[Size] > MaxRotation)
        {
            return ZeroInitialized();
        }
        const std::size_t tail = static_cast<std::size_t>(Size + 1 + src[Size] * RotationEntrySize);
        if (src.size() != tail + 4 || src[tail] > 1 || src[tail + 1] > 1
            || src[tail + 2] > 1 || src[tail + 3] > static_cast<std::uint8_t>(MatchFormat::TwoVsTwoVsTwoVsTwo))
        {
            return ZeroInitialized();
        }
        HostRequestPacket packet;
        packet.Protocol = At(src, 0);
        packet.MaxPlayers = At(src, 1);
        packet.Mode = At(src, 2);
        packet.TimeLimit = R16(Slice(src, 3));
        packet.PointGoal = R16(Slice(src, 5));
        packet.RoomKey = NetText::Read(Slice(src, 7, MaxRoomBytes));
        packet.ServerName = NetText::Read(Slice(src, 7 + MaxRoomBytes, MaxNameBytes));
        packet.Policy = static_cast<ServerSessionPolicy>(src[tail]);
        packet.AllowJoinInProgress = src[tail + 1] != 0;
        packet.RequireReady = src[tail + 2] != 0;
        packet.Format = static_cast<MatchFormat>(src[tail + 3]);
        packet.Rotation = ReadRotation(src);
        return packet;
    }
    std::optional<std::vector<std::pair<std::string, ::MphRead::GameMode>>> HostRequestPacket::ReadRotation(
        std::span<const std::uint8_t> src)
    {
        // Every length is checked rather than trusted: the count byte is the
        // asker's and a truncated datagram must not read past what arrived.
        if (src.size() <= static_cast<std::size_t>(Size))
        {
            return std::nullopt;
        }
        const std::int32_t count = std::min(static_cast<std::int32_t>(src[Size]), MaxRotation);
        if (count == 0)
        {
            return std::nullopt;
        }
        std::vector<std::pair<std::string, ::MphRead::GameMode>> maps;
        maps.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; i++)
        {
            const std::size_t at = static_cast<std::size_t>(Size + 1 + i * RotationEntrySize);
            if (at + RotationEntrySize > src.size())
            {
                break;
            }
            std::string room = NetText::Read(Slice(src, at, MaxRoomBytes));
            if (room.empty())
            {
                continue;
            }
            const std::uint8_t mode = src[at + MaxRoomBytes];
            maps.emplace_back(std::move(room), ::MphRead::IsDefinedGameMode(mode)
                ? static_cast<::MphRead::GameMode>(mode) : ::MphRead::GameMode::Battle);
        }
        if (maps.empty())
        {
            return std::nullopt;
        }
        return maps;
    }
    void HostReplyPacket::Write(std::span<std::uint8_t> dest) const
    {
        At(dest, 0) = Started ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        W16(Slice(dest, 1), Port);
        NetText::Write(Slice(dest, 3, MaxReasonBytes), Reason);
        (void)OwnerToken.TryWriteBytes(Slice(dest, 3 + MaxReasonBytes, 16));
    }
    HostReplyPacket HostReplyPacket::Read(std::span<const std::uint8_t> src)
    {
        HostReplyPacket packet;
        packet.Started = At(src, 0) != 0;
        packet.OwnerToken = ::MphRead::NativeRuntime::Guid(Slice(src, 3 + MaxReasonBytes, 16));
        packet.Port = R16(Slice(src, 1));
        packet.Reason = NetText::Read(Slice(src, 3, MaxReasonBytes));
        return packet;
    }
    void NetText::Write(std::span<std::uint8_t> dest, const std::optional<std::string> &value)
    {
        PacketText::Write(dest, value);
    }
    std::string NetText::Read(std::span<const std::uint8_t> src)
    {
        return PacketText::Read(src);
    }
    void MasterEntryPacket::Write(std::span<std::uint8_t> dest) const
    {
        WB32(dest, Address);
        W16(Slice(dest, 4), Port);
        At(dest, 6) = Players;
        At(dest, 7) = MaxPlayers;
        At(dest, 8) = Mode;
        At(dest, 9) = Protocol;
        NetText::Write(Slice(dest, 10, MaxNameBytes), ServerName);
        NetText::Write(Slice(dest, 10 + MaxNameBytes, MaxRoomBytes), RoomKey);
    }
    MasterEntryPacket MasterEntryPacket::Read(std::span<const std::uint8_t> src)
    {
        MasterEntryPacket packet;
        packet.Address = RB32(src);
        packet.Port = R16(Slice(src, 4));
        packet.Players = At(src, 6);
        packet.MaxPlayers = At(src, 7);
        packet.Mode = At(src, 8);
        packet.Protocol = At(src, 9);
        packet.ServerName = NetText::Read(Slice(src, 10, MaxNameBytes));
        packet.RoomKey = NetText::Read(Slice(src, 10 + MaxNameBytes, MaxRoomBytes));
        return packet;
    }
    void MasterHeartbeatPacket::Write(std::span<std::uint8_t> dest) const
    {
        At(dest, 0) = Protocol;
        W16(Slice(dest, 1), Port);
        At(dest, 3) = Players;
        At(dest, 4) = MaxPlayers;
        At(dest, 5) = Mode;
        NetText::Write(Slice(dest, 6, MasterEntryPacket::MaxNameBytes), ServerName);
        NetText::Write(Slice(dest, 6 + MasterEntryPacket::MaxNameBytes, MasterEntryPacket::MaxRoomBytes), RoomKey);
    }
    MasterHeartbeatPacket MasterHeartbeatPacket::Read(std::span<const std::uint8_t> src)
    {
        MasterHeartbeatPacket packet;
        packet.Protocol = At(src, 0);
        packet.Port = R16(Slice(src, 1));
        packet.Players = At(src, 3);
        packet.MaxPlayers = At(src, 4);
        packet.Mode = At(src, 5);
        packet.ServerName = NetText::Read(Slice(src, 6, MasterEntryPacket::MaxNameBytes));
        packet.RoomKey = NetText::Read(Slice(src, 6 + MasterEntryPacket::MaxNameBytes, MasterEntryPacket::MaxRoomBytes));
        return packet;
    }
}
