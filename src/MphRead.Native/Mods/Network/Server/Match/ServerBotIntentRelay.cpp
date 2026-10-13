#include "Mods/Network/Server/Match/ServerBotIntentRelay.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <memory>
#include <span>

namespace MphRead::Mods::Network
{
    void ServerBotIntentRelay::RelayFrame(std::uint32_t frame, std::uint16_t matchId,
        std::uint64_t authorityEpoch, std::int32_t maxPlayers, NetTransport* transport,
        const ServerPeerRegistry& peers)
    {
        if (ServerBots::Count() == 0 || transport == nullptr || peers.All().empty() || frame == _lastFrame)
        {
            return;
        }
        _lastFrame = frame;

        const auto& players = Entities::PlayerEntity::Players();
        for (std::int32_t slot = 0; slot < maxPlayers && static_cast<std::size_t>(slot) < players.size(); slot++)
        {
            const std::shared_ptr<Entities::PlayerEntity>& player = players[static_cast<std::size_t>(slot)];
            if (!ServerBots::Holds(slot) || player == nullptr
                || !NativeRuntime::HasFlag(player->LoadFlags(), Entities::LoadFlags::Active))
            {
                continue;
            }

            const IntentPacket intent = ServerBots::IntentFor(*player, frame, matchId, authorityEpoch);
            _scratch[0] = static_cast<std::uint8_t>(slot);
            intent.Write(std::span<std::uint8_t>(_scratch).subspan(1));
            const std::size_t length = 1 + intent.WireSize();
            for (const ServerPeerRegistry::PeerHandle& peer : peers.All())
            {
                transport->Send(peer->EndPoint, PacketType::SlotIntent,
                    std::span<const std::uint8_t>(_scratch).first(length));
            }
        }
    }
}
