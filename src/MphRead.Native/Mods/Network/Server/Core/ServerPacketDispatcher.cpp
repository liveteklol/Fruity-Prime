#include "Mods/Network/Server/Core/ServerPacketDispatcher.hpp"

#include "Mods/Network/Server/Core/DedicatedServer.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

namespace MphRead::Mods::Network
{
    void ServerPacketDispatcher::Dispatch(
        DedicatedServer& server, const ReceivedPacket& packet, double now)
    {
        switch (packet.Type())
        {
        case PacketType::LobbyCommand:
            server.HandleLobbyCommand(packet, now);
            break;
        case PacketType::MatchLoaded:
            server.HandleMatchLoaded(packet, now);
            break;
        case PacketType::MatchLoadFailed:
            server.HandleMatchLoadFailed(packet);
            break;
        case PacketType::Hello:
            server.HandleHello(packet, now);
            break;
        case PacketType::Intent:
            server.HandleIntent(packet, now);
            break;
        case PacketType::Snapshot:
            server.HandleSnapshot(packet, now);
            break;
        case PacketType::Bye:
            server.HandleBye(packet);
            break;
        case PacketType::Identify:
            server.HandleIdentify(packet, now);
            break;
        case PacketType::Ping:
            server.HandlePing(packet);
            break;
        case PacketType::Pong:
            server.HandlePong(packet, now);
            break;
        case PacketType::HostRequest:
            server.HandleHostRequest(packet, now);
            break;
        case PacketType::StatusQuery:
            server.HandleStatusQuery(packet, now);
            break;
        case PacketType::MatchEnd:
            server.HandleMatchEnd(packet, now);
            break;
        case PacketType::Chat:
            server.HandleChat(packet, now);
            break;
        case PacketType::Vote:
            server.HandleVote(packet, now);
            break;
        case PacketType::MapPick:
            server.HandleMapPick(packet, now);
            break;
        case PacketType::MapOffer:
            server.HandleMapOffer(packet, now);
            break;
        case PacketType::MapChunk:
            server.HandleMapChunk(packet, now);
            break;
        case PacketType::HitClaim:
            server.HandleHitClaim(packet, now);
            break;
        case PacketType::PickupClaim:
            server.HandlePickupClaim(packet, now);
            break;
        default:
            break;
        }
    }
}
