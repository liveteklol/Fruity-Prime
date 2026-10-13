#include "Mods/Network/Session/SessionMapUpload.hpp"
#include "Mods/Network/Session/NetRole.hpp"
#include "Mods/Network/Protocol/Packets/MapTransferPackets.hpp"

#include "Mods/MapGen/CustomRooms.hpp"
#include "Mods/MapGen/Q3Bsp.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Cryptography.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        std::mutex g_lock;

        [[nodiscard]] bool IsArchive(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".pk3" || extension == ".zip";
        }
    }

    SessionMapUpload::Held SessionMapUpload::Load(const std::string& roomKey)
    {
        Held held{};
        held.RoomKey = roomKey;
        const std::string directory = MapGen::CustomRooms::MapDirectory();
        if (directory.empty() || !std::filesystem::is_directory(Runtime::PathFromUtf8(directory)))
        {
            return held;
        }
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                 Runtime::PathFromUtf8(directory),
                 std::filesystem::directory_options::follow_directory_symlink
                     | std::filesystem::directory_options::skip_permission_denied,
                 error))
        {
            if (entry.is_directory(error) || !IsArchive(entry.path()))
            {
                continue;
            }
            const std::uintmax_t bytes = entry.file_size(error);
            if (error || bytes == 0 || bytes > MaxBytes)
            {
                continue;
            }
            try
            {
                const std::string path = Runtime::PathToUtf8(entry.path());
                bool holds = false;
                for (const std::string& level : MapGen::Q3Bsp::ListMaps(path))
                {
                    holds = holds || Runtime::StringEqualsOrdinalIgnoreCase(level, roomKey);
                }
                if (!holds)
                {
                    continue;
                }
                held.Data = Runtime::FileReadAllBytes(path);
                held.Sha256 = Runtime::Sha256HashData(held.Data);
                held.Found = true;
                return held;
            }
            catch (...)
            {
            }
        }
        return held;
    }

    void SessionMapUpload::Answer(SessionConnection& connection, NetRole role,
        std::span<const std::uint8_t> payload)
    {
        if (role != NetRole::Client || !connection.IsOpen() || !connection.HasHostEndpoint()
            || payload.size() < static_cast<std::size_t>(MapWantPacket::Size))
        {
            return;
        }
        const MapWantPacket want = MapWantPacket::Read(payload);
        const std::string roomKey = want.RoomKey.value_or("");
        if (roomKey.empty())
        {
            return;
        }
        static Held held;
        const std::lock_guard<std::mutex> guard(g_lock);
        // A request with no chunks begins a transfer: read the folder afresh,
        // since the pack may have changed since the last one for this level.
        if (want.Count == 0 || !Runtime::StringEqualsOrdinalIgnoreCase(held.RoomKey, roomKey))
        {
            held = Load(roomKey);
            Runtime::ConsoleWriteLine(held.Found
                ? "[net] the server asked for \"" + roomKey + "\": sending it ("
                    + std::to_string(held.Data.size() / 1024) + " KB)"
                : "[net] the server asked for \"" + roomKey + "\", which no pack here holds");
        }
        if (!held.Found)
        {
            return;
        }
        auto& buffer = connection.Buffer();
        if (want.Count == 0)
        {
            MapOfferPacket offer{};
            offer.RoomKey = roomKey;
            offer.Bytes = static_cast<std::uint32_t>(held.Data.size());
            offer.Sha256 = held.Sha256;
            offer.Write(buffer);
            connection.SendToHost(PacketType::MapOffer,
                std::span<const std::uint8_t>(buffer).first(MapOfferPacket::Size));
            return;
        }
        const std::size_t chunkBytes = static_cast<std::size_t>(MapChunkPacket::ChunkBytes);
        std::size_t offset = want.Offset - (want.Offset % chunkBytes);
        for (std::uint32_t i = 0; i < want.Count && offset < held.Data.size(); ++i, offset += chunkBytes)
        {
            MapChunkPacket chunk{};
            chunk.Offset = static_cast<std::uint32_t>(offset);
            chunk.Data = std::span<const std::uint8_t>(held.Data).subspan(
                offset, std::min(chunkBytes, held.Data.size() - offset));
            const std::size_t length = chunk.Write(buffer);
            connection.SendToHost(PacketType::MapChunk,
                std::span<const std::uint8_t>(buffer).first(length));
        }
    }
}
