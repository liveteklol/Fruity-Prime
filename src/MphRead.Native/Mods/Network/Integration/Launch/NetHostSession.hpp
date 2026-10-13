#pragma once

#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Enums.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>

namespace MphRead
{
    enum class GameMode : std::uint8_t;

    namespace Mods::Network
    {
        class DedicatedServer;

        class NetHostSession final
        {
        public:
            NetHostSession() = delete;

            [[nodiscard]] static bool Running() noexcept;
            [[nodiscard]] static std::optional<std::string> LastError();

            [[nodiscard]] static bool StartAndJoin(std::int32_t port,
                const std::string& playerName, Hunter hunter,
                const std::string& roomKey, GameMode mode, float timeLimit,
                std::int32_t pointGoal,
                std::int32_t maxPlayers = Entities::PlayerEntity::SlotCapacity,
                std::optional<std::tuple<std::string, std::int32_t, std::string>> listing
                    = std::nullopt);

            static void Stop();

        private:
            static std::shared_ptr<DedicatedServer> _server;
            static std::shared_ptr<std::stop_source> _cancel;
            static std::shared_ptr<std::thread> _thread;
            static std::optional<std::string> _lastError;
            static std::mutex _lastErrorMutex;

            static void SetLastError(std::optional<std::string> value);
        };
    }
}
