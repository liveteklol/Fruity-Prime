#include "Mods/Network/Session/PeerRegistry.hpp"

#include "NativeRuntime/System/Net.hpp"

#include <algorithm>
#include <utility>

namespace MphRead::Mods::Network
{
    PeerRegistry::Peer::Peer(std::shared_ptr<System::Net::IPEndPoint> endpoint,
        std::int32_t slotIndex) noexcept : _endpoint(std::move(endpoint)), _slotIndex(slotIndex)
    {
    }

    const std::shared_ptr<System::Net::IPEndPoint>& PeerRegistry::Peer::Endpoint() const noexcept
    {
        return _endpoint;
    }

    std::int32_t PeerRegistry::Peer::SlotIndex() const noexcept
    {
        return _slotIndex;
    }

    const IntentPacket& PeerRegistry::Peer::LatestIntent() const noexcept
    {
        return _latestIntent;
    }

    std::uint32_t PeerRegistry::Peer::LastIntentFrame() const noexcept
    {
        return _lastIntentFrame;
    }

    double PeerRegistry::Peer::LastSeenTime() const noexcept
    {
        return _lastSeenTime;
    }

    std::uint32_t PeerRegistry::Peer::ClientId() const noexcept
    {
        return _clientId;
    }

    void PeerRegistry::Peer::Rebind(std::shared_ptr<System::Net::IPEndPoint> endpoint) noexcept
    {
        _endpoint = std::move(endpoint);
    }

    void PeerRegistry::Peer::ObserveClient(std::uint32_t clientId, double time) noexcept
    {
        _clientId = clientId;
        _lastSeenTime = time;
    }

    void PeerRegistry::Peer::MarkSeen(double time) noexcept
    {
        _lastSeenTime = time;
    }

    void PeerRegistry::Peer::RecordLatestIntent(const IntentPacket& intent, double time) noexcept
    {
        _lastIntentFrame = intent.Frame;
        _latestIntent = intent;
        _lastSeenTime = time;
    }

    void PeerRegistry::Peer::ResetIntentFrame() noexcept
    {
        _lastIntentFrame = 0;
    }

    const std::vector<PeerRegistry::PeerHandle>& PeerRegistry::Peers() const noexcept
    {
        return _peers;
    }

    std::size_t PeerRegistry::Count() const noexcept
    {
        return _peers.size();
    }

    bool PeerRegistry::Empty() const noexcept
    {
        return _peers.empty();
    }

    PeerRegistry::PeerHandle PeerRegistry::At(std::size_t index) const
    {
        return _peers.at(index);
    }

    std::int32_t PeerRegistry::FindSlotByEndpoint(
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint) const
    {
        const PeerHandle peer = FindByEndpoint(endpoint);
        return peer == nullptr ? -1 : peer->SlotIndex();
    }

    std::shared_ptr<System::Net::IPEndPoint> PeerRegistry::FindEndpointBySlot(std::int32_t slot) const
    {
        const auto found = std::find_if(_peers.begin(), _peers.end(),
            [slot](const PeerHandle& peer) { return peer->SlotIndex() == slot; });
        return found == _peers.end() ? nullptr : (*found)->Endpoint();
    }

    PeerRegistry::PeerHandle PeerRegistry::FindByEndpoint(
        const std::shared_ptr<System::Net::IPEndPoint>& endpoint) const
    {
        const auto found = std::find_if(_peers.begin(), _peers.end(),
            [&endpoint](const PeerHandle& peer)
            {
                return peer->Endpoint() != nullptr && endpoint != nullptr
                    && peer->Endpoint()->Equals(*endpoint);
            });
        return found == _peers.end() ? nullptr : *found;
    }

    PeerRegistry::PeerHandle PeerRegistry::FindByClientId(std::uint32_t clientId) const
    {
        const auto found = std::find_if(_peers.begin(), _peers.end(),
            [clientId](const PeerHandle& peer) { return peer->ClientId() == clientId; });
        return found == _peers.end() ? nullptr : *found;
    }

    PeerRegistry::PeerHandle PeerRegistry::AddPeer(
        std::shared_ptr<System::Net::IPEndPoint> endpoint, std::int32_t slotIndex,
        std::uint32_t clientId, double time)
    {
        PeerHandle peer(new Peer(std::move(endpoint), slotIndex));
        peer->ObserveClient(clientId, time);
        _peers.push_back(peer);
        return peer;
    }

    bool PeerRegistry::Remove(const PeerHandle& peer) noexcept
    {
        const auto found = std::find(_peers.begin(), _peers.end(), peer);
        if (found == _peers.end())
        {
            return false;
        }
        _peers.erase(found);
        return true;
    }

    void PeerRegistry::Clear() noexcept
    {
        _peers.clear();
    }

    void PeerRegistry::ResetHostGenerations() noexcept
    {
        _hostGenerations.fill(0);
    }

    std::uint16_t PeerRegistry::HostGeneration(std::size_t slot) const
    {
        return _hostGenerations.at(slot);
    }

    void PeerRegistry::SetHostGeneration(std::size_t slot, std::uint16_t generation)
    {
        _hostGenerations.at(slot) = generation;
    }

    void PeerRegistry::ResetIntentFrameForSlot(std::int32_t slot) noexcept
    {
        for (const PeerHandle& peer : _peers)
        {
            if (peer->SlotIndex() == slot)
            {
                peer->ResetIntentFrame();
            }
        }
    }

    std::int32_t PeerRegistry::NextFreeSlot(std::int32_t first, std::int32_t limit) const noexcept
    {
        for (std::int32_t slot = first; slot < limit; ++slot)
        {
            const bool occupied = std::any_of(_peers.begin(), _peers.end(),
                [slot](const PeerHandle& peer) { return peer->SlotIndex() == slot; });
            if (!occupied)
            {
                return slot;
            }
        }
        return -1;
    }

    PeerRegistry& SessionPeers() noexcept
    {
        static PeerRegistry registry{};
        return registry;
    }
}
