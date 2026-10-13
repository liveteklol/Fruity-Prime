#include "Mods/Network/Server/Lobby/ServerMapUpload.hpp"
#include "Mods/Network/Protocol/Packets/MapTransferPackets.hpp"

#include "Metadata/Metadata.hpp"
#include "Mods/MapGen/CustomRooms.hpp"
#include "Mods/MapGen/Q3Bsp.hpp"
#include "NativeRuntime/System/Cryptography.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        std::atomic<bool> g_enabled{true};

        [[nodiscard]] std::string Hex(const std::array<std::uint8_t, 32>& hash, std::size_t bytes)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string text;
            for (std::size_t i = 0; i < bytes; ++i)
            {
                text.push_back(digits[hash[i] >> 4]);
                text.push_back(digits[hash[i] & 15]);
            }
            return text;
        }
    }

    ServerMapUpload::ServerMapUpload(ServerMapVotingHost& host) noexcept
        : _host(host)
    {
    }

    void ServerMapUpload::Enabled(bool value) noexcept { g_enabled.store(value); }
    bool ServerMapUpload::Enabled() noexcept { return g_enabled.load(); }

    std::uint32_t ServerMapUpload::ChunkCount() const noexcept
    {
        return (_bytes + MapChunkPacket::ChunkBytes - 1) / MapChunkPacket::ChunkBytes;
    }

    bool ServerMapUpload::Begin(const PeerHandle& peer, const std::string& roomKey, double now)
    {
        if (!Enabled() || peer == nullptr || roomKey.empty()
            || roomKey.size() >= static_cast<std::size_t>(MapWantPacket::MaxRoomBytes))
        {
            return false;
        }
        if (Active())
        {
            _host.TellVotingPeer(peer, "the server is already receiving a map; try again in a moment");
            return true;
        }
        _phase = Phase::AwaitOffer;
        _peer = peer;
        _clientId = peer->ClientId;
        _roomKey = roomKey;
        _lastProgress = now;
        _host.TellVotingPeer(peer, "the server does not have \"" + roomKey + "\"; asking you for it");
        _host.LogVote("asking slot " + std::to_string(peer->SlotIndex) + " for the pack holding " + roomKey);
        SendWant(0, 0);
        return true;
    }

    void ServerMapUpload::SendWant(std::uint32_t chunkIndex, std::uint32_t count)
    {
        MapWantPacket want{};
        want.RoomKey = _roomKey;
        want.Offset = chunkIndex * static_cast<std::uint32_t>(MapChunkPacket::ChunkBytes);
        want.Count = static_cast<std::uint16_t>(count);
        want.Write(_scratch);
        _host.SendVotePacket(_peer, PacketType::MapWant,
            std::span<const std::uint8_t>(_scratch.data(), MapWantPacket::Size));
    }

    void ServerMapUpload::HandleOffer(const PeerHandle& peer, std::span<const std::uint8_t> payload, double now)
    {
        if (_phase != Phase::AwaitOffer || peer == nullptr || peer->ClientId != _clientId
            || payload.size() < static_cast<std::size_t>(MapOfferPacket::Size))
        {
            return;
        }
        const MapOfferPacket offer = MapOfferPacket::Read(payload);
        if (!Runtime::StringEqualsOrdinalIgnoreCase(offer.RoomKey.value_or(""), _roomKey))
        {
            return;
        }
        if (offer.Bytes == 0 || offer.Bytes > MaxBytes)
        {
            Fail("that map is too large to send (the limit is "
                + std::to_string(MaxBytes / (1024u * 1024u)) + " MB)");
            return;
        }
        _bytes = offer.Bytes;
        _sha = offer.Sha256;
        _data.assign(_bytes, 0);
        _have.assign(ChunkCount(), false);
        _contiguous = 0;
        _phase = Phase::Receiving;
        _lastProgress = now;
        const std::uint32_t first = std::min(WindowChunks, ChunkCount());
        _wantedEnd = first;
        SendWant(0, first);
        _host.TellVotingPeer(peer, "receiving " + _roomKey + " (" + std::to_string(_bytes / 1024) + " KB)");
    }

    void ServerMapUpload::HandleChunk(const PeerHandle& peer, std::span<const std::uint8_t> payload, double now)
    {
        MapChunkPacket chunk{};
        if (_phase != Phase::Receiving || peer == nullptr || peer->ClientId != _clientId
            || !MapChunkPacket::TryRead(payload, chunk))
        {
            return;
        }
        const std::uint32_t chunkBytes = static_cast<std::uint32_t>(MapChunkPacket::ChunkBytes);
        if (chunk.Offset % chunkBytes != 0 || chunk.Offset >= _bytes)
        {
            return;
        }
        const std::uint32_t index = chunk.Offset / chunkBytes;
        const std::uint32_t expected = std::min(chunkBytes, _bytes - chunk.Offset);
        if (chunk.Data.size() != expected)
        {
            return;
        }
        if (!_have[index])
        {
            std::copy(chunk.Data.begin(), chunk.Data.end(), _data.begin() + chunk.Offset);
            _have[index] = true;
            _lastProgress = now;
        }
        while (_contiguous < ChunkCount() && _have[_contiguous])
        {
            ++_contiguous;
        }
        if (_contiguous == ChunkCount())
        {
            Finish(now);
            return;
        }
        // Keep the window full: ask for the next half as the first half lands.
        const std::uint32_t half = WindowChunks / 2;
        if (_wantedEnd < ChunkCount() && _wantedEnd - _contiguous <= half)
        {
            const std::uint32_t count = std::min(half, ChunkCount() - _wantedEnd);
            SendWant(_wantedEnd, count);
            _wantedEnd += count;
        }
    }

    void ServerMapUpload::Update(double now)
    {
        if (_phase == Phase::Idle)
        {
            return;
        }
        const auto& peers = _host.VotingPeers().All();
        if (std::none_of(peers.begin(), peers.end(),
                [&](const PeerHandle& p) { return p->ClientId == _clientId; }))
        {
            Reset();
            return;
        }
        if (_phase == Phase::Building)
        {
            if (_build != nullptr && _build->load() == 1)
            {
                Complete(now);
            }
            else if (now - _buildStarted > BuildGiveUpSeconds)
            {
                Fail("preparing " + _builtRoom + " took too long");
            }
            return;
        }
        const double quiet = now - _lastProgress;
        if (_phase == Phase::AwaitOffer)
        {
            if (quiet > OfferSeconds)
            {
                Fail("your game did not send \"" + _roomKey + "\"; it may not have that map");
            }
            return;
        }
        if (quiet > StallGiveUpSeconds)
        {
            Fail("the transfer of " + _roomKey + " stalled");
        }
        else if (quiet > StallResendSeconds)
        {
            // Ask again from the first hole; chunks already held are ignored.
            const std::uint32_t count = std::min(WindowChunks, ChunkCount() - _contiguous);
            SendWant(_contiguous, count);
            _wantedEnd = _contiguous + count;
            _lastProgress = now - StallResendSeconds + 0.25;
        }
    }

    void ServerMapUpload::Cancel(std::uint32_t clientId)
    {
        if (Active() && clientId == _clientId)
        {
            Reset();
        }
    }

    void ServerMapUpload::Fail(const std::string& why)
    {
        _host.TellVotingPeer(_peer, why);
        _host.LogVote("map transfer of " + _roomKey + " failed: " + why);
        Reset();
    }

    void ServerMapUpload::Reset()
    {
        _phase = Phase::Idle;
        _peer.reset();
        _data = {};
        _have.clear();
        _roomKey.clear();
        _builtRoom.clear();
        _builtFile.clear();
        _build.reset();
        _bytes = 0;
        _contiguous = 0;
        _wantedEnd = 0;
    }

    void ServerMapUpload::Finish(double now)
    {
        if (Runtime::Sha256HashData(_data) != _sha)
        {
            Fail("what arrived does not match what was offered");
            return;
        }
        namespace fs = std::filesystem;
        const std::string directory = MapGen::CustomRooms::MapDirectory();
        if (directory.empty())
        {
            Fail("this server has no map folder");
            return;
        }
        std::error_code error;
        const fs::path folder = Runtime::PathFromUtf8(directory) / "uploaded";
        fs::create_directories(folder, error);
        // The name is the hash's: nothing the sender chose reaches the disk.
        const fs::path file = folder / (Hex(_sha, 16) + ".pk3");
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(_data.data()), static_cast<std::streamsize>(_data.size()));
            if (!out)
            {
                Fail("the server could not store the map");
                return;
            }
        }
        const auto reject = [&](const std::string& why)
        {
            fs::remove(file, error);
            Fail(why);
        };
        bool holds = false;
        try
        {
            for (const std::string& level : MapGen::Q3Bsp::ListMaps(file.string()))
            {
                holds = holds || Runtime::StringEqualsOrdinalIgnoreCase(level, _roomKey);
            }
        }
        catch (...)
        {
            reject("that file is not a map pack the server can read");
            return;
        }
        if (!holds)
        {
            reject("that pack does not contain \"" + _roomKey + "\"");
            return;
        }
        try
        {
            MapGen::CustomRooms::Rescan();
            (void)::MphRead::Metadata::AppendLateRooms();
        }
        catch (...)
        {
            reject("the server could not load that map");
            return;
        }
        const std::optional<std::string> resolved = ServerMapVoting::ResolveRoomKey(_roomKey);
        if (!resolved.has_value())
        {
            reject("the server could not use that map as a multiplayer room");
            return;
        }
        _host.LogVote("received " + *resolved + " (" + std::to_string(_bytes) + " bytes) into " + file.string());
        // Listed is not loadable: the room's collision and node files are made
        // from the level on first use (the launcher does it when a match is
        // about to load, the server at start), and a vote that passes before
        // they exist rotates the match onto a room that cannot be read.
        // Build them now, off the server's loop.
        _builtRoom = *resolved;
        _builtFile = file.string();
        _buildStarted = now;
        _lastProgress = now;
        _data = {};
        _have.clear();
        _phase = Phase::Building;
        _build = std::make_shared<std::atomic<int>>(0);
        _host.TellVotingPeer(_peer, "received " + _builtRoom + "; preparing it");
        std::thread([room = _builtRoom, state = _build]()
        {
            try
            {
                MapGen::CustomRooms::EnsureReady(room);
            }
            catch (...)
            {
            }
            state->store(1);
        }).detach();
    }

    void ServerMapUpload::Complete(double now)
    {
        const std::optional<std::string> why = MapGen::CustomRooms::WhyUnplayable(_builtRoom);
        if (why.has_value())
        {
            std::error_code error;
            std::filesystem::remove(_builtFile, error);
            try
            {
                MapGen::CustomRooms::Rescan();
                (void)::MphRead::Metadata::AppendLateRooms();
            }
            catch (...)
            {
            }
            Fail(_builtRoom + " cannot be played here: " + *why);
            return;
        }
        _host.LogVote("built " + _builtRoom);
        const PeerHandle peer = _peer;
        const std::string room = _builtRoom;
        Reset();
        if (_arrived != nullptr)
        {
            _arrived(peer, room, now);
        }
    }
}
