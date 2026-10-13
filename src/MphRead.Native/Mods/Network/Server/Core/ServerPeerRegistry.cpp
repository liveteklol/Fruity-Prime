#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"

#include "NativeRuntime/System/Net.hpp"

#include <algorithm>
#include <utility>

namespace MphRead::Mods::Network
{
    void ServerPeerRegistry::Add(PeerHandle peer)
    {
        if (peer == nullptr)
        {
            return;
        }
        _peers.push_back(std::move(peer));
        UpdateCount();
    }

    bool ServerPeerRegistry::Remove(const PeerHandle& peer)
    {
        const auto found = std::find(_peers.begin(), _peers.end(), peer);
        if (found == _peers.end())
        {
            return false;
        }
        _peers.erase(found);
        UpdateCount();
        return true;
    }

    ServerPeerRegistry::PeerHandle ServerPeerRegistry::Find(
        const std::shared_ptr<System::Net::IPEndPoint>& endPoint) const
    {
        for (const PeerHandle& peer : _peers)
        {
            if (peer->EndPoint != nullptr && endPoint != nullptr && peer->EndPoint->Equals(*endPoint))
            {
                return peer;
            }
        }
        return nullptr;
    }

    ServerPeerRegistry::PeerHandle ServerPeerRegistry::FindByClientId(std::uint32_t clientId) const
    {
        if (clientId == 0)
        {
            return nullptr;
        }
        const auto found = std::find_if(_peers.begin(), _peers.end(),
            [clientId](const PeerHandle& peer) { return peer->ClientId == clientId; });
        return found == _peers.end() ? nullptr : *found;
    }
}
