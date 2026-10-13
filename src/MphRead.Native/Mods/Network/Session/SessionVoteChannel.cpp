#include "Mods/Network/Session/SessionVoteChannel.hpp"

#include <span>

namespace MphRead::Mods::Network
{
    void SessionVoteChannel::SendVote(SessionConnection& connection, NetRole role,
        std::uint8_t kind, const std::string& roomKey)
    {
        if (!connection.IsOpen() || !connection.HasHostEndpoint() || role != NetRole::Client)
        {
            return;
        }
        VotePacket vote{};
        vote.Kind = kind;
        vote.RoomKey = roomKey;
        vote.Write(connection.Buffer());
        connection.SendToHost(PacketType::Vote,
            std::span<const std::uint8_t>(connection.Buffer()).first(VotePacket::Size));
    }

    void SessionVoteChannel::SendMapPick(SessionConnection& connection, NetRole role, const std::string& roomKey)
    {
        if (!connection.IsOpen() || !connection.HasHostEndpoint() || role != NetRole::Client)
        {
            return;
        }
        MapPickPacket pick{};
        pick.RoomKey = roomKey;
        pick.Write(connection.Buffer());
        connection.SendToHost(PacketType::MapPick,
            std::span<const std::uint8_t>(connection.Buffer()).first(MapPickPacket::Size));
    }
}
