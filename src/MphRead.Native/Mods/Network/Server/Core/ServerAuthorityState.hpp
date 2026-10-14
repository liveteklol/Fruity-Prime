#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"

#include <cstdint>
#include <memory>

namespace MphRead::Mods::Network
{
    // Fences server packets; no peer may acquire simulation authority.
    class ServerAuthorityState final
    {
    public:
        using PeerHandle = ServerPeerRegistry::PeerHandle;

        explicit ServerAuthorityState(std::uint64_t initialEpoch = 0) noexcept;

        [[nodiscard]] bool HasAuthority() const noexcept;
        [[nodiscard]] bool IsAuthority(const PeerHandle& peer) const noexcept;
        [[nodiscard]] const PeerHandle& Current() const noexcept;
        [[nodiscard]] std::uint64_t Epoch() const noexcept;

        // Legacy grants are rejected without changing the server epoch.
        [[nodiscard]] bool TryClaim(const PeerHandle& peer) noexcept;

        // Legacy migration is ignored.
        void TransferTo(const PeerHandle& peer) noexcept;

    private:
        PeerHandle _current{};
        std::uint64_t _epoch = 0;
    };
}
