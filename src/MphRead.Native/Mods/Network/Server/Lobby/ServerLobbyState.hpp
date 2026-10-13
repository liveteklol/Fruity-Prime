#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "NativeRuntime/System/Guid.hpp"

#include <cstdint>
#include <utility>

namespace MphRead::Mods::Network
{
    // Owns one dedicated server's lobby policy, match snapshots and load barrier.
    class ServerLobbyState final
    {
    public:
        [[nodiscard]] ServerSessionPolicy Policy() const noexcept { return _policy; }
        void SetPolicy(ServerSessionPolicy value) noexcept { _policy = value; }
        [[nodiscard]] MatchFormat Format() const noexcept { return _format; }
        void SetFormat(MatchFormat value) noexcept { _format = value; }
        [[nodiscard]] bool RequireReady() const noexcept { return _requireReady; }
        [[nodiscard]] bool AllowJoinInProgress() const noexcept { return _allowJoinInProgress; }
        void SetSessionOptions(bool requireReady, bool allowJoinInProgress) noexcept
        {
            _requireReady = requireReady;
            _allowJoinInProgress = allowJoinInProgress;
        }
        [[nodiscard]] const NativeRuntime::Guid& OwnerToken() const noexcept { return _ownerToken; }
        void SetOwnerToken(const NativeRuntime::Guid& value) noexcept { _ownerToken = value; }
        [[nodiscard]] SessionPhase Phase() const noexcept { return _phase; }
        void SetPhase(SessionPhase value) noexcept { _phase = value; }
        [[nodiscard]] const MatchDefinition& LobbyMatch() const noexcept { return _lobbyMatch; }
        void SetLobbyMatch(MatchDefinition value) { _lobbyMatch = std::move(value); }
        [[nodiscard]] const MatchDefinition& FrozenMatch() const noexcept { return _frozenMatch; }
        void FreezeMatch(MatchDefinition value) { _frozenMatch = std::move(value); }
        [[nodiscard]] const Multiplayer::MatchWorldProfile& FrozenWorldProfile() const noexcept { return _frozenWorldProfile; }
        void FreezeWorldProfile(Multiplayer::MatchWorldProfile value) noexcept { _frozenWorldProfile = value; }
        [[nodiscard]] const Multiplayer::MatchWorldProfile& MapWorldProfile() const noexcept { return _mapWorldProfile; }
        void SetMapWorldProfile(Multiplayer::MatchWorldProfile value) noexcept { _mapWorldProfile = value; }
        [[nodiscard]] bool LockTeams() const noexcept { return _lockTeams; }
        void SetLockTeams(bool value) noexcept { _lockTeams = value; }
        [[nodiscard]] std::uint16_t SessionRevision() const noexcept { return _sessionRevision; }
        [[nodiscard]] std::uint16_t AdvanceSessionRevision() noexcept
        {
            const std::uint16_t previous = _sessionRevision;
            ++_sessionRevision;
            return previous;
        }
        [[nodiscard]] std::uint32_t LobbyOwnerClientId() const noexcept { return _lobbyOwnerClientId; }
        void SetLobbyOwnerClientId(std::uint32_t value) noexcept { _lobbyOwnerClientId = value; }

        [[nodiscard]] std::uint8_t ExpectedLoadedSlots() const noexcept { return _expectedLoadedSlots; }
        [[nodiscard]] std::uint8_t LoadedSlots() const noexcept { return _loadedSlots; }
        [[nodiscard]] bool IsExpectedLoadedSlot(std::int32_t slot) const noexcept
        {
            const std::uint8_t mask = SlotMask(slot);
            return mask != 0 && (_expectedLoadedSlots & mask) != 0;
        }
        [[nodiscard]] bool IsLoadedSlot(std::int32_t slot) const noexcept
        {
            const std::uint8_t mask = SlotMask(slot);
            return mask != 0 && (_loadedSlots & mask) != 0;
        }
        void ExpectLoadedSlot(std::int32_t slot) noexcept
        {
            _expectedLoadedSlots = static_cast<std::uint8_t>(_expectedLoadedSlots | SlotMask(slot));
        }
        void MarkLoadedSlot(std::int32_t slot) noexcept
        {
            _loadedSlots = static_cast<std::uint8_t>(_loadedSlots | SlotMask(slot));
        }
        void RemoveLoadedSlot(std::int32_t slot) noexcept
        {
            const std::uint8_t mask = SlotMask(slot);
            _expectedLoadedSlots = static_cast<std::uint8_t>(_expectedLoadedSlots & ~mask);
            _loadedSlots = static_cast<std::uint8_t>(_loadedSlots & ~mask);
        }
        [[nodiscard]] bool LoadBarrierComplete() const noexcept
        {
            return (_loadedSlots & _expectedLoadedSlots) == _expectedLoadedSlots;
        }
        void ResetLoadBarrier() noexcept { _expectedLoadedSlots = _loadedSlots = 0; }
        [[nodiscard]] double StartDeadline() const noexcept { return _startDeadline; }
        void SetStartDeadline(double value) noexcept { _startDeadline = value; }

    private:
        [[nodiscard]] static std::uint8_t SlotMask(std::int32_t slot) noexcept
        {
            if (slot < 0 || slot >= static_cast<std::int32_t>(NetworkSlotCapacity))
            {
                return 0;
            }
            return static_cast<std::uint8_t>(1U << slot);
        }

        ServerSessionPolicy _policy = ServerSessionPolicy::Continuous;
        MatchFormat _format = MatchFormat::Auto;
        bool _requireReady = true;
        bool _allowJoinInProgress = true;
        NativeRuntime::Guid _ownerToken{};
        SessionPhase _phase = SessionPhase::InMatch;
        MatchDefinition _lobbyMatch{};
        MatchDefinition _frozenMatch{};
        Multiplayer::MatchWorldProfile _frozenWorldProfile{};
        Multiplayer::MatchWorldProfile _mapWorldProfile{};
        bool _lockTeams = false;
        std::uint16_t _sessionRevision = 1;
        std::uint32_t _lobbyOwnerClientId = 0;
        std::uint8_t _expectedLoadedSlots = 0;
        std::uint8_t _loadedSlots = 0;
        double _startDeadline = 0.0;
    };
}
