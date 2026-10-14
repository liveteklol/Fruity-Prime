#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace MphRead
{
    class EquipInfo;
    class WeaponInfo;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Network
{
    // Which copy of a shot a machine is holding.
    enum class HomingShotRole : std::uint8_t
    {
        Offline,
        Owner,
        Authority,
        Observer
    };

    [[nodiscard]] std::string ToString(HomingShotRole value);

    // What one machine decided about one projectile as it spawned: the
    // weapon row, the charge, the steering strength and the target it took.
    // The same shot is keyed the same way on every machine (owner slot,
    // generation, life, sequence), so records from several logs line up.
    struct HomingShotRecord final
    {
        HomingShotRole Role = HomingShotRole::Offline;
        EntityType OwnerType{};
        std::int32_t OwnerSlot = -1;
        std::uint16_t Generation = 0;
        std::uint16_t LifeId = 0;
        std::uint32_t Sequence = 0;
        std::uint32_t Frame = 0;
        // The shooter's frame this copy fired in: its own frame on the
        // shooter's machine, the frame of the intent that drove the copy
        // anywhere else. What a continuous weapon's ticks are matched by.
        std::uint32_t IntentFrame = 0;
        std::int32_t Projectile = 0;
        BeamType Beam{};
        std::string WeaponRow{};
        std::uint16_t Charge = 0;
        bool Charged = false;
        bool PrimeHunter = false;
        // Per-tick raw values as the weapon table states them (x/4096).
        float HomingRaw = 0.0F;
        float SpeedRaw = 0.0F;
        OpenTK::Mathematics::Vector3 RawOrigin{};
        OpenTK::Mathematics::Vector3 RawRay{};
        OpenTK::Mathematics::Vector3 Velocity{};
        float RangeLimit = 0.0F;
        float CosLimit = 0.0F;
        float TargetDistance = 0.0F;
        float TargetCos = 0.0F;
        bool HasTargetGeometry = false;
        bool Homing = false;
        bool Continuous = false;
        bool MuzzleBlocked = false;
        IntentPacket::ShotTargetReference Target{};
        // How this copy came by its target (HomingTargetAcquisition).
        std::string Acquisition{};
        bool Presentation = false;
        // Degrees between the drawn launch direction and the physical one.
        float RetargetDegrees = 0.0F;
    };

    struct HomingClaimRecord final
    {
        std::int32_t ShooterSlot = -1;
        std::uint16_t ClaimId = 0;
        std::uint32_t Sequence = 0;
        std::uint8_t Beam = 0;
        std::uint8_t VictimSlot = 0;
        std::string WeaponRow{};
        bool Straight = false;
    };

    struct HomingVerdictRecord final
    {
        std::int32_t ShooterSlot = -1;
        std::uint16_t ClaimId = 0;
        std::uint8_t Result = 0;
    };

    // Health a Shock Coil hit gave back to its shooter, on the machine that
    // credited it: the shooter's own prediction or the authority's claim.
    struct HomingDrainRecord final
    {
        HomingShotRole Role = HomingShotRole::Offline;
        std::int32_t ShooterSlot = -1;
        std::int32_t VictimSlot = -1;
        std::uint16_t ClaimId = 0;
        std::int32_t Amount = 0;
        std::string WeaponRow{};
    };

    class HomingShotTrace final
    {
    public:
        HomingShotTrace() = delete;

        using SpawnSink = std::function<void(const HomingShotRecord&, const Entities::BeamProjectileEntity&)>;

        // -homingtrace: every record also goes to the debug log as one line.
        static void Enable(bool value) noexcept;
        [[nodiscard]] static bool Enabled() noexcept;
        // An in-process check's own capture; empty to remove it.
        static void SetSpawnSink(SpawnSink sink);

        static void Spawned(const Entities::BeamProjectileEntity& beam,
            const EquipInfo& equip, std::int32_t projectile, bool muzzleBlocked = false);
        static void Claimed(const HomingClaimRecord& record);
        static void Answered(const HomingVerdictRecord& record);
        static void Drained(const HomingDrainRecord& record);

        // "MP:11", "Enemy:2", "Boss:3", ... or "?" for a row no table holds.
        [[nodiscard]] static std::string WeaponRow(const WeaponInfo* weapon);
        [[nodiscard]] static std::string Describe(const IntentPacket::ShotTargetReference& target);
        [[nodiscard]] static std::string Format(const HomingShotRecord& record);

    private:
        [[nodiscard]] static bool Capturing() noexcept;
        static void Line(const std::string& text);

        inline static bool _enabled = false;
        inline static SpawnSink _spawnSink{};
    };
}
