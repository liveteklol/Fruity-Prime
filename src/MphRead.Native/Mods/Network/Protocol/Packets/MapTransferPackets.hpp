#pragma once

#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/ChatPackets.hpp"
#include "Mods/Network/Protocol/Packets/LobbyPackets.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    // A client hands the server a .pk3 it does not have, so a player can call
    // a vote for a map the server has never seen. The server pulls: it asks,
    // the client answers, and nothing is sent that was not asked for.
    //
    //   server -> client  MapWant   "send me the pack holding this level"
    //                               (Count 0: just tell me what it is)
    //   client -> server  MapOffer  the pack's size and SHA-256
    //   server -> client  MapWant   a window of chunks, from Offset
    //   client -> server  MapChunk  one piece of the file, as asked
    //
    // Additive: a build that does not know these types drops them, so no
    // protocol bump. Without an answer the vote simply gets "no map called".
    struct MapWantPacket
    {
        static constexpr std::int32_t MaxRoomBytes = VotePacket::MaxRoomBytes;
        static constexpr std::int32_t Size = MaxRoomBytes + 4 + 2;

        std::optional<std::string> RoomKey{};
        std::uint32_t Offset = 0;
        std::uint16_t Count = 0;

        void Write(std::span<std::uint8_t> dest) const
        {
            using namespace Protocol::Wire;
            Clear(Slice(dest, 0, Size));
            ChatPacket::WriteAscii(Slice(dest, 0, MaxRoomBytes), RoomKey);
            W32(Slice(dest, MaxRoomBytes, 4), Offset);
            W16(Slice(dest, MaxRoomBytes + 4, 2), Count);
        }
        [[nodiscard]] static MapWantPacket Read(std::span<const std::uint8_t> src)
        {
            using namespace Protocol::Wire;
            MapWantPacket packet;
            packet.RoomKey = ChatPacket::ReadAscii(Slice(src, 0, MaxRoomBytes));
            packet.Offset = R32(Slice(src, MaxRoomBytes, 4));
            packet.Count = R16(Slice(src, MaxRoomBytes + 4, 2));
            return packet;
        }
    };

    struct MapOfferPacket
    {
        static constexpr std::int32_t MaxRoomBytes = VotePacket::MaxRoomBytes;
        static constexpr std::int32_t Size = MaxRoomBytes + 4 + 32;

        std::optional<std::string> RoomKey{};
        std::uint32_t Bytes = 0;
        std::array<std::uint8_t, 32> Sha256{};

        void Write(std::span<std::uint8_t> dest) const
        {
            using namespace Protocol::Wire;
            Clear(Slice(dest, 0, Size));
            ChatPacket::WriteAscii(Slice(dest, 0, MaxRoomBytes), RoomKey);
            W32(Slice(dest, MaxRoomBytes, 4), Bytes);
            std::copy(Sha256.begin(), Sha256.end(), dest.begin() + MaxRoomBytes + 4);
        }
        [[nodiscard]] static MapOfferPacket Read(std::span<const std::uint8_t> src)
        {
            using namespace Protocol::Wire;
            MapOfferPacket packet;
            packet.RoomKey = ChatPacket::ReadAscii(Slice(src, 0, MaxRoomBytes));
            packet.Bytes = R32(Slice(src, MaxRoomBytes, 4));
            const auto hash = Slice(src, MaxRoomBytes + 4, 32);
            std::copy(hash.begin(), hash.end(), packet.Sha256.begin());
            return packet;
        }
    };

    struct MapChunkPacket
    {
        static constexpr std::int32_t ChunkBytes = 1000;
        static constexpr std::int32_t HeaderSize = 4 + 2;

        std::uint32_t Offset = 0;
        std::span<const std::uint8_t> Data{};

        // The bytes written.
        [[nodiscard]] std::size_t Write(std::span<std::uint8_t> dest) const
        {
            using namespace Protocol::Wire;
            W32(Slice(dest, 0, 4), Offset);
            W16(Slice(dest, 4, 2), static_cast<std::uint16_t>(Data.size()));
            std::copy(Data.begin(), Data.end(), Slice(dest, HeaderSize, Data.size()).begin());
            return HeaderSize + Data.size();
        }
        // False when the packet is shorter than it says.
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, MapChunkPacket& out)
        {
            using namespace Protocol::Wire;
            if (src.size() < static_cast<std::size_t>(HeaderSize))
            {
                return false;
            }
            const std::uint16_t length = R16(Slice(src, 4, 2));
            if (src.size() < HeaderSize + static_cast<std::size_t>(length))
            {
                return false;
            }
            out.Offset = R32(Slice(src, 0, 4));
            out.Data = src.subspan(HeaderSize, length);
            return true;
        }
    };
}
