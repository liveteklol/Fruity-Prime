#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include "Mods/Network/Domain/MatchDefinitionRules.hpp"
#include "Mods/Network/Protocol/Codec/PacketText.hpp"

#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] bool HasRule(SessionRules value, SessionRules flag) noexcept
        {
            return (value & flag) == flag;
        }
    }

    std::string ToString(LobbyCommandType value)
    {
        switch (value)
        {
        case LobbyCommandType::SetReady: return "SetReady";
        case LobbyCommandType::SetTeam: return "SetTeam";
        case LobbyCommandType::UpdateMatch: return "UpdateMatch";
        case LobbyCommandType::StartMatch: return "StartMatch";
        case LobbyCommandType::KickPlayer: return "KickPlayer";
        case LobbyCommandType::TransferOwner: return "TransferOwner";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    std::string ToString(LobbyResultCode value)
    {
        switch (value)
        {
        case LobbyResultCode::Ok: return "Ok";
        case LobbyResultCode::NotOwner: return "NotOwner";
        case LobbyResultCode::InvalidPhase: return "InvalidPhase";
        case LobbyResultCode::StaleRevision: return "StaleRevision";
        case LobbyResultCode::InvalidConfiguration: return "InvalidConfiguration";
        case LobbyResultCode::InvalidTeam: return "InvalidTeam";
        case LobbyResultCode::TeamFull: return "TeamFull";
        case LobbyResultCode::PlayersNotReady: return "PlayersNotReady";
        case LobbyResultCode::NotEnoughPlayers: return "NotEnoughPlayers";
        case LobbyResultCode::TargetNotFound: return "TargetNotFound";
        case LobbyResultCode::ServerBusy: return "ServerBusy";
        case LobbyResultCode::MapUnavailable: return "MapUnavailable";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    bool SessionStatePacket::LockTeams() const noexcept { return HasRule(RuleFlags, SessionRules::LockTeams); }
    bool SessionStatePacket::RequireReady() const noexcept { return HasRule(RuleFlags, SessionRules::RequireReady); }
    bool SessionStatePacket::AllowJoinInProgress() const noexcept
    {
        return HasRule(RuleFlags, SessionRules::AllowJoinInProgress);
    }

    void SessionStatePacket::Write(std::span<std::uint8_t> dest) const
    {
        const std::span<std::uint8_t> packet = Runtime::SpanSlice(dest, 0, Size);
        std::fill(packet.begin(), packet.end(), std::uint8_t{0});
        dest[0] = static_cast<std::uint8_t>(Phase);
        dest[1] = static_cast<std::uint8_t>(Policy);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 2), Revision);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 4), MatchId);
        dest[6] = OwnerSlot;
        dest[7] = MaxPlayers;
        dest[8] = static_cast<std::uint8_t>(Match.Format);
        dest[9] = static_cast<std::uint8_t>(Match.Mode);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 10), Match.TimeLimitSeconds);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 12), Match.PointGoal);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 14),
            static_cast<std::uint16_t>(RuleFlags | Match.Rules()));
        dest[16] = ExpectedParticipants;
        dest[17] = LoadedParticipants;
        dest[20] = Match.CustomTeams.TeamCount;
        dest[21] = Match.CustomTeams.TeamA;
        dest[22] = Match.CustomTeams.TeamB;
        dest[23] = Match.CustomTeams.TeamC;
        dest[24] = Match.CustomTeams.TeamD;
        dest[25] = WorldProfile.EntityLayerPlayers;
        dest[26] = static_cast<std::uint8_t>(WorldProfile.Resources);
        PacketText::Write(Runtime::SpanSlice(dest, 27, RoomKeyBytes), Match.RoomKey);
        Runtime::WriteUInt64LittleEndian(Runtime::SpanSlice(dest, Size - 8), AuthorityEpoch);
    }

    bool SessionStatePacket::TryRead(std::span<const std::uint8_t> src, SessionStatePacket& state)
    {
        state = SessionStatePacket{};
        if (src.size() != static_cast<std::size_t>(Size)
            || src[0] > static_cast<std::uint8_t>(SessionPhase::PostMatch)
            || src[1] > static_cast<std::uint8_t>(ServerSessionPolicy::Lobby)
            || src[7] < 1 || src[7] > 8
            || (src[6] != 0xFF && src[6] >= src[7])
            || (src[16] & ~((1 << src[7]) - 1)) != 0 || (src[17] & ~src[16]) != 0
            || src[8] > static_cast<std::uint8_t>(MatchFormat::Custom)
            || !::MphRead::IsDefinedGameMode(src[9]))
        {
            return false;
        }
        const auto flags = static_cast<SessionRules>(Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 14)));
        if ((static_cast<std::uint16_t>(flags) & ~127) != 0)
        {
            return false;
        }
        SessionStatePacket read;
        read.Phase = static_cast<SessionPhase>(src[0]);
        read.Policy = static_cast<ServerSessionPolicy>(src[1]);
        read.Revision = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 2));
        read.MatchId = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 4));
        read.AuthorityEpoch = Runtime::ReadUInt64LittleEndian(Runtime::SpanSlice(src, Size - 8));
        read.OwnerSlot = src[6];
        read.MaxPlayers = src[7];
        read.RuleFlags = flags;
        read.ExpectedParticipants = src[16];
        read.LoadedParticipants = src[17];
        read.WorldProfile = Multiplayer::MatchWorldProfile(src[25], static_cast<Multiplayer::ResourceSpawnProfile>(src[26]));
        read.Match.Format = static_cast<MatchFormat>(src[8]);
        read.Match.Mode = static_cast<::MphRead::GameMode>(src[9]);
        read.Match.CustomTeams = Multiplayer::TeamLayout(src[20], src[21], src[22], src[23], src[24]);
        read.Match.TimeLimitSeconds = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 10));
        read.Match.PointGoal = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 12));
        read.Match.RoomKey = PacketText::Read(src.subspan(27, RoomKeyBytes));
        read.Match.FriendlyFire = HasRule(flags, SessionRules::FriendlyFire);
        read.Match.AffinityWeapons = HasRule(flags, SessionRules::AffinityWeapons);
        read.Match.ShadowFreeze = HasRule(flags, SessionRules::ShadowFreeze);
        read.Match.HideOpponentHealth = HasRule(flags, SessionRules::HideOpponentHealth);
        state = read;
        std::string reason;
        return MatchDefinitionRules::Validate(state.Match, reason)
            && (state.Match.Format != MatchFormat::Custom || state.Match.CustomTeams.IsValid())
            && (state.WorldProfile.IsValid()
                || (state.Phase == SessionPhase::Lobby && state.WorldProfile == Multiplayer::MatchWorldProfile{}));
    }

    bool SessionStatePacket::IsNewer(std::uint16_t value, std::uint16_t previous) noexcept
    {
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(value - previous)) > 0;
    }

    void LobbyCommandPacket::Write(std::span<std::uint8_t> dest) const
    {
        const std::span<std::uint8_t> packet = Runtime::SpanSlice(dest, 0, Size);
        std::fill(packet.begin(), packet.end(), std::uint8_t{0});
        Runtime::WriteUInt32LittleEndian(dest, CommandId);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 4), ExpectedRevision);
        dest[6] = static_cast<std::uint8_t>(Type);
        dest[7] = TargetSlot;
        dest[8] = static_cast<std::uint8_t>(TeamIndex);
        dest[9] = Ready ? 1 : 0;
        Configuration.Write(Runtime::SpanSlice(dest, 10));
    }

    bool LobbyCommandPacket::TryRead(std::span<const std::uint8_t> src, LobbyCommandPacket& command)
    {
        command = LobbyCommandPacket{};
        if (src.size() != static_cast<std::size_t>(Size)
            || src[6] > static_cast<std::uint8_t>(LobbyCommandType::TransferOwner) || src[9] > 1)
        {
            return false;
        }
        SessionStatePacket config{};
        if (src[6] == static_cast<std::uint8_t>(LobbyCommandType::UpdateMatch)
            && !SessionStatePacket::TryRead(Runtime::SpanSlice(src, 10), config))
        {
            return false;
        }
        command.CommandId = Runtime::ReadUInt32LittleEndian(src);
        command.ExpectedRevision = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 4));
        command.Type = static_cast<LobbyCommandType>(src[6]);
        command.TargetSlot = src[7];
        command.TeamIndex = static_cast<std::int8_t>(src[8]);
        command.Ready = src[9] != 0;
        command.Configuration = config;
        return command.CommandId != 0;
    }

    void LobbyCommandResultPacket::Write(std::span<std::uint8_t> dest) const
    {
        Runtime::WriteUInt32LittleEndian(dest, CommandId);
        Runtime::ManagedAt(dest, 4) = static_cast<std::uint8_t>(ResultCode);
        Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, 5), CurrentRevision);
        PacketText::Write(Runtime::SpanSlice(dest, 7, 96), Reason);
    }

    bool LobbyCommandResultPacket::TryRead(std::span<const std::uint8_t> src, LobbyCommandResultPacket& result)
    {
        result = LobbyCommandResultPacket{};
        if (src.size() != static_cast<std::size_t>(Size) || src[4] > static_cast<std::uint8_t>(LobbyResultCode::MapUnavailable))
        {
            return false;
        }
        result.CommandId = Runtime::ReadUInt32LittleEndian(src);
        result.ResultCode = static_cast<LobbyResultCode>(src[4]);
        result.CurrentRevision = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, 5));
        result.Reason = PacketText::Read(src.subspan(7, 96));
        return true;
    }

    void MatchLoadedPacket::Write(std::span<std::uint8_t> dest) const
    {
        Runtime::WriteUInt16LittleEndian(dest, MatchId);
    }

    bool MatchLoadedPacket::TryRead(std::span<const std::uint8_t> src, MatchLoadedPacket& packet)
    {
        packet = src.size() == static_cast<std::size_t>(Size)
            ? MatchLoadedPacket{Runtime::ReadUInt16LittleEndian(src)} : MatchLoadedPacket{};
        return src.size() == static_cast<std::size_t>(Size);
    }

    void MatchLoadFailedPacket::Write(std::span<std::uint8_t> dest) const
    {
        Runtime::WriteUInt16LittleEndian(dest, MatchId);
        PacketText::Write(Runtime::SpanSlice(dest, 2, 96), Reason);
    }

    bool MatchLoadFailedPacket::TryRead(std::span<const std::uint8_t> src, MatchLoadFailedPacket& packet)
    {
        packet = src.size() == static_cast<std::size_t>(Size)
            ? MatchLoadFailedPacket{Runtime::ReadUInt16LittleEndian(src), PacketText::Read(src.subspan(2, 96))}
            : MatchLoadFailedPacket{};
        return src.size() == static_cast<std::size_t>(Size);
    }
}
