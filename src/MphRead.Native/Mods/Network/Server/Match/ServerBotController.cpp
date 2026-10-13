#include "Mods/Network/Server/Match/ServerBotController.hpp"

#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <string>

namespace MphRead::Mods::Network
{
    ServerBotController::ServerBotController(Ports ports) noexcept
        : _ports(ports)
    {
    }

    void ServerBotController::Log(const std::string& message) const
    {
        if (_ports.WriteLog != nullptr)
        {
            _ports.WriteLog(_ports.Context, message);
        }
    }

    void ServerBotController::RequestConfiguration(std::int32_t count, std::int32_t level)
    {
        if (_ports.Running->load())
        {
            const std::scoped_lock lock(_configurationMutex);
            _pendingConfiguration = Configuration{count, level};
            return;
        }
        if (const std::optional<std::string> configured = Configure(
                count, level, _ports.MaxPlayers, *_ports.Slots); configured.has_value())
        {
            Log(*configured);
        }
    }

    void ServerBotController::ApplyPendingConfiguration()
    {
        std::optional<Configuration> pending;
        {
            const std::scoped_lock lock(_configurationMutex);
            pending.swap(_pendingConfiguration);
        }
        if (!pending.has_value())
        {
            return;
        }
        if (_ports.Lobby->Phase() != SessionPhase::Lobby || _ports.Simulation->HasSimulation())
        {
            Log("bot configuration rejected outside the idle lobby");
            return;
        }
        std::string result;
        if (!Reconfigure(pending->Count, pending->Level, _ports.MaxPlayers,
                *_ports.Peers, *_ports.Slots, result))
        {
            Log("bot configuration rejected: " + result);
            return;
        }
        Log(result);
        _ports.LobbySession->TouchRevision("bot slots changed");
    }

    std::optional<std::string> ServerBotController::Configure(
        std::int32_t count, std::int32_t level, std::int32_t maxPlayers,
        ServerSlotIdentity& slots)
    {
        ServerBots::Configure(count, level, maxPlayers);
        for (std::int32_t slot = 0; slot < maxPlayers; ++slot)
        {
            if (ServerBots::Holds(slot))
            {
                slots.EnsureGeneration(static_cast<std::size_t>(slot));
            }
        }
        if (ServerBots::Count() == 0)
        {
            return std::nullopt;
        }
        return std::to_string(ServerBots::Count()) + " bot(s) at level "
            + std::to_string(ServerBots::Level()) + " in slots "
            + std::to_string(maxPlayers - ServerBots::Count()) + "-"
            + std::to_string(maxPlayers - 1);
    }

    bool ServerBotController::Reconfigure(std::int32_t count, std::int32_t level,
        std::int32_t maxPlayers, const ServerPeerRegistry& peers,
        ServerSlotIdentity& slots, std::string& result)
    {
        const std::int32_t boundedPlayers = std::clamp(maxPlayers, 0,
            static_cast<std::int32_t>(ServerSlotIdentity::SlotCapacity));
        const std::int32_t boundedCount = std::clamp(count, 0, std::max(0, boundedPlayers - 1));
        const std::int32_t firstBotSlot = boundedPlayers - boundedCount;
        for (const ServerPeerRegistry::PeerHandle& peer : peers.All())
        {
            if (peer->SlotIndex >= firstBotSlot)
            {
                result = "cannot reserve bot slot " + std::to_string(peer->SlotIndex)
                    + ": occupied by a human peer";
                return false;
            }
        }

        std::array<bool, ServerSlotIdentity::SlotCapacity> previouslyReserved{};
        for (std::size_t slot = 0; slot < previouslyReserved.size(); ++slot)
        {
            previouslyReserved[slot] = ServerBots::Holds(static_cast<std::int32_t>(slot));
        }
        std::array<std::uint16_t, ServerSlotIdentity::SlotCapacity> previousGenerations{};
        for (std::size_t slot = 0; slot < previousGenerations.size(); ++slot)
        {
            previousGenerations[slot] = slots.Generation(slot);
        }
        const std::optional<std::string> description = Configure(
            boundedCount, level, boundedPlayers, slots);
        for (std::size_t slot = 0; slot < previouslyReserved.size(); ++slot)
        {
            if (previouslyReserved[slot] != ServerBots::Holds(static_cast<std::int32_t>(slot)))
            {
                if (previousGenerations[slot] != 0 || !ServerBots::Holds(static_cast<std::int32_t>(slot)))
                {
                    static_cast<void>(slots.AdvanceGeneration(slot));
                }
            }
        }
        result = description.value_or("0 server bot slots reserved");
        return true;
    }

    bool ServerBotController::Holds(std::int32_t slot) noexcept
    {
        return ServerBots::Holds(slot);
    }
}
