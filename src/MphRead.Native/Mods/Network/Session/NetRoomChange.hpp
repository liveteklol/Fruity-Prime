#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

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
        static bool _loadPending;
        static std::uint16_t _requestedMatch;
        static std::uint16_t _loadedMatch;
        static std::uint32_t _loadedFrame;
    };
}
