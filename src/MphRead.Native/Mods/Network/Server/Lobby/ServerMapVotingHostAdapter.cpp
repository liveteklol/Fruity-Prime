#include "Mods/Network/Server/Lobby/ServerMapVotingHostAdapter.hpp"

#include "Mods/Network/Server/Core/ServerChatChannel.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Runtime.hpp"

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    ServerMapVotingHostAdapter::ServerMapVotingHostAdapter(ServerPeerRegistry& peers,
        const bool& mapVotesAllowed, ServerLobbyState& lobby,
        const std::shared_ptr<MapRotation>& rotation,
        const std::unique_ptr<NetTransport>& transport, ScratchBuffer& scratch,
        Callbacks callbacks) noexcept
        : _peers(peers),
          _mapVotesAllowed(mapVotesAllowed),
          _lobby(lobby),
          _rotation(rotation),
          _transport(transport),
          _scratch(scratch),
          _callbacks(callbacks)
    {
    }

    MatchDefinition ServerMapVotingHostAdapter::VotingDefinition() const
    {
        return _callbacks.ReadDefinition != nullptr
            ? _callbacks.ReadDefinition(_callbacks.Context) : MatchDefinition{};
    }

    GameMode ServerMapVotingHostAdapter::VotingModeForRoom(const std::string& roomKey) const
    {
        for (const std::shared_ptr<const RotationEntry>& entry : _rotation->Entries())
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(entry->RoomKey, roomKey))
            {
                return entry->Mode;
            }
        }
        return VotingDefinition().Mode;
    }

    void ServerMapVotingHostAdapter::SetPendingMapPick(const std::string& roomKey, GameMode mode)
    {
        _rotation->PlayNext(roomKey, mode);
    }

    void ServerMapVotingHostAdapter::ClearPendingMapPick()
    {
        _rotation->ClearPending();
    }

    void ServerMapVotingHostAdapter::MarkVotingPeerSeen(
        const ServerPeerRegistry::PeerHandle& peer, double now) noexcept
    {
        if (peer != nullptr)
        {
            peer->Liveness.MarkSeen(now);
        }
    }

    void ServerMapVotingHostAdapter::TellVotingPeer(
        const ServerPeerRegistry::PeerHandle& peer, const std::string& text)
    {
        ServerChatChannel::Tell(_transport.get(), _scratch, peer, text);
    }

    void ServerMapVotingHostAdapter::AnnounceVote(const std::string& text)
    {
        ServerChatChannel::Announce(_peers, _transport.get(), _scratch, text);
    }

    void ServerMapVotingHostAdapter::LogVote(const std::string& text)
    {
        if (_callbacks.WriteLog != nullptr)
        {
            _callbacks.WriteLog(text);
        }
    }

    void ServerMapVotingHostAdapter::SendVotePacket(const ServerPeerRegistry::PeerHandle& peer,
        PacketType type, std::span<const std::uint8_t> payload)
    {
        if (_transport != nullptr && peer != nullptr)
        {
            _transport->Send(peer->EndPoint, type, payload);
        }
    }

    void ServerMapVotingHostAdapter::ApplyAcceptedMapVote(
        double now, const std::string& roomKey, GameMode mode)
    {
        _rotation->PlayNext(roomKey, mode);
        if (_callbacks.ApplyAcceptedVote != nullptr)
        {
            _callbacks.ApplyAcceptedVote(_callbacks.Context, now, roomKey, mode);
        }
    }
}
