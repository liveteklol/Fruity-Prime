#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    enum class PacketType : std::uint8_t
    {
        Hello = 1,
        Welcome = 2,
        Intent = 3,
        Snapshot = 4,
        Bye = 5,
        Ping = 6,
        Pong = 7,
        MatchState = 8,
        MapChange = 9,
        Roster = 10,
        Identify = 11,
        Authority = 12,
        SlotIntent = 13,
        StatusQuery = 14,
        StatusReply = 15,
        MatchEnd = 16,
        MasterHeartbeat = 17,
        MasterQuery = 18,
        MasterList = 19,
        HostRequest = 20,
        HostReply = 21,
        Refused = 22,
        Chat = 23,
        Vote = 26,
        VoteState = 27,
        MapChoices = 28,
        MapPick = 29,
        HitClaim = 30,
        HitVerdict = 31,
        // Map transfer is negotiated before loading a custom room. All
        // requests are bounded and identify package hashes rather than peer
        // filenames.
        MapOffer = 32,
        MapWant = 33,
        MapChunk = 34,
        SessionState = 36,
        LobbyCommand = 37,
        LobbyCommandResult = 38,
        MatchLoaded = 39,
        MatchLoadFailed = 40,
        // A client's own player touched a power-up, and the authority's
        // answer (protocol 29): NetPickupClaims.
        PickupClaim = 41,
        PickupVerdict = 42,
        MapDone = 35
    };

    class NetConfig final
    {
    public:
        static constexpr std::uint16_t DefaultPort = 27888;
        static constexpr std::int32_t MaxPacketSize = 1232;
        static constexpr std::int32_t ProtocolVersion = 33;
        static constexpr std::int32_t IntentSendInterval = 1;
        static constexpr double TimeoutSeconds = 30.0;

        NetConfig() = delete;
    };

}
