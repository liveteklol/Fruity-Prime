#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Codec/PacketText.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Protocol/Packets/ChatPackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    bool MatchStatePacket::Ending() const noexcept { return (Flags & FlagEnding) != 0; }
    bool MatchStatePacket::FriendlyFire() const noexcept { return (Flags & FlagFriendlyFire) != 0; }
    bool MatchStatePacket::ShadowFreeze() const noexcept { return (Flags & FlagNoShadowFreeze) == 0; }
    std::int32_t MatchStatePacket::DamageLevel() const noexcept
    {
        const std::int32_t stated = (Flags & FlagDamageMask) >> FlagDamageShift;
        return stated == 0 ? -1 : stated - 1;
    }
    bool MatchStatePacket::StatesRules() const noexcept { return (Flags & FlagDamageMask) != 0; }
    bool MatchStatePacket::AffinityWeapons() const noexcept { return (Flags & FlagAffinityWeapons) != 0; }
    bool MatchStatePacket::Instagib() const noexcept { return (Flags & FlagInstagib) != 0; }
    std::uint8_t MatchStatePacket::RuleFlags(std::int32_t damageLevel, bool affinityWeapons) noexcept
    {
        if (damageLevel < 0 || damageLevel > 2)
        {
            return 0;
        }
        std::uint8_t flags = static_cast<std::uint8_t>((damageLevel + 1) << FlagDamageShift);
        if (affinityWeapons)
        {
            flags = static_cast<std::uint8_t>(flags | FlagAffinityWeapons);
        }
        return flags;
    }
    void MatchStatePacket::Write(std::span<std::uint8_t> dest) const
    {
        W64(Slice(dest, 95), AuthorityEpoch);
        At(dest, 0) = Mode;
        WF(Slice(dest, 1), TimeRemaining);
        WF(Slice(dest, 5), TimeElapsed);
        At(dest, 9) = PlayerCount;
        At(dest, 10) = Flags;
        W16(Slice(dest, 11), PointGoal);
        W16(Slice(dest, 13), MatchId);
        WriteName(Slice(dest, 15), RoomKey);
        WriteName(Slice(dest, 15 + MaxNameBytes), NextRoomKey);
    }
    MatchStatePacket MatchStatePacket::Read(std::span<const std::uint8_t> src)
    {
        MatchStatePacket packet;
        packet.AuthorityEpoch = R64(Slice(src, 95));
        packet.Mode = At(src, 0);
        packet.TimeRemaining = RF(Slice(src, 1));
        packet.TimeElapsed = RF(Slice(src, 5));
        packet.PlayerCount = At(src, 9);
        packet.Flags = At(src, 10);
        packet.PointGoal = R16(Slice(src, 11));
        packet.MatchId = R16(Slice(src, 13));
        packet.RoomKey = ReadName(Slice(src, 15));
        packet.NextRoomKey = ReadName(Slice(src, 15 + MaxNameBytes));
        return packet;
    }
    void MatchStatePacket::WriteName(std::span<std::uint8_t> dest, const std::optional<std::string> &value)
    {
        CP(dest, MaxNameBytes);
        if (!value.has_value() || value->empty())
        {
            return;
        }
        const std::vector<std::uint16_t> units = U16(*value);
        const std::size_t count = std::min<std::size_t>(units.size(), MaxNameBytes);
        for (std::size_t index = 0; index < count; index++)
        {
            dest[index] = static_cast<std::uint8_t>(units[index]);
        }
    }
    std::string MatchStatePacket::ReadName(std::span<const std::uint8_t> src)
    {
        std::size_t length = 0;
        while (length < static_cast<std::size_t>(MaxNameBytes) && At(src, length) != 0)
        {
            length++;
        }
        return length == 0 ? std::string{} : DA(Slice(src, 0, length));
    }
    void ServerStatusPacket::Write(std::span<std::uint8_t> dest) const
    {
        Match.Write(dest);
        At(dest, MatchStatePacket::Size) = MaxPlayers;
        At(dest, MatchStatePacket::Size + 1) = Protocol;
        PacketText::Write(Slice(dest, MatchStatePacket::Size + 2, MaxNameBytes), ServerName);
        if (dest.size() >= static_cast<std::size_t>(SizeWithFlags))
        {
            dest[Size] = Flags;
            dest[Size + 1] = static_cast<std::uint8_t>(Phase);
            dest[Size + 2] = static_cast<std::uint8_t>(Format);
            dest[Size + 3] = LobbyEnabled ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
            dest[Size + 4] = AllowJoinInProgress ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        }
    }
    ServerStatusPacket ServerStatusPacket::Read(std::span<const std::uint8_t> src)
    {
        ServerStatusPacket packet;
        packet.Match = MatchStatePacket::Read(src);
        packet.MaxPlayers = At(src, MatchStatePacket::Size);
        packet.Protocol = At(src, MatchStatePacket::Size + 1);
        packet.ServerName = src.size() >= static_cast<std::size_t>(Size) ? std::optional<std::string>(PacketText::Read(Slice(src, MatchStatePacket::Size + 2, MaxNameBytes))) : std::optional<std::string>(std::string{});
        const bool withFlags = src.size() >= static_cast<std::size_t>(SizeWithFlags);
        packet.Flags = src.size() > static_cast<std::size_t>(Size) ? src[Size] : static_cast<std::uint8_t>(0);
        packet.Phase = withFlags ? static_cast<SessionPhase>(src[Size + 1]) : SessionPhase::InMatch;
        packet.Format = withFlags ? static_cast<MatchFormat>(src[Size + 2]) : MatchFormat::Auto;
        packet.LobbyEnabled = withFlags && src[Size + 3] != 0;
        packet.AllowJoinInProgress = !withFlags || src[Size + 4] != 0;
        return packet;
    }
    RosterPacket RosterPacket::Create()
    {
        RosterPacket packet;
        packet.Count = 0;
        packet.Generations = std::make_shared<std::vector<std::uint16_t>>(MaxSlots);
        packet.Slots = std::make_shared<std::vector<std::uint8_t>>(MaxSlots);
        packet.Teams = std::make_shared<std::vector<std::int8_t>>(MaxSlots);
        packet.LobbyReady = std::make_shared<std::vector<bool>>(MaxSlots);
        packet.Hunters = std::make_shared<std::vector<std::uint8_t>>(MaxSlots);
        packet.Colors = std::make_shared<std::vector<std::uint8_t>>(MaxSlots);
        packet.Pings = std::make_shared<std::vector<std::uint16_t>>(MaxSlots);
        packet.Names = std::make_shared<std::vector<std::optional<std::string>>>(MaxSlots);
        return packet;
    }
    void RosterPacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        dest[0] = Count;
        W16(Slice(dest, 1), MatchId);
        W64(Slice(dest, 3), AuthorityEpoch);
        W32(Slice(dest, 11), Revision);
        W16(Slice(dest, 15), SessionRevision);
        std::size_t offset = HeaderSize;
        for (std::size_t index = 0; index < static_cast<std::size_t>(Count) && index < static_cast<std::size_t>(MaxSlots); index++)
        {
            dest[offset] = AA(Slots, index);
            dest[offset + 1] = AA(Hunters, index);
            dest[offset + 2] = AA(Colors, index);
            W16(Slice(dest, offset + 3), AA(Pings, index));
            WriteName(Slice(dest, offset + 5, MaxNameBytes), AA(Names, index));
            dest[offset + 7 + MaxNameBytes] = static_cast<std::uint8_t>(AA(Teams, index));
            dest[offset + 8 + MaxNameBytes] = ReadyAt(LobbyReady, index) ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
            W16(Slice(dest, offset + 21), AA(Generations, index));
            offset += EntrySize;
        }
    }
    bool RosterPacket::TryRead(std::span<const std::uint8_t> src, RosterPacket& roster)
    {
        roster = RosterPacket{};
        if (src.size() != static_cast<std::size_t>(Size) || src[0] > MaxSlots)
        {
            return false;
        }
        std::int32_t seen = 0;
        for (std::size_t i = 0; i < src[0]; i++)
        {
            const std::size_t offset = HeaderSize + i * EntrySize;
            const std::int32_t slot = src[offset];
            const std::int32_t team = static_cast<std::int8_t>(src[offset + 7 + MaxNameBytes]);
            if (slot >= MaxSlots || (seen & (1 << slot)) != 0 || src[offset + 1] >= 7
                || src[offset + 2] > 3 || team < -1 || team > 3 || src[offset + 8 + MaxNameBytes] > 1)
            {
                return false;
            }
            seen |= 1 << slot;
        }
        roster = Read(src);
        return true;
    }
    RosterPacket RosterPacket::Read(std::span<const std::uint8_t> src)
    {
        RosterPacket packet = Create();
        packet.Count = std::min<std::uint8_t>(At(src, 0), static_cast<std::uint8_t>(MaxSlots));
        packet.MatchId = R16(Slice(src, 1));
        packet.AuthorityEpoch = R64(Slice(src, 3));
        packet.Revision = R32(Slice(src, 11));
        packet.SessionRevision = R16(Slice(src, 15));
        std::size_t offset = HeaderSize;
        for (std::size_t index = 0; index < packet.Count; index++)
        {
            AAM(packet.Slots, index) = At(src, offset);
            AAM(packet.Hunters, index) = At(src, offset + 1);
            AAM(packet.Colors, index) = At(src, offset + 2);
            AAM(packet.Pings, index) = R16(Slice(src, offset + 3));
            AAM(packet.Names, index) = ReadName(Slice(src, offset + 5, MaxNameBytes));
            AAM(packet.Teams, index) = static_cast<std::int8_t>(At(src, offset + 7 + MaxNameBytes));
            ReadyAt(packet.LobbyReady, index);
            (*packet.LobbyReady)[index] = At(src, offset + 8 + MaxNameBytes) != 0;
            AAM(packet.Generations, index) = R16(Slice(src, offset + 21));
            offset += EntrySize;
        }
        return packet;
    }
    void RosterPacket::WriteName(std::span<std::uint8_t> dest, const std::optional<std::string> &value)
    {
        Clear(dest);
        if (!value.has_value() || value->empty())
        {
            return;
        }
        const std::vector<std::uint16_t> units = U16(*value);
        const std::size_t count = std::min<std::size_t>(units.size(), MaxNameBytes);
        for (std::size_t index = 0; index < count; index++)
        {
            const std::uint16_t unit = units[index];
            dest[index] = unit < 32U || unit > 126U ? static_cast<std::uint8_t>('?') : static_cast<std::uint8_t>(unit);
        }
    }
    std::string RosterPacket::ReadName(std::span<const std::uint8_t> src)
    {
        std::size_t length = 0;
        while (length < src.size() && src[length] != 0)
        {
            length++;
        }
        return length == 0 ? std::string{} : DA(Slice(src, 0, length));
    }
    void ChatPacket::Write(std::span<std::uint8_t> dest) const
    {
        CP(dest, Size);
        dest[0] = Slot;
        dest[1] = Kind;
        WriteAscii(Slice(dest, 2, MaxNameBytes), Name);
        WriteAscii(Slice(dest, 2 + MaxNameBytes, MaxTextBytes), Text);
    }
    ChatPacket ChatPacket::Read(std::span<const std::uint8_t> src)
    {
        ChatPacket packet;
        packet.Slot = At(src, 0);
        packet.Kind = At(src, 1);
        packet.Name = ReadAscii(Slice(src, 2, MaxNameBytes));
        packet.Text = ReadAscii(Slice(src, 2 + MaxNameBytes, MaxTextBytes));
        return packet;
    }
    void ChatPacket::WriteAscii(std::span<std::uint8_t> dest, const std::optional<std::string> &value)
    {
        Clear(dest);
        if (!value.has_value() || value->empty())
        {
            return;
        }
        const std::vector<std::uint16_t> units = U16(*value);
        const std::size_t count = std::min(units.size(), dest.size());
        for (std::size_t index = 0; index < count; index++)
        {
            const std::uint16_t unit = units[index];
            dest[index] = unit < 32U || unit > 126U ? static_cast<std::uint8_t>('?') : static_cast<std::uint8_t>(unit);
        }
    }
    std::string ChatPacket::ReadAscii(std::span<const std::uint8_t> src)
    {
        std::size_t length = 0;
        while (length < src.size() && src[length] != 0)
        {
            length++;
        }
        if (length == 0)
        {
            return {};
        }
        std::string result;
        result.reserve(length);
        for (std::size_t index = 0; index < length; index++)
        {
            const std::uint8_t value = src[index];
            result.push_back(value < 32U || value > 126U ? '?' : static_cast<char>(value));
        }
        return result;
    }
}
