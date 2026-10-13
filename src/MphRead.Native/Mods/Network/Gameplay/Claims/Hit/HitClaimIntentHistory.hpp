#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // A ray a shooter's intent reported for a shot.
    struct ClaimShotRay final
    {
        std::uint32_t Ack = 0;
        OpenTK::Mathematics::Vector3 Origin{};
        OpenTK::Mathematics::Vector3 Direction{};
    };

    // Retains the bounded intent evidence used to compare a hit claim with the
    // world the shooter acknowledged and the ray their shot followed.
    class HitClaimIntentHistory final
    {
    public:
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t ShotRayCapacity = 32;
        static constexpr std::size_t AckFrameCapacity = 64;

        void Record(std::int32_t slot, const IntentPacket& intent) noexcept;
        [[nodiscard]] bool HasSlot(std::int32_t slot) const noexcept;
        [[nodiscard]] std::uint32_t LatestAck(std::int32_t slot) const noexcept;
        [[nodiscard]] std::optional<std::uint32_t> AckForFrame(
            std::int32_t slot, std::uint32_t frame) const noexcept;
        [[nodiscard]] const ClaimShotRay* NearestRay(
            std::int32_t slot, std::uint32_t launch, std::uint32_t maxGapExclusive) const noexcept;

        void Reset() noexcept;

    private:
        struct AckRecord final
        {
            std::uint32_t IntentFrame = 0;
            std::uint32_t AckFrame = 0;
        };

        std::array<std::array<ClaimShotRay, ShotRayCapacity>, SlotCapacity> _shotRays{};
        std::array<std::size_t, SlotCapacity> _shotRayNext{};
        std::array<std::uint32_t, SlotCapacity> _latestAck{};
        std::array<std::array<AckRecord, AckFrameCapacity>, SlotCapacity> _ackFrames{};
    };
}
