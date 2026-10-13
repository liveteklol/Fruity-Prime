#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"

#include <cstdint>
#include <memory>

namespace MphRead::Mods::Network
{
    // Owns the selected client authority and the epoch that fences its packets.
    class ServerAuthorityState final
    {
    public:
        using PeerHandle = ServerPeerRegistry::PeerHandle;

        explicit ServerAuthorityState(std::uint64_t initialEpoch = 0) noexcept;

        [[nodiscard]] bool HasAuthority() const noexcept;
        [[nodiscard]] bool IsAuthority(const PeerHandle& peer) const noexcept;
        [[nodiscard]] const PeerHandle& Current() const noexcept;
        [[nodiscard]] std::uint64_t Epoch() const noexcept;

        // The first client authority advances the epoch exactly once.
        [[nodiscard]] bool TryClaim(const PeerHandle& peer) noexcept;

        // Authority removal advances the epoch even when there is no successor.
        void TransferTo(const PeerHandle& peer) noexcept;

    private:
        PeerHandle _current{};
        std::uint64_t _epoch = 0;
    };
}
