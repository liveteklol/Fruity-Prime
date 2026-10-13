#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    class MapRotation;
    class NetTransport;

    // Adapts map-vote effects to the server's concrete peers, rotation, chat,
    // transport, and match-lifecycle operations without making the server
    // facade implement the voting service's host interface.
    class ServerMapVotingHostAdapter final : public ServerMapVotingHost
    {
    public:
        using ScratchBuffer = std::array<std::uint8_t, NetConfig::MaxPacketSize>;

        struct Callbacks final
        {
            void* Context = nullptr;
            MatchDefinition (*ReadDefinition)(void*) = nullptr;
            void (*ApplyAcceptedVote)(void*, double, const std::string&, GameMode) = nullptr;
            void (*WriteLog)(const std::string&) = nullptr;
        };

        ServerMapVotingHostAdapter(ServerPeerRegistry& peers, const bool& mapVotesAllowed,
            ServerLobbyState& lobby, const std::shared_ptr<MapRotation>& rotation,
            const std::unique_ptr<NetTransport>& transport, ScratchBuffer& scratch,
            Callbacks callbacks) noexcept;

        [[nodiscard]] ServerPeerRegistry& VotingPeers() noexcept override { return _peers; }
        [[nodiscard]] bool MapVotesAllowed() const noexcept override { return _mapVotesAllowed; }
        [[nodiscard]] SessionPhase VotingPhase() const noexcept override { return _lobby.Phase(); }
        [[nodiscard]] MatchDefinition VotingDefinition() const override;
        [[nodiscard]] GameMode VotingModeForRoom(const std::string& roomKey) const override;
        void SetPendingMapPick(const std::string& roomKey, GameMode mode) override;
        void ClearPendingMapPick() override;
        void MarkVotingPeerSeen(const ServerPeerRegistry::PeerHandle& peer, double now) noexcept override;
        void TellVotingPeer(const ServerPeerRegistry::PeerHandle& peer, const std::string& text) override;
        void AnnounceVote(const std::string& text) override;
        void LogVote(const std::string& text) override;
        void SendVotePacket(const ServerPeerRegistry::PeerHandle& peer, PacketType type,
            std::span<const std::uint8_t> payload) override;
        void ApplyAcceptedMapVote(double now, const std::string& roomKey, GameMode mode) override;

    private:
        ServerPeerRegistry& _peers;
        const bool& _mapVotesAllowed;
        ServerLobbyState& _lobby;
        const std::shared_ptr<MapRotation>& _rotation;
        const std::unique_ptr<NetTransport>& _transport;
        ScratchBuffer& _scratch;
        Callbacks _callbacks;
    };
}
