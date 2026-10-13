#include "Mods/Network/Server/Match/ServerIntentChannel.hpp"

#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Server/Match/ServerIntentValidator.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/Runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    ServerIntentChannel::ServerIntentChannel(ServerPeerRegistry& peers,
        ServerAuthorityState& authority, ServerSlotIdentity& slots,
        ServerSimulationController& simulation, DedicatedServerSessionPort& session,
        ServerLobbyState& lobby, std::uint16_t& matchId, ScratchBuffer& scratch) noexcept
        : _peers(peers),
          _authority(authority),
          _slots(slots),
          _simulation(simulation),
          _session(session),
          _lobby(lobby),
          _matchId(matchId),
          _scratch(scratch)
    {
    }

    void ServerIntentChannel::Receive(const ReceivedPacket& packet, double now, NetTransport* transport)
    {
        if (_lobby.Phase() == SessionPhase::Lobby || _lobby.Phase() == SessionPhase::Starting)
        {
            return;
        }

        const ServerPeerRegistry::PeerHandle peer = _peers.Find(packet.Sender);
        if (peer == nullptr || (!_authority.HasAuthority() && !_simulation.Running()))
        {
            return;
        }

        const std::span<const std::uint8_t> payload = packet.Payload();
        peer->Liveness.MarkSeen(now);
        const auto slot = static_cast<std::size_t>(peer->SlotIndex);
        const std::uint16_t lifeId = _simulation.HasSimulation()
            ? NetPlayerLifecycle::Get(peer->SlotIndex)
            : _slots.Life(slot);
        const std::optional<IntentPacket> validatedIntent = ServerIntentValidator::Validate(
            payload, _matchId, _authority.Epoch(), _slots.Generation(slot), lifeId);
        if (!validatedIntent.has_value())
        {
            return;
        }

        const IntentPacket& intent = *validatedIntent;
        if (_simulation.HasSimulation())
        {
            _session.AcceptSlotIntent(peer->SlotIndex, intent);
        }

        // Stale movement is ignored, but a delayed shot can still be new to
        // every peer's shot ledger and must continue through the relay.
        const ServerPeerIntentState::Disposition disposition = peer->Intent.Observe(
            intent.Frame, intent.ShotHistoryLength != 0);
        if (disposition == ServerPeerIntentState::Disposition::StaleWithoutShots)
        {
            return;
        }
        if (disposition == ServerPeerIntentState::Disposition::Newest)
        {
            peer->Lobby.SetPostMatchReady(Runtime::HasFlag(intent.Buttons, IntentButtons::ReadyState));
        }

        _scratch[0] = static_cast<std::uint8_t>(peer->SlotIndex);
        std::copy(payload.begin(), payload.end(), _scratch.begin() + 1);
        for (const ServerPeerRegistry::PeerHandle& other : _peers.All())
        {
            if (other != peer && transport != nullptr)
            {
                transport->Send(other->EndPoint, PacketType::SlotIntent,
                    std::span<const std::uint8_t>(_scratch).first(payload.size() + 1));
            }
        }
    }
}
