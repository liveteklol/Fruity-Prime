#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Domain/ShotKey.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // Owns per-shooter hit-claim deduplication and remembered verdicts. Its
    // sequence window and shot metadata are specific to HitClaim packets.
    class HitClaimReplayLedger final
    {
    public:
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t Capacity = 128;

        [[nodiscard]] bool Seen(std::int32_t slot, std::uint16_t id) const noexcept;
        [[nodiscard]] std::optional<std::uint8_t> Result(std::int32_t slot, std::uint16_t id) const noexcept;
        [[nodiscard]] ::MphRead::BeamType Beam(std::int32_t slot, std::uint16_t id) const noexcept;
        [[nodiscard]] ShotKey Key(std::int32_t slot, std::uint16_t id) const noexcept;
        void Begin(std::int32_t slot, std::uint16_t id, std::uint8_t result,
            ::MphRead::BeamType beam, ShotKey key) noexcept;
        void Remember(std::int32_t slot, std::uint16_t id, std::uint8_t result) noexcept;
        void ForgetSlot(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] static bool Newer(std::uint16_t value, std::uint16_t previous) noexcept;

        std::array<std::uint16_t, SlotCapacity> _newestId{};
        std::array<std::array<std::uint16_t, Capacity>, SlotCapacity> _ids{};
        std::array<std::array<std::uint8_t, Capacity>, SlotCapacity> _results{};
        std::array<std::array<::MphRead::BeamType, Capacity>, SlotCapacity> _beams{};
        std::array<std::array<ShotKey, Capacity>, SlotCapacity> _keys{};
    };
}
