#pragma once

#include <cstdint>
#include <vector>

namespace MphRead::Mods::Network
{
    // Per-slot firing clocks for continuous player weapons.
    class ContinuousWeaponPhase final
    {
    public:
        static constexpr std::uint32_t MaxIntentAge = 30;

        explicit ContinuousWeaponPhase(std::int32_t slots);

        // DS fixed-point cadence: ammo rounds strictly above the boundary,
        // damage includes it. Both use the same logical firing phase.
        [[nodiscard]] static std::int32_t Amount(
            std::int32_t amount, std::uint64_t phase, bool damage) noexcept;

        void Reset();
        void ResetSlot(std::int32_t slot);

        // Called on every player simulation step, including steps without a
        // Spawn. A missing packet does not end a held stream; a release,
        // weapon change, or stale intent does. The next fresh shot then seeds
        // a new phase.
        void Observe(std::int32_t slot, std::uint64_t sceneFrame, bool continuousHeld, bool intentFresh);

        [[nodiscard]] std::uint64_t Resolve(std::int32_t slot, std::uint64_t sceneFrame,
            bool networked, bool localOwner, std::uint32_t netFrame, bool intentValid,
            std::uint32_t intentFrame, std::uint32_t intentAge, bool& shared);

    private:
        struct Clock final
        {
            std::uint64_t Phase = 0;
            std::uint64_t SceneFrame = 0;
            bool Valid = false;
        };

        static void Advance(Clock& clock, std::uint64_t sceneFrame) noexcept;

        std::vector<Clock> _clocks;
    };

    // The process-wide firing clock is owned by this weapon subsystem, not
    // by NetSession. Session lifecycle events explicitly reset its state.
    [[nodiscard]] ContinuousWeaponPhase& SharedContinuousWeaponPhase() noexcept;
}
