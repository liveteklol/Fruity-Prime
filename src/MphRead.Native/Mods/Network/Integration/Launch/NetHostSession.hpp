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
    namespace Mods::Diagnostics { class HostedAuthorityCheck; }

    namespace Mods::Network
    {
        class DedicatedServerProcess;

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
            friend class Mods::Diagnostics::HostedAuthorityCheck;
            static std::shared_ptr<DedicatedServerProcess> _server;
            static std::optional<std::string> _lastError;
            static std::mutex _lastErrorMutex;

            static void SetLastError(std::optional<std::string> value);
        };
    }
}
