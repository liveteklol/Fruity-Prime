#include "NetProtocol.hpp"

#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    [[noreturn]] void R() { throw std::out_of_range("Index was outside the bounds of the array."); }
    template <typename T>
    T &At(std::span<T> span, std::size_t index)
    {
        if (index >= span.size())
        {
            R();
        }
        return span[index];
    }
    template <typename T>
    const T &At(std::span<const T> span, std::size_t index)
    {
        if (index >= span.size())
        {
            R();
        }
        return span[index];
    }
    template <typename T>
    std::span<T> Slice(std::span<T> span, std::size_t offset)
    {
        if (offset > span.size())
        {
            R();
        }
        return span.subspan(offset);
    }
    template <typename T>
    std::span<const T> Slice(std::span<const T> span, std::size_t offset)
    {
        if (offset > span.size())
        {
            R();
        }
        return span.subspan(offset);
    }
    template <typename T>
    std::span<T> Slice(std::span<T> span, std::size_t offset, std::size_t count)
    {
        if (offset > span.size() || count > span.size() - offset)
        {
            R();
        }
        return span.subspan(offset, count);
    }
    template <typename T>
    std::span<const T> Slice(std::span<const T> span, std::size_t offset, std::size_t count)
    {
        if (offset > span.size() || count > span.size() - offset)
        {
            R();
        }
        return span.subspan(offset, count);
    }
    template <typename T>
    void L(std::span<T> span, std::size_t count)
    {
        if (span.size() < count)
        {
            R();
        }
    }
    template <typename T>
    void L(std::span<const T> span, std::size_t count)
    {
        if (span.size() < count)
        {
            R();
        }
    }
    void Clear(std::span<std::uint8_t> span) { std::fill(span.begin(), span.end(), static_cast<std::uint8_t>(0)); }
    void CP(std::span<std::uint8_t> span, std::size_t count) { Clear(Slice(span, 0, count)); }
    void W16(std::span<std::uint8_t> destination, std::uint16_t value)
    {
        L(destination, 2);
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
    }
    std::uint16_t R16(std::span<const std::uint8_t> source)
    {
        L(source, 2);
        const std::uint16_t b0 = source[0];
        const std::uint16_t b1 = source[1];
        return static_cast<std::uint16_t>(b0 | static_cast<std::uint16_t>(b1 << 8));
    }
    void WI16(std::span<std::uint8_t> destination, std::int16_t value) { W16(destination, static_cast<std::uint16_t>(value)); }
    std::int16_t RI16(std::span<const std::uint8_t> source) { return static_cast<std::int16_t>(R16(source)); }
    void W32(std::span<std::uint8_t> destination, std::uint32_t value)
    {
        L(destination, 4);
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
        destination[2] = static_cast<std::uint8_t>(value >> 16);
        destination[3] = static_cast<std::uint8_t>(value >> 24);
    }
    std::uint32_t R32(std::span<const std::uint8_t> source)
    {
        L(source, 4);
        return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8) | (static_cast<std::uint32_t>(source[2]) << 16) | (static_cast<std::uint32_t>(source[3]) << 24);
    }
    void WB32(std::span<std::uint8_t> destination, std::uint32_t value)
    {
        L(destination, 4);
        destination[0] = static_cast<std::uint8_t>(value >> 24);
        destination[1] = static_cast<std::uint8_t>(value >> 16);
        destination[2] = static_cast<std::uint8_t>(value >> 8);
        destination[3] = static_cast<std::uint8_t>(value);
    }
    std::uint32_t RB32(std::span<const std::uint8_t> source)
    {
        L(source, 4);
        return (static_cast<std::uint32_t>(source[0]) << 24) | (static_cast<std::uint32_t>(source[1]) << 16) | (static_cast<std::uint32_t>(source[2]) << 8) | static_cast<std::uint32_t>(source[3]);
    }
    void W64(std::span<std::uint8_t> destination, std::uint64_t value)
    {
        L(destination, 8);
        for (std::size_t i = 0; i < 8; ++i)
        {
            destination[i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }
    std::uint64_t R64(std::span<const std::uint8_t> source)
    {
        L(source, 8);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(source[i]) << (8 * i);
        }
        return value;
    }
    void WF(std::span<std::uint8_t> destination, float value) { W32(destination, std::bit_cast<std::uint32_t>(value)); }
    float RF(std::span<const std::uint8_t> source) { return std::bit_cast<float>(R32(source)); }
    std::vector<std::uint16_t> U16(std::string_view utf8)
    {
        std::vector<std::uint16_t> result;
        result.reserve(utf8.size());
        std::size_t index = 0;
        while (index < utf8.size())
        {
            const auto first = static_cast<unsigned char>(utf8[index]);
            std::uint32_t codePoint = 0xFFFDU;
            std::size_t consumed = 1;
            if (first < 0x80U)
            {
                codePoint = first;
            }
            else if (first >= 0xC2U && first <= 0xDFU && index + 1 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                if ((b1 & 0xC0U) == 0x80U)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x1FU) << 6) | static_cast<std::uint32_t>(b1 & 0x3FU);
                    consumed = 2;
                }
            }
            else if (first >= 0xE0U && first <= 0xEFU && index + 2 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                const auto b2 = static_cast<unsigned char>(utf8[index + 2]);
                const bool continuation = (b1 & 0xC0U) == 0x80U && (b2 & 0xC0U) == 0x80U;
                const bool notOverlong = first != 0xE0U || b1 >= 0xA0U;
                const bool notSurrogate = first != 0xEDU || b1 <= 0x9FU;
                if (continuation && notOverlong && notSurrogate)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x0FU) << 12) | (static_cast<std::uint32_t>(b1 & 0x3FU) << 6) | static_cast<std::uint32_t>(b2 & 0x3FU);
                    consumed = 3;
                }
            }
            else if (first >= 0xF0U && first <= 0xF4U && index + 3 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                const auto b2 = static_cast<unsigned char>(utf8[index + 2]);
                const auto b3 = static_cast<unsigned char>(utf8[index + 3]);
                const bool continuation = (b1 & 0xC0U) == 0x80U && (b2 & 0xC0U) == 0x80U && (b3 & 0xC0U) == 0x80U;
                const bool notOverlong = first != 0xF0U || b1 >= 0x90U;
                const bool inRange = first != 0xF4U || b1 <= 0x8FU;
                if (continuation && notOverlong && inRange)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x07U) << 18) | (static_cast<std::uint32_t>(b1 & 0x3FU) << 12) | (static_cast<std::uint32_t>(b2 & 0x3FU) << 6) | static_cast<std::uint32_t>(b3 & 0x3FU);
                    consumed = 4;
                }
            }
            if (codePoint <= 0xFFFFU)
            {
                result.push_back(static_cast<std::uint16_t>(codePoint));
            }
            else
            {
                const std::uint32_t scalar = codePoint - 0x10000U;
                result.push_back(static_cast<std::uint16_t>(0xD800U + (scalar >> 10)));
                result.push_back(static_cast<std::uint16_t>(0xDC00U + (scalar & 0x3FFU)));
            }
            index += consumed;
        }
        return result;
    }
    std::string DA(std::span<const std::uint8_t> source)
    {
        std::string result;
        result.reserve(source.size());
        for (const std::uint8_t value : source)
        {
            result.push_back(value <= 0x7FU ? static_cast<char>(value) : '?');
        }
        return result;
    }
    bool ReadyAt(const std::shared_ptr<std::vector<bool>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
    template <typename T>
    const T &AA(const std::shared_ptr<std::vector<T>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
    template <typename T>
    T &AAM(const std::shared_ptr<std::vector<T>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
}
namespace MphRead::Mods::Network
{
    void RefusedPacket::Write(std::span<std::uint8_t> dest) const
    {
        At(dest, 0) = Reason;
        At(dest, 1) = Players;
        At(dest, 2) = MaxPlayers;
    }
    RefusedPacket RefusedPacket::Read(std::span<const std::uint8_t> src)
    {
        RefusedPacket packet;
        packet.Reason = At(src, 0);
        packet.Players = src.size() > 1 ? src[1] : static_cast<std::uint8_t>(0);
        packet.MaxPlayers = src.size() > 2 ? src[2] : static_cast<std::uint8_t>(0);
        return packet;
    }
    std::string RefusedPacket::Describe(const std::optional<std::string> &where) const
    {
        const std::string prefix = where.value_or("");
        if (Reason == ReasonKicked)
        {
            return "You were removed by the lobby owner.";
        }
        if (Reason == ReasonInMatch)
        {
            return "This server does not allow joining a match in progress.";
        }
        if (Reason == ReasonFull)
        {
            return prefix + " is full (" + std::to_string(static_cast<unsigned int>(Players)) + "/" + std::to_string(static_cast<unsigned int>(MaxPlayers)) + " players). Try again when somebody leaves.";
        }
        if (Reason == ReasonProtocol)
        {
            return prefix + " is running a different version of the game. " + "One of you needs updating.";
        }
        return prefix + " would not admit this client.";
    }
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
    std::string NetText::Read(std::span<const std::uint8_t> src)
    {
        std::size_t length = 0;
        while (length < src.size() && src[length] != 0)
        {
            length++;
        }
        return length == 0 ? std::string{} : DA(Slice(src, 0, length));
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
        NetText::Write(Slice(dest, MatchStatePacket::Size + 2, MaxNameBytes), ServerName);
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
        packet.ServerName = src.size() >= static_cast<std::size_t>(Size) ? std::optional<std::string>(NetText::Read(Slice(src, MatchStatePacket::Size + 2, MaxNameBytes))) : std::optional<std::string>(std::string{});
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
    void IntentPacket::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 74), MatchId);
        W64(Slice(dest, 76), AuthorityEpoch);
        W16(Slice(dest, 84), SlotGeneration);
        W16(Slice(dest, 86), LifeId);
        W32(Slice(dest, 0), Frame);
        W32(Slice(dest, 4), static_cast<std::uint32_t>(Buttons));
        WF(Slice(dest, 8), Aim.X);
        WF(Slice(dest, 12), Aim.Y);
        WF(Slice(dest, 16), Aim.Z);
        At(dest, 20) = WeaponSelect;
        for (std::size_t index = 0; index < static_cast<std::size_t>(PressHistory); index++)
        {
            std::uint32_t value = 0;
            if (Presses != nullptr && index < Presses->size())
            {
                value = (*Presses)[index];
            }
            W32(Slice(dest, 21 + index * 4), value);
        }
        constexpr std::size_t at = 21 + PressHistory * 4;
        WF(Slice(dest, at), Position.X);
        WF(Slice(dest, at + 4), Position.Y);
        WF(Slice(dest, at + 8), Position.Z);
        W16(Slice(dest, at + 12), AmmoUa);
        W16(Slice(dest, at + 14), AmmoMissiles);
        W32(Slice(dest, at + 16), AckFrame);
        At(dest, at + 20) = AckSubFrame;
        if (dest.size() >= static_cast<std::size_t>(Size + StateSize))
        {
            dest[Size] = ChargeLevel;
            dest[Size + 1] = BoostDamage;
            dest[Size + 2] = ShotFlags;
            dest[Size + 3] = 0;
        }
        if (dest.size() >= static_cast<std::size_t>(Size + StateSize + LegacyTouchSize))
        {
            dest[Size + 3] = static_cast<std::uint8_t>(TouchFlags & ~(TouchSamplePresent | TouchSecondStep));
            W16(Slice(dest, static_cast<std::size_t>(Size + StateSize)), static_cast<std::uint16_t>(TouchDelta4X));
            W16(Slice(dest, static_cast<std::size_t>(Size + StateSize + 2)), static_cast<std::uint16_t>(TouchDelta4Y));
        }
        if (dest.size() >= static_cast<std::size_t>(FullSize))
        {
            dest[Size + 3] = TouchFlags;
            W32(Slice(dest, static_cast<std::size_t>(Size + StateSize + LegacyTouchSize)), TouchSampleSequence);
        }
        if (dest.size() >= static_cast<std::size_t>(ShotFullSize))
        {
            const auto at = static_cast<std::size_t>(FullSize);
            const ::OpenTK::Mathematics::Vector3 origin = HasShot ? ShotOrigin : ::OpenTK::Mathematics::Vector3::Zero;
            const ::OpenTK::Mathematics::Vector3 direction = HasShot ? ShotDirection : ::OpenTK::Mathematics::Vector3::Zero;
            WF(Slice(dest, at), origin.X);
            WF(Slice(dest, at + 4), origin.Y);
            WF(Slice(dest, at + 8), origin.Z);
            WF(Slice(dest, at + 12), direction.X);
            WF(Slice(dest, at + 16), direction.Y);
            WF(Slice(dest, at + 20), direction.Z);
            const auto history = static_cast<std::size_t>(FullSize + ShotSize);
            const std::uint8_t count = std::min<std::uint8_t>(ShotHistoryLength, ShotHistoryCount);
            At(dest, history) = count;
            At(dest, history + 1) = 0;
            At(dest, history + 2) = 0;
            At(dest, history + 3) = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(ShotHistoryCount); ++i)
            {
                const ShotEvent event = i < count ? ShotHistory[i] : ShotEvent{};
                const std::size_t entry = history + 4 + i * static_cast<std::size_t>(ShotEventSize);
                W32(Slice(dest, entry), event.Sequence);
                W32(Slice(dest, entry + 4), event.Frame);
                W32(Slice(dest, entry + 8), event.AckFrame);
                At(dest, entry + 12) = event.WeaponId;
                At(dest, entry + 13) = event.Charge;
                At(dest, entry + 14) = 0;
                At(dest, entry + 15) = 0;
                WF(Slice(dest, entry + 16), event.Origin.X);
                WF(Slice(dest, entry + 20), event.Origin.Y);
                WF(Slice(dest, entry + 24), event.Origin.Z);
                WF(Slice(dest, entry + 28), event.Direction.X);
                WF(Slice(dest, entry + 32), event.Direction.Y);
                WF(Slice(dest, entry + 36), event.Direction.Z);
            }
        }
        if (dest.size() >= static_cast<std::size_t>(BombFullSize))
        {
            const auto at = static_cast<std::size_t>(ShotFullSize);
            const std::uint8_t count = HasBombs ? std::min<std::uint8_t>(BombsLength, BombCount) : 0;
            std::uint8_t gone = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                gone = static_cast<std::uint8_t>(gone | (Bombs[i].Gone ? 1U << i : 0U));
            }
            At(dest, at) = count;
            At(dest, at + 1) = HasBombs ? 1 : 0;
            At(dest, at + 2) = gone;
            At(dest, at + 3) = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(BombCount); ++i)
            {
                const Bomb bomb = i < count ? Bombs[i] : Bomb{};
                const std::size_t entry = at + 4 + i * static_cast<std::size_t>(BombSize);
                W32(Slice(dest, entry), bomb.Sequence);
                WF(Slice(dest, entry + 4), bomb.Position.X);
                WF(Slice(dest, entry + 8), bomb.Position.Y);
                WF(Slice(dest, entry + 12), bomb.Position.Z);
            }
        }
    }
    void IntentPacket::SetTouchReport(const ::MphRead::Mods::Input::NativeTouchState::Reported& touch) noexcept
    {
        TouchFlags = static_cast<std::uint8_t>(TouchPresent | TouchSamplePresent
            | (touch.Down ? TouchDown : 0) | (touch.Continued ? TouchContinued : 0)
            | (touch.SecondStep ? TouchSecondStep : 0));
        TouchDelta4X = touch.Delta4X;
        TouchDelta4Y = touch.Delta4Y;
        TouchSampleSequence = touch.SampleSequence;
    }

    ::MphRead::Mods::Input::NativeTouchState::Reported IntentPacket::TouchReport() const noexcept
    {
        return {HasTouch() && (TouchFlags & TouchDown) != 0,
            HasTouch() && (TouchFlags & TouchContinued) != 0, TouchDelta4X, TouchDelta4Y,
            HasTouchSample() ? TouchSampleSequence : Frame / 2,
            HasTouchSample() ? (TouchFlags & TouchSecondStep) != 0 : (Frame & 1U) != 0};
    }

    IntentPacket IntentPacket::Read(std::span<const std::uint8_t> src)
    {
        auto presses = std::make_shared<std::vector<std::uint32_t>>(PressHistory);
        for (std::size_t index = 0; index < static_cast<std::size_t>(PressHistory); index++)
        {
            (*presses)[index] = R32(Slice(src, 21 + index * 4));
        }
        IntentPacket packet;
        packet.MatchId = R16(Slice(src, 74));
        packet.AuthorityEpoch = R64(Slice(src, 76));
        packet.SlotGeneration = R16(Slice(src, 84));
        packet.LifeId = R16(Slice(src, 86));
        packet.Frame = R32(Slice(src, 0));
        packet.Buttons = static_cast<IntentButtons>(R32(Slice(src, 4)));
        packet.Aim = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 8)), RF(Slice(src, 12)), RF(Slice(src, 16)));
        packet.WeaponSelect = At(src, 20);
        packet.Presses = std::move(presses);
        packet.Position = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 21 + PressHistory * 4)), RF(Slice(src, 25 + PressHistory * 4)), RF(Slice(src, 29 + PressHistory * 4)));
        packet.AmmoUa = R16(Slice(src, 33 + PressHistory * 4));
        packet.AmmoMissiles = R16(Slice(src, 35 + PressHistory * 4));
        packet.AckFrame = R32(Slice(src, 37 + PressHistory * 4));
        packet.AckSubFrame = At(src, 41 + PressHistory * 4);
        const bool full = src.size() >= static_cast<std::size_t>(Size + StateSize);
        packet.HasState = full;
        if (src.size() >= static_cast<std::size_t>(Size + StateSize + LegacyTouchSize))
        {
            packet.TouchFlags = static_cast<std::uint8_t>(src[Size + 3] & ~(TouchSamplePresent | TouchSecondStep));
            packet.TouchDelta4X = static_cast<std::int16_t>(R16(Slice(src, static_cast<std::size_t>(Size + StateSize))));
            packet.TouchDelta4Y = static_cast<std::int16_t>(R16(Slice(src, static_cast<std::size_t>(Size + StateSize + 2))));
        }
        if (src.size() >= static_cast<std::size_t>(FullSize))
        {
            packet.TouchFlags = src[Size + 3];
            packet.TouchSampleSequence = R32(Slice(src, static_cast<std::size_t>(Size + StateSize + LegacyTouchSize)));
        }
        packet.ChargeLevel = full ? src[Size] : static_cast<std::uint8_t>(0);
        packet.BoostDamage = full ? src[Size + 1] : static_cast<std::uint8_t>(0);
        packet.ShotFlags = full ? src[Size + 2] : static_cast<std::uint8_t>(0);
        if (src.size() >= static_cast<std::size_t>(ShotFullSize))
        {
            const auto at = static_cast<std::size_t>(FullSize);
            packet.ShotOrigin = ::OpenTK::Mathematics::Vector3(RF(Slice(src, at)), RF(Slice(src, at + 4)), RF(Slice(src, at + 8)));
            packet.ShotDirection = ::OpenTK::Mathematics::Vector3(
                RF(Slice(src, at + 12)), RF(Slice(src, at + 16)), RF(Slice(src, at + 20)));
            packet.HasShot = std::isfinite(packet.ShotOrigin.X) && std::isfinite(packet.ShotOrigin.Y)
                && std::isfinite(packet.ShotOrigin.Z) && std::isfinite(packet.ShotDirection.X)
                && std::isfinite(packet.ShotDirection.Y) && std::isfinite(packet.ShotDirection.Z)
                && packet.ShotDirection.LengthSquared() > 0.25F && packet.ShotDirection.LengthSquared() < 4.0F;
            const auto history = static_cast<std::size_t>(FullSize + ShotSize);
            const std::uint8_t count = std::min<std::uint8_t>(At(src, history), ShotHistoryCount);
            packet.ShotHistoryLength = count;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t entry = history + 4 + i * static_cast<std::size_t>(ShotEventSize);
                IntentPacket::ShotEvent& event = packet.ShotHistory[i];
                event.Sequence = R32(Slice(src, entry));
                event.Frame = R32(Slice(src, entry + 4));
                event.AckFrame = R32(Slice(src, entry + 8));
                event.WeaponId = At(src, entry + 12);
                event.Charge = At(src, entry + 13);
                event.Origin = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, entry + 16)), RF(Slice(src, entry + 20)), RF(Slice(src, entry + 24)));
                event.Direction = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, entry + 28)), RF(Slice(src, entry + 32)), RF(Slice(src, entry + 36)));
                const bool finite = std::isfinite(event.Origin.X) && std::isfinite(event.Origin.Y)
                    && std::isfinite(event.Origin.Z) && std::isfinite(event.Direction.X)
                    && std::isfinite(event.Direction.Y) && std::isfinite(event.Direction.Z)
                    && event.Direction.LengthSquared() < 4.0F;
                if (!finite)
                {
                    event.Origin = event.Direction = ::OpenTK::Mathematics::Vector3::Zero;
                }
            }
        }
        if (src.size() >= static_cast<std::size_t>(BombFullSize))
        {
            const auto at = static_cast<std::size_t>(ShotFullSize);
            packet.HasBombs = At(src, at + 1) != 0;
            packet.BombsLength = std::min<std::uint8_t>(At(src, at), BombCount);
            for (std::size_t i = 0; i < packet.BombsLength; ++i)
            {
                const std::size_t entry = at + 4 + i * static_cast<std::size_t>(BombSize);
                packet.Bombs[i].Sequence = R32(Slice(src, entry));
                packet.Bombs[i].Position = ::OpenTK::Mathematics::Vector3(
                    RF(Slice(src, entry + 4)), RF(Slice(src, entry + 8)), RF(Slice(src, entry + 12)));
                packet.Bombs[i].Gone = (At(src, at + 2) & (1U << i)) != 0;
            }
        }
        return packet;
    }
    std::int16_t DamageEvent::PackDirection(float value) noexcept
    {
        const std::int32_t rounded = ::MphRead::NativeRuntime::ConvertToInt32Net9(
            ::MphRead::NativeRuntime::RoundToEven(value * DirectionScale));
        return static_cast<std::int16_t>(std::clamp<std::int32_t>(rounded, -32768, 32767));
    }
    float DamageEvent::UnpackDirection(std::int16_t value) noexcept
    {
        return static_cast<float>(value) / DirectionScale;
    }
    void DamageEvent::Write(std::span<std::uint8_t> dest) const
    {
        W16(dest, EventId);
        W16(Slice(dest, 2), AttackerGeneration);
        W16(Slice(dest, 4), Damage);
        At(dest, 6) = AttackerSlot;
        At(dest, 7) = Beam;
        At(dest, 8) = Flags;
        WI16(Slice(dest, 9), PackDirection(Direction.X));
        WI16(Slice(dest, 11), PackDirection(Direction.Y));
        WI16(Slice(dest, 13), PackDirection(Direction.Z));
    }
    ImpactOffset ImpactOffset::From(::OpenTK::Mathematics::Vector3 offset) noexcept
    {
        ImpactOffset packed{};
        if (!std::isfinite(offset.X) || !std::isfinite(offset.Y) || !std::isfinite(offset.Z))
        {
            return packed;
        }
        const auto pack = [](float value)
        {
            return static_cast<std::int8_t>(std::clamp(std::lround(value * Scale), -127L, 127L));
        };
        packed.X = pack(offset.X);
        packed.Y = pack(offset.Y);
        packed.Z = pack(offset.Z);
        return packed;
    }
    DamageEvent DamageEvent::Read(std::span<const std::uint8_t> src)
    {
        DamageEvent value;
        value.EventId = R16(src);
        value.AttackerGeneration = R16(Slice(src, 2));
        value.Damage = R16(Slice(src, 4));
        value.AttackerSlot = At(src, 6);
        value.Beam = At(src, 7);
        value.Flags = At(src, 8);
        value.Direction = ::OpenTK::Mathematics::Vector3(
            UnpackDirection(RI16(Slice(src, 9))),
            UnpackDirection(RI16(Slice(src, 11))),
            UnpackDirection(RI16(Slice(src, 13))));
        return value;
    }
    DamageEvent PlayerState::EventAt(std::int32_t index) const
    {
        switch (index)
        {
        case 0: return Damage0;
        case 1: return Damage1;
        case 2: return Damage2;
        case 3: return Damage3;
        default: throw System::ArgumentOutOfRangeException("index");
        }
    }
    void PlayerState::Write(std::span<std::uint8_t> dest) const
    {
        if (dest.size() < Size) throw std::out_of_range("PlayerState protocol 19 requires " + std::to_string(Size) + " bytes");
        At(dest, 0) = SlotIndex;
        At(dest, 1) = Flags;
        WriteVec(Slice(dest, 2), Position);
        WriteVec(Slice(dest, 14), Speed);
        WriteVec(Slice(dest, 26), Facing);
        W16(Slice(dest, 38), Health);
        At(dest, 40) = CurrentWeapon;
        At(dest, 41) = Team;
        WI16(Slice(dest, 42), Points);
        W16(Slice(dest, 44), Kills);
        W16(Slice(dest, 46), Deaths);
        W16(Slice(dest, 48), SlotGeneration);
        W16(Slice(dest, 50), LifeId);
        W16(Slice(dest, 52), DamageEventId);
        for (std::int32_t i = 0; i < DamageHistory; i++)
        {
            EventAt(i).Write(Slice(dest, 54 + static_cast<std::size_t>(i) * DamageEvent::Size));
        }
        At(dest, LegacySize) = WeavelFlags;
        At(dest, LegacySize + 1) = HalfturretHealth;
        WriteVec(Slice(dest, LegacySize + 2), HalfturretPosition);
        At(dest, LegacySize + 14) = ImpactLaunchLow;
        At(dest, LegacySize + 15) = static_cast<std::uint8_t>(Impact.Y);
        std::uint8_t bearing = 0;
        if (Impact.Known() && (Impact.X != 0 || Impact.Z != 0))
        {
            const double angle = std::atan2(static_cast<double>(Impact.Z), static_cast<double>(Impact.X));
            bearing = static_cast<std::uint8_t>(static_cast<std::int32_t>(std::lround(angle * 128.0 / 3.141592653589793)) & 0xFF);
        }
        At(dest, LegacySize + 16) = bearing;
    }
    PlayerState PlayerState::Read(std::span<const std::uint8_t> src)
    {
        if (src.size() < Size) throw std::out_of_range("PlayerState layout older than protocol 19 is incompatible");
        PlayerState state;
        state.SlotIndex = At(src, 0);
        state.Flags = At(src, 1);
        state.Position = ReadVec(Slice(src, 2));
        state.Speed = ReadVec(Slice(src, 14));
        state.Facing = ReadVec(Slice(src, 26));
        state.Health = R16(Slice(src, 38));
        state.CurrentWeapon = At(src, 40);
        state.Team = At(src, 41);
        state.Points = RI16(Slice(src, 42));
        state.Kills = R16(Slice(src, 44));
        state.Deaths = R16(Slice(src, 46));
        state.SlotGeneration = R16(Slice(src, 48));
        state.LifeId = R16(Slice(src, 50));
        state.DamageEventId = R16(Slice(src, 52));
        state.Damage0 = DamageEvent::Read(Slice(src, 54));
        state.Damage1 = DamageEvent::Read(Slice(src, 54 + DamageEvent::Size));
        state.Damage2 = DamageEvent::Read(Slice(src, 54 + 2 * DamageEvent::Size));
        state.Damage3 = DamageEvent::Read(Slice(src, 54 + 3 * DamageEvent::Size));
        state.WeavelFlags = At(src, LegacySize);
        state.HalfturretHealth = At(src, LegacySize + 1);
        state.HalfturretPosition = ReadVec(Slice(src, LegacySize + 2));
        state.ImpactLaunchLow = At(src, LegacySize + 14);
        const auto height = static_cast<std::int8_t>(At(src, LegacySize + 15));
        if (height != ImpactOffset::None)
        {
            const double angle = static_cast<std::int8_t>(At(src, LegacySize + 16)) * 3.141592653589793 / 128.0;
            state.Impact = ImpactOffset::From(::OpenTK::Mathematics::Vector3(
                static_cast<float>(std::cos(angle)) * ImpactRadius, height / ImpactOffset::Scale,
                static_cast<float>(std::sin(angle)) * ImpactRadius));
        }

        DamageEvent latest{};
        for (std::int32_t i = DamageHistory - 1; i >= 0; i--)
        {
            const DamageEvent candidate = state.EventAt(i);
            if (candidate.EventId == state.DamageEventId)
            {
                latest = candidate;
                break;
            }
        }
        state.AttackerSlot = latest.EventId == 0 ? static_cast<std::uint8_t>(0xFF) : latest.AttackerSlot;
        state.DamageBeam = latest.EventId == 0 ? static_cast<std::uint8_t>(0xFF) : latest.Beam;
        state.DamageFlags = latest.Flags;
        state.HitDirection = latest.Direction;
        return state;
    }
    void PlayerState::WriteVec(std::span<std::uint8_t> dest, ::OpenTK::Mathematics::Vector3 value)
    {
        WF(Slice(dest, 0), value.X);
        WF(Slice(dest, 4), value.Y);
        WF(Slice(dest, 8), value.Z);
    }
    ::OpenTK::Mathematics::Vector3 PlayerState::ReadVec(std::span<const std::uint8_t> src) { return ::OpenTK::Mathematics::Vector3(RF(Slice(src, 0)), RF(Slice(src, 4)), RF(Slice(src, 8))); }
    void SnapshotHeader::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 13), MatchId);
        W64(Slice(dest, 15), AuthorityEpoch);
        W32(Slice(dest, 0), Frame);
        W32(Slice(dest, 4), Rng1);
        W32(Slice(dest, 8), Rng2);
        At(dest, 12) = PlayerCount;
    }
    SnapshotHeader SnapshotHeader::Read(std::span<const std::uint8_t> src)
    {
        SnapshotHeader packet;
        packet.MatchId = R16(Slice(src, 13));
        packet.AuthorityEpoch = R64(Slice(src, 15));
        packet.Frame = R32(Slice(src, 0));
        packet.Rng1 = R32(Slice(src, 4));
        packet.Rng2 = R32(Slice(src, 8));
        packet.PlayerCount = At(src, 12);
        return packet;
    }
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
    void HitClaimPacket::Write(std::span<std::uint8_t> dest) const
    {
        W16(Slice(dest, 31), MatchId);
        W64(Slice(dest, 33), AuthorityEpoch);
        W16(Slice(dest, 41), ShooterGeneration);
        W16(Slice(dest, 43), ShooterLifeId);
        W16(Slice(dest, 45), VictimGeneration);
        W16(Slice(dest, 47), VictimLifeId);
        W16(Slice(dest, 0), ClaimId);
        W32(Slice(dest, 2), Frame);
        W32(Slice(dest, 6), AckFrame);
        W32(Slice(dest, 10), LaunchFrame);
        At(dest, 14) = VictimSlot;
        At(dest, 15) = Beam;
        W16(Slice(dest, 16), Damage);
        At(dest, 18) = Flags;
        WF(Slice(dest, 19), HitPoint.X);
        WF(Slice(dest, 23), HitPoint.Y);
        WF(Slice(dest, 27), HitPoint.Z);
        WF(Slice(dest, 49), Impulse.X);
        WF(Slice(dest, 53), Impulse.Y);
        WF(Slice(dest, 57), Impulse.Z);
        At(dest, 61) = static_cast<std::uint8_t>(Impact.X);
        At(dest, 62) = static_cast<std::uint8_t>(Impact.Y);
        At(dest, 63) = static_cast<std::uint8_t>(Impact.Z);
        W32(Slice(dest, 64), ShotSequence);
        W16(Slice(dest, 68), TurretDamage);
        At(dest, 70) = Cause;
    }
    HitClaimPacket HitClaimPacket::Read(std::span<const std::uint8_t> src)
    {
        HitClaimPacket packet;
        packet.MatchId = R16(Slice(src, 31));
        packet.AuthorityEpoch = R64(Slice(src, 33));
        packet.ShooterGeneration = R16(Slice(src, 41));
        packet.ShooterLifeId = R16(Slice(src, 43));
        packet.VictimGeneration = R16(Slice(src, 45));
        packet.VictimLifeId = R16(Slice(src, 47));
        packet.ClaimId = R16(Slice(src, 0));
        packet.Frame = R32(Slice(src, 2));
        packet.AckFrame = R32(Slice(src, 6));
        packet.LaunchFrame = R32(Slice(src, 10));
        packet.VictimSlot = At(src, 14);
        packet.Beam = At(src, 15);
        packet.Damage = R16(Slice(src, 16));
        packet.Flags = At(src, 18);
        packet.HitPoint = ::OpenTK::Mathematics::Vector3(
            RF(Slice(src, 19)), RF(Slice(src, 23)), RF(Slice(src, 27)));
        packet.Impulse = ::OpenTK::Mathematics::Vector3(RF(Slice(src, 49)), RF(Slice(src, 53)), RF(Slice(src, 57)));
        packet.Impact.X = static_cast<std::int8_t>(At(src, 61));
        packet.Impact.Y = static_cast<std::int8_t>(At(src, 62));
        packet.Impact.Z = static_cast<std::int8_t>(At(src, 63));
        if (!std::isfinite(packet.Impulse.X) || !std::isfinite(packet.Impulse.Y) || !std::isfinite(packet.Impulse.Z)
            || packet.Impulse.LengthSquared() > 4.0F)
        {
            packet.Impulse = ::OpenTK::Mathematics::Vector3::Zero;
            packet.Flags = static_cast<std::uint8_t>(packet.Flags & ~FlagImpulse);
        }
        packet.ShotSequence = R32(Slice(src, 64));
        packet.TurretDamage = R16(Slice(src, 68));
        packet.Cause = At(src, 70) <= CauseDeathalt ? At(src, 70) : CauseHit;
        return packet;
    }
    void HitVerdictPacket::Write(std::span<std::uint8_t> dest,
        std::span<const std::pair<std::uint16_t, std::uint8_t>> entries,
        std::uint16_t matchId, std::uint64_t epoch, std::uint16_t generation,
        std::uint16_t lifeId)
    {
        At(dest, 0) = static_cast<std::uint8_t>(entries.size());
        W16(Slice(dest, 1), matchId);
        W64(Slice(dest, 3), epoch);
        W16(Slice(dest, 11), generation);
        W16(Slice(dest, 13), lifeId);
        for (std::size_t i = 0; i < entries.size(); i++)
        {
            const std::size_t at = HeaderSize + i * EntrySize;
            W16(Slice(dest, at), entries[i].first);
            At(dest, at + 2) = entries[i].second;
        }
    }
    std::string HitVerdictPacket::Describe(std::uint8_t result)
    {
        switch (result)
        {
        case ResultWrongLife: return "wrong lifecycle";
        case ResultGeometry: return "hit point outside reconciliation radius";
        case ResultDamageLimit: return "damage exceeds weapon limit";
        case ResultInvalidLaunch: return "launch frame follows hit frame";
        case ResultNoDamage: return "authority damage rules prevented the hit";
        case ResultApplied: return "applied";
        case ResultDuplicate: return "already resolved";
        case ResultDeadShooter: return "shooter was already dead when it fired";
        case ResultDeadVictim: return "victim was already down";
        case ResultRefused: return "refused";
        case ResultTooOld: return "older than the history";
        default: return "unknown";
        }
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
