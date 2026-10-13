#pragma once

#include "Mods/Network/Protocol/ConnectionSecret.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Server/Core/ServerPeerLiveness.hpp"
#include "Mods/Network/Server/Core/ServerPeerIntentState.hpp"
#include "Mods/Network/Server/Core/ServerPeerProfile.hpp"
#include "Mods/Network/Server/Lobby/LobbyCommandHistory.hpp"
#include "Mods/Network/Server/Lobby/PeerChatRateLimiter.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyPeerState.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace System::Net
{
    class IPEndPoint;
}

namespace MphRead::Mods::Network
{
    // A connected client and the per-connection state retained by the server.
    struct ServerPeer final
    {
        std::shared_ptr<System::Net::IPEndPoint> EndPoint{};
        std::int32_t SlotIndex = -1;
        ServerPeerLiveness Liveness{};
        ServerPeerIntentState Intent{};
        ServerPeerProfile Profile{};
        PeerChatRateLimiter ChatRate{};
        ServerLobbyPeerState Lobby{};
        LobbyCommandHistory LobbyCommands{};
        std::uint32_t ClientId = 0;
        ConnectionSecret Secret{};
    };

    // Owns connection membership and the server's peer-to-slot lookup policy.
    // Peer gameplay/lobby fields remain on each peer and are mutated by the
    // server's single-threaded packet loop.
    class ServerPeerRegistry final
    {
    public:
        using Peer = ServerPeer;
        using PeerHandle = std::shared_ptr<Peer>;

        [[nodiscard]] const std::vector<PeerHandle>& All() const noexcept { return _peers; }
        [[nodiscard]] std::int32_t Count() const noexcept { return _count.load(); }

        void Add(PeerHandle peer);
        [[nodiscard]] bool Remove(const PeerHandle& peer);
        [[nodiscard]] PeerHandle Find(const std::shared_ptr<System::Net::IPEndPoint>& endPoint) const;
        [[nodiscard]] PeerHandle FindByClientId(std::uint32_t clientId) const;

        template <typename IsReserved>
        [[nodiscard]] bool SlotFree(std::int32_t slot, IsReserved&& isReserved) const
        {
            if (slot < 0 || isReserved(slot))
            {
                return false;
            }
            for (const PeerHandle& peer : _peers)
            {
                if (peer->SlotIndex == slot)
                {
                    return false;
                }
            }
            return true;
        }

        template <typename IsReserved>
        [[nodiscard]] std::int32_t NextFreeSlot(std::int32_t capacity, IsReserved&& isReserved) const
        {
            for (std::int32_t slot = 0; slot < capacity; slot++)
            {
                if (SlotFree(slot, isReserved))
                {
                    return slot;
                }
            }
            return -1;
        }

        // Keep the returning client's slot when it is still available; otherwise
        // choose the first free slot. Bot reservations and current membership
        // are evaluated together so callers cannot accidentally prefer a slot
        // that belongs to either one.
        template <typename IsReserved>
        [[nodiscard]] std::int32_t SelectFreeSlot(
            std::int32_t preferredSlot, std::int32_t capacity, IsReserved&& isReserved) const
        {
            if (preferredSlot >= 0 && preferredSlot < capacity
                && SlotFree(preferredSlot, isReserved))
            {
                return preferredSlot;
            }
            return NextFreeSlot(capacity, isReserved);
        }

    private:
        void UpdateCount() noexcept { _count.store(static_cast<std::int32_t>(_peers.size())); }

        std::vector<PeerHandle> _peers{};
        std::atomic<std::int32_t> _count{0};
    };
}
