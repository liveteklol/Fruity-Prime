#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace System::Net
{
    class IPEndPoint;
}

namespace MphRead::Mods::Network
{
    // Owns endpoint-to-peer membership, slot allocation and host generations.
    class PeerRegistry final
    {
    public:
        static constexpr std::size_t SlotCapacity = static_cast<std::size_t>(RosterPacket::MaxSlots);

        class Peer final
        {
        public:
            [[nodiscard]] const std::shared_ptr<System::Net::IPEndPoint>& Endpoint() const noexcept;
            [[nodiscard]] std::int32_t SlotIndex() const noexcept;
            [[nodiscard]] const IntentPacket& LatestIntent() const noexcept;
            [[nodiscard]] std::uint32_t LastIntentFrame() const noexcept;
            [[nodiscard]] double LastSeenTime() const noexcept;
            [[nodiscard]] std::uint32_t ClientId() const noexcept;

            void Rebind(std::shared_ptr<System::Net::IPEndPoint> endpoint) noexcept;
            void ObserveClient(std::uint32_t clientId, double time) noexcept;
            void MarkSeen(double time) noexcept;
            void RecordLatestIntent(const IntentPacket& intent, double time) noexcept;
            void ResetIntentFrame() noexcept;

        private:
            friend class PeerRegistry;
            Peer(std::shared_ptr<System::Net::IPEndPoint> endpoint, std::int32_t slotIndex) noexcept;

            std::shared_ptr<System::Net::IPEndPoint> _endpoint{};
            std::int32_t _slotIndex = -1;
            IntentPacket _latestIntent{};
            std::uint32_t _lastIntentFrame = 0;
            double _lastSeenTime = 0.0;
            std::uint32_t _clientId = 0;
        };

        using PeerHandle = std::shared_ptr<Peer>;

        [[nodiscard]] const std::vector<PeerHandle>& Peers() const noexcept;
        [[nodiscard]] std::size_t Count() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] PeerHandle At(std::size_t index) const;
        [[nodiscard]] PeerHandle FindByEndpoint(
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint) const;
        [[nodiscard]] PeerHandle FindByClientId(std::uint32_t clientId) const;
        [[nodiscard]] PeerHandle AddPeer(
            std::shared_ptr<System::Net::IPEndPoint> endpoint, std::int32_t slotIndex,
            std::uint32_t clientId, double time);
        [[nodiscard]] bool Remove(const PeerHandle& peer) noexcept;
        void Clear() noexcept;
        void ResetHostGenerations() noexcept;
        [[nodiscard]] std::uint16_t HostGeneration(std::size_t slot) const;
        void SetHostGeneration(std::size_t slot, std::uint16_t generation);
        void ResetIntentFrameForSlot(std::int32_t slot) noexcept;
        [[nodiscard]] std::int32_t NextFreeSlot(std::int32_t first, std::int32_t limit) const noexcept;

        [[nodiscard]] std::int32_t FindSlotByEndpoint(
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint) const;
        [[nodiscard]] std::shared_ptr<System::Net::IPEndPoint> FindEndpointBySlot(std::int32_t slot) const;

    private:
        std::vector<PeerHandle> _peers{};
        std::array<std::uint16_t, SlotCapacity> _hostGenerations{};
    };

    [[nodiscard]] PeerRegistry& SessionPeers() noexcept;
}
