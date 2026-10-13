#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "NativeRuntime/System/Guid.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    // Client-side lobby identity, authoritative session, and reliable command state.
    class ClientLobbyState final
    {
    public:
        [[nodiscard]] const std::optional<SessionStatePacket>& Session() const noexcept { return _session; }
        [[nodiscard]] bool HasSession() const noexcept { return _session.has_value(); }
        [[nodiscard]] SessionPhase Phase() const noexcept;
        [[nodiscard]] std::uint16_t SessionRevision() const noexcept;
        [[nodiscard]] std::optional<MatchDefinition> ActiveMatchDefinition() const;
        [[nodiscard]] bool IsLocalOwner(std::int32_t localSlot) const noexcept;
        [[nodiscard]] bool IsInLobby() const noexcept;
        [[nodiscard]] bool IsStarting() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;
        [[nodiscard]] bool IsPostMatch() const noexcept;
        [[nodiscard]] bool CanEditLobby(std::int32_t localSlot) const noexcept;
        [[nodiscard]] bool Persistent() const noexcept;
        [[nodiscard]] bool FreezeGameplay() const noexcept;
        [[nodiscard]] bool ShouldLoadMatch(std::int32_t localSlot) const noexcept;
        [[nodiscard]] const std::string& Message() const noexcept { return _message; }
        [[nodiscard]] bool HasPendingCommand() const noexcept { return !_pendingCommands.empty(); }
        [[nodiscard]] std::uint16_t RosterSessionRevision() const noexcept { return _rosterRevision; }

        [[nodiscard]] const ::MphRead::NativeRuntime::Guid& OwnerToken() const noexcept { return _ownerToken; }
        void SetOwnerToken(const ::MphRead::NativeRuntime::Guid& value) noexcept { _ownerToken = value; }

        [[nodiscard]] bool AcceptsSessionState(const SessionStatePacket& state,
            const std::optional<MatchStatePacket>& matchState) const;
        [[nodiscard]] bool AcceptsMatchState(const MatchStatePacket& state) const noexcept;
        // Returns true when applying the packet begins a different match.
        [[nodiscard]] bool SetSession(const SessionStatePacket& state);
        void SetRosterSessionRevision(std::uint16_t value) noexcept { _rosterRevision = value; }
        void SetMessage(std::string value) { _message = std::move(value); }

        [[nodiscard]] std::uint32_t NextCommandId() noexcept;
        void QueueCommand(const LobbyCommandPacket& packet, double now);
        [[nodiscard]] bool AcknowledgeCommand(std::uint32_t id);

        template <typename SendCommand>
        void RetryPendingCommands(double now, SendCommand&& sendCommand)
        {
            for (auto it = _pendingCommands.begin(); it != _pendingCommands.end(); ++it)
            {
                PendingCommand& pending = it->second;
                if (now - pending.SentAt < std::min(1.0, 0.25 * (pending.Attempts + 1)))
                {
                    continue;
                }
                if (pending.Attempts >= 4)
                {
                    _message = "The server did not acknowledge the command. Check the current lobby and try again.";
                    _pendingCommands.erase(it);
                    break;
                }
                pending.Attempts++;
                pending.SentAt = now;
                sendCommand(pending.Packet);
            }
        }

        [[nodiscard]] bool IdentityUpdateDue(double now, double interval) noexcept;
        [[nodiscard]] bool LoadedMatchRetryDue(double now, double interval, bool starting) const noexcept;
        [[nodiscard]] std::optional<std::uint16_t> MarkMatchLoaded(double now, double retryInterval) noexcept;

        void Reset();

    private:
        struct PendingCommand final
        {
            LobbyCommandPacket Packet{};
            double SentAt = 0;
            std::int32_t Attempts = 0;
        };

        std::optional<SessionStatePacket> _session{};
        std::string _message{};
        ::MphRead::NativeRuntime::Guid _ownerToken{};
        std::uint32_t _nextCommandId = 0;
        std::optional<std::uint16_t> _loadedMatch{};
        std::uint16_t _rosterRevision = 0;
        double _lastLoadAck = 0;
        double _lastIdentity = 0;
        std::map<std::uint32_t, PendingCommand> _pendingCommands{};
    };
}
