#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace MphRead::Mods::Network
{
    // Buffers and emits bounded verdict batches independently of claim replay
    // policy. The writer is installed by the session integration layer.
    class HitVerdictOutbox final
    {
    public:
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t BatchCapacity = HitVerdictPacket::MaxPerPacket;

        using Entry = std::pair<std::uint16_t, std::uint8_t>;
        using Writer = std::function<void(std::int32_t slot, std::span<const Entry> verdicts)>;

        [[nodiscard]] bool Enqueue(std::int32_t slot, std::uint16_t id, std::uint8_t result) noexcept;
        void Flush();
        void Clear() noexcept;
        void ClearSlot(std::int32_t slot) noexcept;

        [[nodiscard]] std::size_t PendingCount(std::int32_t slot) const noexcept;
        [[nodiscard]] const Writer& Sink() const noexcept { return _sink; }
        void Sink(Writer value) { _sink = std::move(value); }

    private:
        std::array<std::array<Entry, BatchCapacity>, SlotCapacity> _entries{};
        std::array<std::size_t, SlotCapacity> _counts{};
        Writer _sink;
    };
}
