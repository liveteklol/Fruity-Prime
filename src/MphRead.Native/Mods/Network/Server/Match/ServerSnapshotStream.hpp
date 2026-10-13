#pragma once

#include "Mods/Network/Protocol/Packets/PacketTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    // Owns the latest snapshot bytes and the authority client's frame gate.
    // Fixed storage keeps snapshot forwarding off the heap on every frame.
    class ServerSnapshotStream final
    {
    public:
        [[nodiscard]] bool HasLatest() const noexcept { return _latestSize != 0; }
        [[nodiscard]] std::span<const std::uint8_t> Latest() const noexcept
        {
            return std::span<const std::uint8_t>(_latest).first(_latestSize);
        }
        [[nodiscard]] bool CanAcceptIncoming(std::uint32_t frame) const noexcept;
        [[nodiscard]] bool CacheLatest(std::span<const std::uint8_t> payload) noexcept;
        [[nodiscard]] bool AcceptIncoming(std::uint32_t frame,
            std::span<const std::uint8_t> payload) noexcept;
        void ResetIncomingSequence() noexcept;
        void ClearLatest() noexcept { _latestSize = 0; }

    private:
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _latest{};
        std::size_t _latestSize = 0;
        std::uint32_t _incomingFrame = 0;
        bool _incomingSeen = false;
    };
}
