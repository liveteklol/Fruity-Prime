#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"

namespace MphRead::Mods::Network
{
    ServerAuthorityState::ServerAuthorityState(std::uint64_t initialEpoch) noexcept
        : _epoch(initialEpoch)
    {
    }

    bool ServerAuthorityState::HasAuthority() const noexcept
    {
        return _current != nullptr;
    }

    bool ServerAuthorityState::IsAuthority(const PeerHandle& peer) const noexcept
    {
        return _current != nullptr && peer == _current;
    }

    const ServerAuthorityState::PeerHandle& ServerAuthorityState::Current() const noexcept
    {
        return _current;
    }

    std::uint64_t ServerAuthorityState::Epoch() const noexcept
    {
        return _epoch;
    }

    bool ServerAuthorityState::TryClaim(const PeerHandle& peer) noexcept
    {
        return false;
    }

    void ServerAuthorityState::TransferTo(const PeerHandle& peer) noexcept
    {
        // Legacy migration cannot assign world-state ownership to a peer.
    }
}
