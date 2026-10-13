#pragma once

#include "Formats/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace MphRead
{
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    class ServerSimCheck final
    {
    public:
        ServerSimCheck() = delete;
        ServerSimCheck(const ServerSimCheck&) = delete;
        ServerSimCheck(ServerSimCheck&&) = delete;
        ServerSimCheck& operator=(const ServerSimCheck&) = delete;
        ServerSimCheck& operator=(ServerSimCheck&&) = delete;

        [[nodiscard]] static std::int32_t Run(
            const std::string& room, std::int32_t players, double seconds, MphRead::GameMode mode,
            bool formCheck = false);

    private:
        class IntentDriver final
        {
        public:
            explicit IntentDriver(std::int32_t players);
            IntentDriver(const IntentDriver&) = delete;
            IntentDriver(IntentDriver&&) = delete;
            IntentDriver& operator=(const IntentDriver&) = delete;
            IntentDriver& operator=(IntentDriver&&) = delete;

            void Feed(std::uint32_t frame);

        private:
            const std::int32_t _players;
            std::vector<::OpenTK::Mathematics::Vector3> _at;
        };

        [[nodiscard]] static bool CheckStalledUnmorph();
        static void ApplyRoster(std::int32_t players);
        [[nodiscard]] static std::int64_t WorkingSetBytes();
        [[nodiscard]] static std::int64_t PeakWorkingSetBytes();
        [[nodiscard]] static std::string Mb(std::int64_t bytes);
    };
}
