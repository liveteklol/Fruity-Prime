#include "Mods/Network/Session/SessionPacketDispatcher.hpp"

#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/SessionClaimChannel.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/UI/Voting/MapVote.hpp"
#include "Mods/Chat/ChatBox.hpp"
#include "Mods/MapPick.hpp"

#include <utility>

namespace MphRead::Mods::Network
{
    void SessionPacketDispatcher::Dispatch(ReceivedPacket packet, double time)
    {
        const PacketType type = packet.Type();
        if (DemoPlayback::IsActive() && (type == PacketType::Welcome || type == PacketType::Authority
            || type == PacketType::Bye || type == PacketType::Refused))
        {
            return;
        }
        const bool fromHost = NetSession::IsHostEndpoint(packet.Sender);
        if (NetSession::Role() == NetRole::Client && !NetSession::IsPlayback() && !fromHost)
        {
            return;
        }
        if ((type == PacketType::MapOffer || type == PacketType::MapChunk)
            && (NetSession::Role() != NetRole::Client || !fromHost))
        {
            return;
        }
        if (NetSession::Role() == NetRole::Client)
        {
            if (NetSession::RecordServerPacket(time))
            {
                Mods::Chat::ChatBox::System("Reconnected.");
            }
        }

        const std::span<const std::uint8_t> payload = packet.Payload();
        switch (type)
        {
        case PacketType::SessionState:
            if (NetSession::Role() == NetRole::Client)
            {
                SessionStatePacket session{};
                if (SessionStatePacket::TryRead(payload, session))
                {
                    NetSession::ApplySessionState(session);
                }
            }
            break;
        case PacketType::LobbyCommandResult:
            if (NetSession::Role() == NetRole::Client)
            {
                LobbyCommandResultPacket result{};
                if (LobbyCommandResultPacket::TryRead(payload, result))
                {
                    NetSession::ApplyLobbyResult(result);
                }
            }
            break;
        case PacketType::Hello:
            if (NetSession::Role() == NetRole::Host)
            {
                NetSession::HandleHello(packet, time);
            }
            break;
        case PacketType::Welcome:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandleWelcome(payload);
            }
            break;
        case PacketType::Intent:
            if (NetSession::Role() == NetRole::Host)
            {
                NetSession::HandleIntent(packet, time);
            }
            break;
        case PacketType::SlotIntent:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandleSlotIntent(packet);
            }
            break;
        case PacketType::Snapshot:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandleSnapshot(packet);
            }
            break;
        case PacketType::Authority:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandleAuthority(payload);
            }
            break;
        case PacketType::Refused:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandleRefused(payload);
            }
            break;
        case PacketType::Roster:
            if (NetSession::Role() == NetRole::Client)
            {
                RosterPacket roster{};
                if (payload.size() >= RosterPacket::Size && RosterPacket::TryRead(payload, roster))
                {
                    NetSession::ApplyRoster(std::move(roster));
                }
            }
            break;
        case PacketType::Ping:
            if (NetSession::Role() == NetRole::Client)
            {
                NetSession::HandlePing(payload);
            }
            break;
        case PacketType::MatchState:
        case PacketType::MapChange:
            if (NetSession::Role() == NetRole::Client)
            {
                if (payload.size() >= MatchStatePacket::Size)
                {
                    NetSession::ReceiveMatchState(MatchStatePacket::Read(payload),
                        type == PacketType::MapChange);
                }
            }
            break;
        case PacketType::HitVerdict:
            if (NetSession::Role() == NetRole::Client)
            {
                SessionClaimChannel::ApplyHitVerdicts(payload);
            }
            break;
        case PacketType::HitClaim:
            if (NetSession::Role() == NetRole::Host)
            {
                SessionClaimChannel::ReceiveHitClaim(packet, SessionPeers());
            }
            break;
        case PacketType::PickupVerdict:
            if (NetSession::Role() == NetRole::Client)
            {
                SessionClaimChannel::ApplyPickupVerdicts(payload);
            }
            break;
        case PacketType::PickupClaim:
            if (NetSession::Role() == NetRole::Host)
            {
                SessionClaimChannel::ReceivePickupClaim(packet, SessionPeers());
            }
            break;
        case PacketType::Chat:
            NetSession::HandleChat(packet, time);
            break;
        case PacketType::VoteState:
            if (NetSession::Role() == NetRole::Client && payload.size() >= VoteStatePacket::Size)
            {
                MapVote::Apply(VoteStatePacket::Read(payload));
            }
            break;
        case PacketType::MapChoices:
            if (NetSession::Role() == NetRole::Client && payload.size() >= MapChoicesPacket::Size)
            {
                Mods::MapPick::Apply(MapChoicesPacket::Read(payload));
            }
            break;
        case PacketType::Bye:
            NetSession::HandleBye(packet);
            break;
        default:
            break;
        }
    }
}
