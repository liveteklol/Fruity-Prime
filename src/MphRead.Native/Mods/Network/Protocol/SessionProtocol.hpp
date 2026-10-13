#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"

#include "Mods/Multiplayer/MatchWorldProfile.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    // Fixed, bounded control packets. TryRead is the only wire entry point.
    struct SessionStatePacket final
    {
        static constexpr std::int32_t RoomKeyBytes = 40;
        static constexpr std::int32_t Size = 35 + RoomKeyBytes;

        std::uint64_t AuthorityEpoch = 0;
        SessionPhase Phase = SessionPhase::Lobby;
        ServerSessionPolicy Policy = ServerSessionPolicy::Continuous;
        std::uint16_t Revision = 0;
        std::uint16_t MatchId = 0;
        std::uint8_t OwnerSlot = 0;
        std::uint8_t MaxPlayers = 0;
        std::uint8_t ExpectedParticipants = 0;
        std::uint8_t LoadedParticipants = 0;
        SessionRules RuleFlags = SessionRules::None;
        MatchDefinition Match{};
        Multiplayer::MatchWorldProfile WorldProfile{};

        [[nodiscard]] bool LockTeams() const noexcept;
        [[nodiscard]] bool RequireReady() const noexcept;
        [[nodiscard]] bool AllowJoinInProgress() const noexcept;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, SessionStatePacket& state);
        [[nodiscard]] static bool IsNewer(std::uint16_t value, std::uint16_t previous) noexcept;
    };

    enum class LobbyCommandType : std::uint8_t { SetReady, SetTeam, UpdateMatch, StartMatch, KickPlayer, TransferOwner };
    // LobbyCommandType.ToString().
    [[nodiscard]] std::string ToString(LobbyCommandType value);
    enum class LobbyResultCode : std::uint8_t
    {
        Ok, NotOwner, InvalidPhase, StaleRevision, InvalidConfiguration, InvalidTeam,
        TeamFull, PlayersNotReady, NotEnoughPlayers, TargetNotFound, ServerBusy, MapUnavailable
    };
    // LobbyResultCode.ToString().
    [[nodiscard]] std::string ToString(LobbyResultCode value);

    struct LobbyCommandPacket final
    {
        static constexpr std::int32_t Size = 10 + SessionStatePacket::Size;

        std::uint32_t CommandId = 0;
        std::uint16_t ExpectedRevision = 0;
        LobbyCommandType Type = LobbyCommandType::SetReady;
        std::uint8_t TargetSlot = 0;
        std::int8_t TeamIndex = 0;
        bool Ready = false;
        SessionStatePacket Configuration{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, LobbyCommandPacket& command);
    };

    struct LobbyCommandResultPacket final
    {
        static constexpr std::int32_t Size = 7 + 96;

        std::uint32_t CommandId = 0;
        LobbyResultCode ResultCode = LobbyResultCode::Ok;
        std::uint16_t CurrentRevision = 0;
        std::string Reason{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, LobbyCommandResultPacket& result);
    };

    struct MatchLoadedPacket final
    {
        static constexpr std::int32_t Size = 2;

        std::uint16_t MatchId = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, MatchLoadedPacket& packet);
    };

    struct MatchLoadFailedPacket final
    {
        static constexpr std::int32_t Size = 98;

        std::uint16_t MatchId = 0;
        std::string Reason{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, MatchLoadFailedPacket& packet);
    };
}
