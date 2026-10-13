#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    class ServerSlotIdentity;
    class ServerPeerRegistry;
    class ServerLobbyState;
    class ServerLobbySession;
    class ServerSimulationController;

    // Applies the dedicated server's bot roster and reserves generation-safe
    // slots before client admission begins.
    class ServerBotController final
    {
    public:
        struct Ports final
        {
            ServerPeerRegistry* Peers = nullptr;
            ServerSlotIdentity* Slots = nullptr;
            ServerLobbyState* Lobby = nullptr;
            ServerLobbySession* LobbySession = nullptr;
            ServerSimulationController* Simulation = nullptr;
            const std::atomic<bool>* Running = nullptr;
            std::int32_t MaxPlayers = 0;
            void* Context = nullptr;
            void (*WriteLog)(void*, const std::string&) = nullptr;
        };

        explicit ServerBotController(Ports ports) noexcept;

        void RequestConfiguration(std::int32_t count, std::int32_t level);
        void ApplyPendingConfiguration();

        [[nodiscard]] static std::optional<std::string> Configure(
            std::int32_t count, std::int32_t level, std::int32_t maxPlayers,
            ServerSlotIdentity& slots);
        [[nodiscard]] static bool Reconfigure(std::int32_t count, std::int32_t level,
            std::int32_t maxPlayers, const ServerPeerRegistry& peers,
            ServerSlotIdentity& slots, std::string& result);
        [[nodiscard]] static bool Holds(std::int32_t slot) noexcept;

    private:
        struct Configuration final
        {
            std::int32_t Count;
            std::int32_t Level;
        };

        void Log(const std::string& message) const;

        Ports _ports;
        std::mutex _configurationMutex{};
        std::optional<Configuration> _pendingConfiguration{};
    };
}
