#pragma once

#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    // Receives a .pk3 from the player who called a vote for a map this server
    // does not have, so the vote can go ahead. One transfer at a time, the
    // server asking for every piece (see MapTransferPackets.hpp).
    //
    // What is accepted is deliberately narrow: a size cap, a hash the sender
    // committed to up front, a file the importer can open that actually holds
    // the level asked for, and a name the server chooses (never the sender's).
    // -nomapupload switches the whole thing off.
    class ServerMapUpload final
    {
    public:
        using Peer = ServerPeerRegistry::Peer;
        using PeerHandle = ServerPeerRegistry::PeerHandle;
        using Arrived = std::function<void(const PeerHandle& peer, const std::string& roomKey, double now)>;

        static constexpr std::uint32_t MaxBytes = 48u * 1024u * 1024u;
        static constexpr std::uint32_t WindowChunks = 32;
        static constexpr double OfferSeconds = 5.0;
        static constexpr double StallResendSeconds = 1.0;
        static constexpr double StallGiveUpSeconds = 10.0;
        static constexpr double BuildGiveUpSeconds = 180.0;

        explicit ServerMapUpload(ServerMapVotingHost& host) noexcept;

        static void Enabled(bool value) noexcept;
        [[nodiscard]] static bool Enabled() noexcept;

        void OnArrived(Arrived callback) { _arrived = std::move(callback); }

        // Asks the peer for the pack holding roomKey. False when the server
        // will not (switched off, unusable name), in which case nothing was sent.
        [[nodiscard]] bool Begin(const PeerHandle& peer, const std::string& roomKey, double now);
        void HandleOffer(const PeerHandle& peer, std::span<const std::uint8_t> payload, double now);
        void HandleChunk(const PeerHandle& peer, std::span<const std::uint8_t> payload, double now);
        void Update(double now);
        void Cancel(std::uint32_t clientId);

        [[nodiscard]] bool Active() const noexcept { return _phase != Phase::Idle; }

    private:
        enum class Phase : std::uint8_t { Idle, AwaitOffer, Receiving, Building };

        void SendWant(std::uint32_t chunkIndex, std::uint32_t count);
        void Fail(const std::string& why);
        void Finish(double now);
        void Complete(double now);
        void Reset();
        [[nodiscard]] std::uint32_t ChunkCount() const noexcept;

        ServerMapVotingHost& _host;
        Phase _phase = Phase::Idle;
        PeerHandle _peer{};
        std::uint32_t _clientId = 0;
        std::string _roomKey{};
        double _lastProgress = 0.0;
        std::uint32_t _bytes = 0;
        std::array<std::uint8_t, 32> _sha{};
        std::vector<std::uint8_t> _data{};
        std::vector<bool> _have{};
        std::uint32_t _contiguous = 0;
        std::uint32_t _wantedEnd = 0;
        // The room is made playable (its collision and node files built) on a
        // thread of its own, so the server keeps answering while it runs.
        std::shared_ptr<std::atomic<int>> _build{};
        std::string _builtRoom{};
        std::string _builtFile{};
        double _buildStarted = 0.0;
        Arrived _arrived{};
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _scratch{};
    };
}
