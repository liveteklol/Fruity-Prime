#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MphRead
{
    enum class Hunter : std::uint8_t;
    class Scene;

    namespace Entities
    {
        enum class LoadFlags : std::uint8_t;
        class PlayerEntity;
    }
}

namespace MphRead::Mods::Network
{
    class NetRoomChange final
    {
    public:
        NetRoomChange() = delete;
        NetRoomChange(const NetRoomChange&) = delete;
        NetRoomChange(NetRoomChange&&) = delete;
        NetRoomChange& operator=(const NetRoomChange&) = delete;
        NetRoomChange& operator=(NetRoomChange&&) = delete;

        [[nodiscard]] static bool GameplayReady();
        [[nodiscard]] static bool Settling();
        [[nodiscard]] static std::int32_t RoomPlayerCount();
        [[nodiscard]] static bool Rebuilding();

        static void Reset();
        static void Sync(Scene& scene);
        [[nodiscard]] static std::shared_ptr<Entities::PlayerEntity> RebuildPlayers(
            Scene& scene, Hunter hunter, std::int32_t recolor);
        static void AfterRebuild(Scene& scene);

    private:
        static constexpr std::uint32_t SettleFrames = 60U;

        static void ReloadIntroCamSeq(Scene& scene);
        static void ResetScores();

        static std::string _requested;
        static std::uint32_t _requestedFrame;
        // The map pack being fetched for a room this device lacks: which, and
        // 0 while it comes, 1 once on disk, 2 if it could not be had.
        static std::string _fetchWanted;
        static std::shared_ptr<std::atomic<int>> _fetchState;
        static bool _loadPending;
        static std::uint16_t _requestedMatch;
        static std::uint16_t _loadedMatch;
        static std::uint32_t _loadedFrame;
    };
}
