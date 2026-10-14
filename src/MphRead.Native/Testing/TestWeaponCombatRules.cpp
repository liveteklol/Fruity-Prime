#include "Mods/Combat/WeaponCharge.hpp"
#include "Mods/Combat/WeaponVariant.hpp"
#include "Metadata/Weapons.hpp"
#include "Mods/Combat/HomingSteeringRules.hpp"
#include "Mods/Combat/ProjectileFlight.hpp"
#include "Mods/Diagnostics/HomingReferenceFlight.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetSelection.hpp"
#include "GameState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <optional>

namespace
{
    using namespace MphRead;
    using MphRead::Mods::Combat::HomingSteeringRules;
    using MphRead::Mods::Network::IntentPacket;

    bool Check(bool condition, const char* label)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << label << '\n';
        }
        return condition;
    }

    bool Near(float value, float expected)
    {
        return std::abs(value - expected) < 0.00001F;
    }

    std::array<std::int32_t, 3> HomingValues(
        const std::shared_ptr<const Weapons::WeaponList>& table, std::size_t index)
    {
        if (table == nullptr || index >= table->size() || (*table)[index] == nullptr)
        {
            return { -1, -1, -1 };
        }
        const WeaponInfo& weapon = *(*table)[index];
        return { weapon.UnchargedHoming, weapon.MinChargeHoming, weapon.ChargedHoming };
    }

    float HomingAmountAt(const std::shared_ptr<const Weapons::WeaponList>& table,
        std::size_t index, std::uint16_t chargeLevel)
    {
        if (table == nullptr || index >= table->size() || (*table)[index] == nullptr)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        const WeaponInfo& weapon = *(*table)[index];
        const Mods::Combat::WeaponChargeState charge = Mods::Combat::ResolveWeaponCharge(
            weapon.Flags, weapon.MinCharge, weapon.FullCharge, chargeLevel);
        return Mods::Combat::InterpolateWeaponChargeAmount(weapon.UnchargedHoming,
            weapon.MinChargeHoming, weapon.ChargedHoming, charge);
    }

    enum class TargetMotion
    {
        Moving,
        TurnsBehind
    };

    enum class TrajectoryEvent
    {
        None,
        Target,
        Wall
    };

    struct TrajectoryResult final
    {
        OpenTK::Mathematics::Vector3 Position{};
        OpenTK::Mathematics::Vector3 Velocity{};
        float TotalTurnRadians = 0.0F;
        float TargetHitTime = -1.0F;
        float WallHitTime = -1.0F;
        float TargetReleasedAt = -1.0F;
        float FirstEventTime = std::numeric_limits<float>::infinity();
        TrajectoryEvent FirstEvent = TrajectoryEvent::None;
    };

    OpenTK::Mathematics::Vector3 TargetAt(TargetMotion motion, float time,
        float targetX, float targetZ)
    {
        if (motion == TargetMotion::TurnsBehind && time >= 0.5F)
        {
            return OpenTK::Mathematics::Vector3(50.0F, 0.0F, -100.0F);
        }
        return OpenTK::Mathematics::Vector3(targetX + 0.2F * time, 0.0F, targetZ);
    }

    float SegmentDistanceSquared(OpenTK::Mathematics::Vector3 start,
        OpenTK::Mathematics::Vector3 end, OpenTK::Mathematics::Vector3 targetStart,
        OpenTK::Mathematics::Vector3 targetEnd, float& fraction)
    {
        using OpenTK::Mathematics::Vector3;
        const Vector3 relativeStart = start - targetStart;
        const Vector3 relativeDelta = (end - start) - (targetEnd - targetStart);
        const float lengthSquared = Vector3::Dot(relativeDelta, relativeDelta);
        fraction = lengthSquared > 0.0F
            ? std::clamp(-Vector3::Dot(relativeStart, relativeDelta) / lengthSquared, 0.0F, 1.0F)
            : 0.0F;
        const Vector3 closest = relativeStart + ::OpenTK::Mathematics::Scale(relativeDelta, fraction);
        return Vector3::Dot(closest, closest);
    }

    enum class Flight
    {
        Reference30,
        Native60
    };

    // One second of a homing shot, speeds in 30 Hz per-tick units. The
    // reference is the DS's 30 Hz flight; the native one is the game's 60 Hz
    // flight core and steering rule, the same calls the projectile makes.
    TrajectoryResult SimulateHoming(Flight flight, TargetMotion targetMotion,
        float initialSpeed, float finalSpeed, float decaySeconds, float maximumDelta = 0.2F,
        float targetX = 2.0F, float targetZ = 10.0F, float wallZ = -1.0F)
    {
        using OpenTK::Mathematics::Vector3;
        using MphRead::Mods::Combat::ProjectileFlight;
        using MphRead::Mods::Combat::ProjectileFlightParameters;
        using MphRead::Mods::Combat::ProjectileFlightState;
        using MphRead::Mods::Diagnostics::HomingReferenceFlight;
        TrajectoryResult result{};
        static_cast<void>(HomingSteeringRules::ConfigureFrequencyHz(HomingSteeringRules::DefaultFrequencyHz));
        const bool native = flight == Flight::Native60;
        const std::int32_t stepHz = native ? 60 : 30;
        const float frameTime = 1.0F / static_cast<float>(stepHz);

        HomingReferenceFlight::Spec spec{};
        spec.SpeedRaw = initialSpeed * 4096.0F;
        spec.FinalSpeedRaw = finalSpeed * 4096.0F;
        spec.DecayTicks = static_cast<std::int32_t>(std::lround(decaySeconds * 30.0F));
        spec.HomingRaw = maximumDelta * 4096.0F;
        HomingReferenceFlight reference(spec, Vector3::Zero, Vector3(0.0F, 0.0F, 1.0F));

        const ProjectileFlightParameters parameters{Vector3::Zero, decaySeconds, 0,
            initialSpeed / 2.0F, finalSpeed / 2.0F};
        ProjectileFlightState state{Vector3::Zero, Vector3(0.0F, 0.0F, initialSpeed / 2.0F), initialSpeed / 2.0F, 0};
        std::int32_t steeringPhase = 0;

        bool targetActive = true;
        for (std::int32_t frame = 1; frame <= stepHz; ++frame)
        {
            const float previousTime = static_cast<float>(frame - 1) * frameTime;
            const float currentTime = static_cast<float>(frame) * frameTime;
            const Vector3 previousPosition = native ? state.Position : reference.Position();
            if (native)
            {
                ProjectileFlight::Move(parameters, currentTime, state);
            }
            else
            {
                reference.Move();
            }
            const Vector3 position = native ? state.Position : reference.Position();
            const Vector3 velocity = native ? state.Velocity : reference.Velocity();
            float eventFraction = std::numeric_limits<float>::infinity();
            TrajectoryEvent event = TrajectoryEvent::None;
            if (targetActive)
            {
                float fraction = 0.0F;
                if (SegmentDistanceSquared(previousPosition, position,
                    TargetAt(targetMotion, previousTime, targetX, targetZ),
                    TargetAt(targetMotion, currentTime, targetX, targetZ), fraction)
                    <= 0.35F * 0.35F)
                {
                    eventFraction = fraction;
                    event = TrajectoryEvent::Target;
                }
            }
            if (wallZ > 0.0F && previousPosition.Z < wallZ && position.Z >= wallZ)
            {
                const float fraction = (wallZ - previousPosition.Z) / (position.Z - previousPosition.Z);
                if (fraction < eventFraction)
                {
                    eventFraction = fraction;
                    event = TrajectoryEvent::Wall;
                }
            }
            if (event != TrajectoryEvent::None)
            {
                result.FirstEventTime = previousTime + frameTime * eventFraction;
                result.FirstEvent = event;
                result.Position = previousPosition
                    + ::OpenTK::Mathematics::Scale(position - previousPosition, eventFraction);
                result.Velocity = native ? ::OpenTK::Mathematics::Scale(velocity, 2.0F) : velocity;
                if (event == TrajectoryEvent::Target)
                {
                    result.TargetHitTime = result.FirstEventTime;
                }
                else
                {
                    result.WallHitTime = result.FirstEventTime;
                }
                return result;
            }
            if (!targetActive)
            {
                continue;
            }
            const Vector3 target = TargetAt(targetMotion, currentTime, targetX, targetZ);
            const Vector3 before = velocity;
            Vector3 after = velocity;
            bool released = false;
            if (native)
            {
                if (HomingSteeringRules::Advance(steeringPhase))
                {
                    const auto steered = HomingSteeringRules::Step(
                        state.Position, target, state.Velocity, state.Speed, maximumDelta / 2.0F);
                    released = !steered.has_value();
                    if (steered.has_value())
                    {
                        state.Velocity = *steered;
                    }
                }
                after = state.Velocity;
            }
            else
            {
                released = reference.Steer(target) == HomingReferenceFlight::SteerResult::Released;
                after = reference.Velocity();
            }
            if (released)
            {
                targetActive = false;
                result.TargetReleasedAt = currentTime;
                continue;
            }
            const float beforeLengthSquared = Vector3::Dot(before, before);
            const float afterLengthSquared = Vector3::Dot(after, after);
            if (beforeLengthSquared > 0.0F && afterLengthSquared > 0.0F)
            {
                const float cosine = std::clamp(Vector3::Dot(before, after)
                    / std::sqrt(beforeLengthSquared * afterLengthSquared), -1.0F, 1.0F);
                result.TotalTurnRadians += std::acos(cosine);
            }
        }
        result.Position = native ? state.Position : reference.Position();
        result.Velocity = native ? ::OpenTK::Mathematics::Scale(state.Velocity, 2.0F) : reference.Velocity();
        return result;
    }
}

int main()
{
    using OpenTK::Mathematics::Vector3;
    bool passed = true;

    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(30), "the shared steering rate accepts 30 Hz");
    std::int32_t phase = 0;
    std::int32_t steps = 0;
    for (std::int32_t i = 0; i < 60; ++i)
    {
        steps += HomingSteeringRules::Advance(phase) ? 1 : 0;
    }
    passed &= Check(steps == 30 && phase == 0, "all homing shots steer at the shared 30 Hz cadence");

    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(15), "the shared steering rate accepts 15 Hz");
    phase = 0;
    steps = 0;
    for (std::int32_t i = 0; i < 60; ++i)
    {
        steps += HomingSteeringRules::Advance(phase) ? 1 : 0;
    }
    passed &= Check(steps == 15 && phase == 0, "one frequency change controls every steering schedule");
    passed &= Check(!HomingSteeringRules::ConfigureFrequencyHz(61)
        && HomingSteeringRules::FrequencyHz() == 15, "out-of-range rates leave the shared setting unchanged");
    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(30), "the default rate can be restored");

    const auto velocity = HomingSteeringRules::Step(
        Vector3::Zero, Vector3(10, 0, 0), Vector3(0, 1, 0), 1.0F, 0.5F);
    passed &= Check(velocity.has_value() && Near(velocity->X, 0.353553F)
        && Near(velocity->Y, 0.646447F), "steering clamps one velocity change to its configured bound");
    passed &= Check(!HomingSteeringRules::Step(
        Vector3::Zero, Vector3(10, 0, 0), Vector3(-1, 0, 0), 1.0F, 0.5F).has_value(),
        "a target behind the current travel direction is dropped");

    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(60)
        && Near(HomingSteeringRules::MaximumDeltaPerStep(0.5F), 0.25F),
        "60 Hz divides the shared per-second steering allowance across twice as many steps");
    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(15)
        && Near(HomingSteeringRules::MaximumDeltaPerStep(0.5F), 1.0F),
        "15 Hz scales the same steering allowance across fewer steps");
    passed &= Check(HomingSteeringRules::ConfigureFrequencyHz(0)
        && HomingSteeringRules::MaximumDeltaPerStep(0.5F) == 0.0F,
        "zero Hz disables ballistic steering through the shared setting");
    phase = 0;
    passed &= Check(!HomingSteeringRules::Advance(phase),
        "zero Hz leaves every projectile steering schedule idle");
    static_cast<void>(HomingSteeringRules::ConfigureFrequencyHz(HomingSteeringRules::DefaultFrequencyHz));

    const TrajectoryResult reference30 = SimulateHoming(Flight::Reference30, TargetMotion::Moving, 1.0F, 1.0F, 0.0F);
    const TrajectoryResult native60 = SimulateHoming(Flight::Native60, TargetMotion::Moving, 1.0F, 1.0F, 0.0F);
    const float referencePositionError = std::hypot(reference30.Position.X - native60.Position.X,
        reference30.Position.Z - native60.Position.Z);
    const float referenceVelocityError = std::hypot(reference30.Velocity.X - native60.Velocity.X,
        reference30.Velocity.Z - native60.Velocity.Z);
    passed &= Check(referencePositionError < 0.0001F && referenceVelocityError < 0.0001F
        && std::abs(reference30.TotalTurnRadians - native60.TotalTurnRadians) < 0.0001F,
        "the 60 Hz projectile matches the 30 Hz steering reference through first target impact");

    const TrajectoryResult targetHit30 = SimulateHoming(Flight::Reference30, TargetMotion::Moving, 1.0F, 1.0F, 0.0F);
    const TrajectoryResult targetHit60 = SimulateHoming(Flight::Native60, TargetMotion::Moving, 1.0F, 1.0F, 0.0F);
    passed &= Check(targetHit30.TargetHitTime >= 0.0F && targetHit60.TargetHitTime >= 0.0F
        && std::abs(targetHit30.TargetHitTime - targetHit60.TargetHitTime) < 0.0001F,
        "the reference and native projectile reach a moving target at the same time");

    const TrajectoryResult wallHit30 = SimulateHoming(Flight::Reference30, TargetMotion::Moving,
        1.0F, 1.0F, 0.0F, 0.2F, 2.0F, 10.0F, 4.0F);
    const TrajectoryResult wallHit60 = SimulateHoming(Flight::Native60, TargetMotion::Moving,
        1.0F, 1.0F, 0.0F, 0.2F, 2.0F, 10.0F, 4.0F);
    passed &= Check(wallHit30.FirstEvent == TrajectoryEvent::Wall
        && wallHit60.FirstEvent == TrajectoryEvent::Wall
        && wallHit30.TargetHitTime < 0.0F && wallHit60.TargetHitTime < 0.0F
        && std::abs(wallHit30.FirstEventTime - wallHit60.FirstEventTime) < 0.0001F,
        "the wall stops both trajectories before target contact at the same time");

    const TrajectoryResult release30 = SimulateHoming(Flight::Reference30, TargetMotion::TurnsBehind,
        1.0F, 1.0F, 0.0F, 0.2F, 0.0F, 20.0F);
    const TrajectoryResult release60 = SimulateHoming(Flight::Native60, TargetMotion::TurnsBehind,
        1.0F, 1.0F, 0.0F, 0.2F, 0.0F, 20.0F);
    passed &= Check(release30.TargetReleasedAt == 0.5F && release60.TargetReleasedAt == 0.5F,
        "a target behind the projectile is released on the same steering step");

    const TrajectoryResult accelerate30 = SimulateHoming(Flight::Reference30, TargetMotion::Moving,
        0.8F, 1.6F, 0.5F, 0.2F, 2.0F, 100.0F);
    const TrajectoryResult accelerate60 = SimulateHoming(Flight::Native60, TargetMotion::Moving,
        0.8F, 1.6F, 0.5F, 0.2F, 2.0F, 100.0F);
    const TrajectoryResult decelerate30 = SimulateHoming(Flight::Reference30, TargetMotion::Moving,
        1.6F, 0.8F, 0.5F, 0.2F, 2.0F, 100.0F);
    const TrajectoryResult decelerate60 = SimulateHoming(Flight::Native60, TargetMotion::Moving,
        1.6F, 0.8F, 0.5F, 0.2F, 2.0F, 100.0F);
    const float accelerateError = std::hypot(accelerate30.Position.X - accelerate60.Position.X,
        accelerate30.Position.Z - accelerate60.Position.Z);
    const float decelerateError = std::hypot(decelerate30.Position.X - decelerate60.Position.X,
        decelerate30.Position.Z - decelerate60.Position.Z);
    passed &= Check(accelerateError < 0.0001F && decelerateError < 0.0001F,
        "accelerating and decelerating shots land on the 30 Hz reference path at every tick");
    std::cout << "Homing parity deltas: impact="
        << std::abs(targetHit30.TargetHitTime - targetHit60.TargetHitTime)
        << "s, wall=" << std::abs(wallHit30.FirstEventTime - wallHit60.FirstEventTime)
        << "s, release=" << std::abs(release30.TargetReleasedAt - release60.TargetReleasedAt)
        << "s, accelerate=" << accelerateError << ", decelerate=" << decelerateError
        << " world units.\n";

    const auto affinity = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::VoltDriver, Hunter::Samus, BeamType::VoltDriver, false, false);
    const auto prime = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::PowerBeam, Hunter::Samus, BeamType::VoltDriver, false, true);
    const auto singlePlayer = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::OmegaCannon, Hunter::Samus, BeamType::VoltDriver, true, false);
    passed &= Check(affinity.Index == 10 && !affinity.PrimeHunter,
        "affinity selection uses the same variant resolver");
    passed &= Check(prime.Index == 9 && prime.PrimeHunter,
        "the role's weapon row is selected at fire time");
    passed &= Check(singlePlayer.Index == 17 && !singlePlayer.PrimeHunter,
        "single-player exceptions share the resolver");
    passed &= Check(Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::None, Hunter::Samus, BeamType::VoltDriver, false, false).Index == 0xFF,
        "invalid beam values have no weapon row");
    bool everyPrimeVariantUsesAffinity = true;
    for (std::int32_t beam = 0; beam < Mods::Combat::BaseWeaponVariantCount; ++beam)
    {
        everyPrimeVariantUsesAffinity &= Mods::Combat::ResolveWeaponVariantAtFire(
            static_cast<BeamType>(beam), Hunter::Samus, BeamType::Missile, false, true).Index
                == beam + Mods::Combat::BaseWeaponVariantCount;
    }
    passed &= Check(everyPrimeVariantUsesAffinity,
        "every Prime Hunter weapon resolves through the same affinity row rule");

    passed &= Check(HomingValues(Weapons::Weapons1P, 9) == std::array<std::int32_t, 3>{0, 0, 81}
        && HomingValues(Weapons::Weapons1P, 10) == std::array<std::int32_t, 3>{0, 40, 40}
        && HomingValues(Weapons::Weapons1P, 11) == std::array<std::int32_t, 3>{12, 12, 81},
        "single-player weapon tables retain their actual charge-dependent homing values");
    passed &= Check(HomingValues(Weapons::WeaponsMP, 9) == std::array<std::int32_t, 3>{0, 0, 81}
        && HomingValues(Weapons::WeaponsMP, 10) == std::array<std::int32_t, 3>{0, 40, 40}
        && HomingValues(Weapons::WeaponsMP, 11) == std::array<std::int32_t, 3>{0, 81, 81}
        && HomingValues(Weapons::WeaponsMP, 16) == std::array<std::int32_t, 3>{409, 409, 409},
        "multiplayer weapon variants supply the values selected for homing");
    const Mods::Combat::WeaponVariantSelection standardShockCoil = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::ShockCoil, Hunter::Samus, Weapons::GetAffinityBeam(Hunter::Samus), false, false);
    const Mods::Combat::WeaponVariantSelection affinityShockCoil = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::ShockCoil, Hunter::Sylux, Weapons::GetAffinityBeam(Hunter::Sylux), false, false);
    const WeaponInfo& standardShock = *Weapons::WeaponsMP->at(standardShockCoil.Index);
    const WeaponInfo& affinityShock = *Weapons::WeaponsMP->at(affinityShockCoil.Index);
    passed &= Check(standardShockCoil.Index == 7 && affinityShockCoil.Index == 16
        && TestFlag(standardShock.Flags, WeaponFlags::Continuous)
        && TestFlag(affinityShock.Flags, WeaponFlags::Continuous)
        && !TestFlag(standardShock.Flags, WeaponFlags::LifeDrainUncharged)
        && TestFlag(affinityShock.Flags, WeaponFlags::LifeDrainUncharged)
        && Near(HomingAmountAt(Weapons::WeaponsMP, standardShockCoil.Index, 0), 409.0F)
        && Near(HomingAmountAt(Weapons::WeaponsMP, affinityShockCoil.Index, 0), 409.0F),
        "Shock Coil variants keep the continuous lock row and LifeDrain behavior aligned");
    passed &= Check(HomingValues(Weapons::EnemyWeapons, 0) == std::array<std::int32_t, 3>{0, 20, 20}
        && HomingValues(Weapons::EnemyWeapons, 2) == std::array<std::int32_t, 3>{0, 40, 204}
        && HomingValues(Weapons::EnemyWeapons, 8) == std::array<std::int32_t, 3>{0, 20, 20}
        && HomingValues(Weapons::EnemyWeapons, 9) == std::array<std::int32_t, 3>{0, 20, 20}
        && HomingValues(Weapons::EnemyWeapons, 10) == std::array<std::int32_t, 3>{0, 20, 20},
        "enemy weapon strengths use their source rows");
    passed &= Check(HomingValues(Weapons::BossWeapons, 3) == std::array<std::int32_t, 3>{2048, 2048, 14336}
        && HomingValues(Weapons::GoreaWeapons, 1) == std::array<std::int32_t, 3>{409, 409, 409}
        && HomingValues(Weapons::PlatformWeapons, 0) == std::array<std::int32_t, 3>{0, 40, 204}
        && HomingValues(Weapons::PlatformWeapons, 3) == std::array<std::int32_t, 3>{409, 409, 409},
        "boss, Gorea and platform weapons share the same homing step inputs");
    passed &= Check(Near(HomingAmountAt(Weapons::EnemyWeapons, 0, 60), 0.0F)
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 0, 61), 20.0F)
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 2, 30), 0.0F)
        && HomingAmountAt(Weapons::EnemyWeapons, 2, 31) > 40.0F
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 2, 120), 204.0F)
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 8, 61), 20.0F)
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 9, 61), 20.0F)
        && Near(HomingAmountAt(Weapons::EnemyWeapons, 10, 61), 20.0F)
        && Near(HomingAmountAt(Weapons::BossWeapons, 2, 0), 40.0F)
        && Near(HomingAmountAt(Weapons::BossWeapons, 3, 30), 2048.0F)
        && HomingAmountAt(Weapons::BossWeapons, 3, 31) > 2048.0F
        && Near(HomingAmountAt(Weapons::BossWeapons, 3, 120), 14336.0F)
        && Near(HomingAmountAt(Weapons::GoreaWeapons, 1, 0), 409.0F)
        && HomingAmountAt(Weapons::PlatformWeapons, 0, 31) > 40.0F
        && Near(HomingAmountAt(Weapons::PlatformWeapons, 0, 120), 204.0F)
        && Near(HomingAmountAt(Weapons::PlatformWeapons, 3, 0), 409.0F),
        "enemy, boss, Gorea and platform homing stages use their actual spawn charge rules");

    const WeaponFlags partialFlags = WeaponFlags::CanCharge | WeaponFlags::PartialCharge;
    const Mods::Combat::WeaponChargeState beforeMinimum = Mods::Combat::ResolveWeaponCharge(partialFlags, 5, 10, 9);
    const Mods::Combat::WeaponChargeState atMinimum = Mods::Combat::ResolveWeaponCharge(partialFlags, 5, 10, 10);
    const Mods::Combat::WeaponChargeState halfway = Mods::Combat::ResolveWeaponCharge(partialFlags, 5, 10, 15);
    const Mods::Combat::WeaponChargeState full = Mods::Combat::ResolveWeaponCharge(partialFlags, 5, 10, 20);
    passed &= Check(!beforeMinimum.Charged && atMinimum.Charged && Near(atMinimum.Percent, 0.0F)
        && Near(halfway.Percent, 0.5F) && Near(full.Percent, 1.0F),
        "partial charge stages follow the projectile charge calculation");
    passed &= Check(!Mods::Combat::ResolveWeaponCharge(WeaponFlags::None, 5, 10, 20).Charged
        && !Mods::Combat::ResolveWeaponCharge(WeaponFlags::CanCharge, 5, 10, 19).Charged
        && Mods::Combat::ResolveWeaponCharge(WeaponFlags::CanCharge, 5, 10, 20).Charged,
        "non-partial and unchargeable weapons use their actual charge stage");

    const auto mpHomingAtFire = [](BeamType beam, Hunter hunter, bool primeHolder,
        std::uint16_t chargeLevel)
    {
        const Mods::Combat::WeaponVariantSelection variant = Mods::Combat::ResolveWeaponVariantAtFire(beam, hunter,
            Weapons::GetAffinityBeam(hunter), false, primeHolder);
        return HomingAmountAt(Weapons::WeaponsMP, variant.Index, chargeLevel);
    };
    const auto powerBeamAt = [&mpHomingAtFire](bool primeHolder, std::uint16_t level)
    {
        return mpHomingAtFire(BeamType::PowerBeam, Hunter::Samus, primeHolder, level);
    };
    passed &= Check(Near(powerBeamAt(false, 35), 0.0F) && Near(powerBeamAt(false, 36), 0.0F)
        && Near(powerBeamAt(false, 37), 0.0F) && Near(powerBeamAt(false, 48), 0.0F)
        && Near(powerBeamAt(false, 60), 0.0F)
        && Near(powerBeamAt(true, 35), 0.0F) && Near(powerBeamAt(true, 36), 0.0F)
        && Near(powerBeamAt(true, 37), 3.375F) && Near(powerBeamAt(true, 48), 40.5F)
        && Near(powerBeamAt(true, 60), 81.0F),
        "Power Beam charge boundaries use the selected multiplayer row");

    passed &= Check(Near(mpHomingAtFire(BeamType::Missile, Hunter::Samus, false, 0), 0.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Samus, false, 89), 0.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Samus, false, 90), 81.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Kanden, false, 0), 0.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Kanden, false, 89), 0.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Kanden, false, 90), 0.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Samus, true, 90), 81.0F)
        && Near(mpHomingAtFire(BeamType::Missile, Hunter::Kanden, true, 90), 81.0F),
        "multiplayer missiles resolve hunter affinity, Prime role and the charge threshold together");

    passed &= Check(Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Kanden, false, 0), 0.0F)
        && Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Kanden, false, 119), 0.0F)
        && Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Kanden, false, 120), 40.0F)
        && Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Samus, false, 120), 0.0F)
        && Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Kanden, true, 120), 40.0F)
        && Near(mpHomingAtFire(BeamType::VoltDriver, Hunter::Samus, true, 120), 40.0F),
        "multiplayer Volt Driver resolves hunter affinity, Prime role and the charge threshold together");

    const Mods::Combat::WeaponVariantSelection samusMissile1P = Mods::Combat::ResolveWeaponVariantAtFire(
        BeamType::Missile, Hunter::Samus, Weapons::GetAffinityBeam(Hunter::Samus), true, false);
    passed &= Check(Near(HomingAmountAt(Weapons::Weapons1P, samusMissile1P.Index, 0), 12.0F)
        && Near(HomingAmountAt(Weapons::Weapons1P, samusMissile1P.Index, 90), 81.0F),
        "single-player Samus missiles use their affinity row at zero and full charge");

    passed &= Check(Near(Mods::Combat::InterpolateWeaponChargeAmount(409, 409, 409,
        Mods::Combat::ResolveWeaponCharge(WeaponFlags::Continuous, 0, 0, 0)), 409.0F),
        "continuous lock strength stays independent of ballistic charge stages");

    using MphRead::Mods::Network::HomingTargetSelection;
    using MphRead::Mods::Network::HomingTargetSelectionRequest;
    using MphRead::Mods::Network::HomingTargetSnapshot;
    const auto playerTarget = [](std::uint8_t slot, Vector3 position, std::int32_t team,
        bool targetable = true)
    {
        HomingTargetSnapshot candidate{};
        candidate.Reference = { IntentPacket::ShotTargetKind::Player, 0xFF, slot, 1, 1, -1, 0 };
        candidate.Type = EntityType::Player;
        candidate.Position = position;
        candidate.TeamIndex = team;
        candidate.OwnerSlot = slot;
        candidate.Targetable = targetable;
        return candidate;
    };
    HomingTargetSelectionRequest selection{};
    selection.Origin = Vector3::Zero;
    selection.Direction = Vector3(0, 0, 1);
    selection.OwnerType = EntityType::Player;
    selection.OwnerSlot = 0;
    selection.OwnerTeam = 0;
    selection.Beam = BeamType::Missile;
    selection.Range = 100.0F;
    selection.Tolerance = 0.9F;
    const std::array<HomingTargetSnapshot, 2> angleCandidates{
        playerTarget(1, Vector3(2, 0, 5), 1),
        playerTarget(2, Vector3(0, 0, 20), 1)
    };
    const auto angleSelected = HomingTargetSelection::Select(angleCandidates, selection);
    passed &= Check(angleSelected.Slot == 2,
        "homing acquisition selects the best cosine instead of the nearest target");

    HomingTargetSelectionRequest continuousSelection = selection;
    continuousSelection.Beam = BeamType::ShockCoil;
    continuousSelection.Continuous = true;
    continuousSelection.Range = 5.0F;
    continuousSelection.Tolerance = 0.9F;
    HomingTargetSnapshot movingContinuousTarget = playerTarget(3, Vector3(0.1F, 0, 8), 1);
    const auto initialContinuousLock = HomingTargetSelection::Select(
        std::span<const HomingTargetSnapshot>(&movingContinuousTarget, 1), continuousSelection);
    movingContinuousTarget.Position = Vector3(0.2F, 0, 8);
    const auto movedContinuousLock = HomingTargetSelection::Select(
        std::span<const HomingTargetSnapshot>(&movingContinuousTarget, 1), continuousSelection);
    HomingTargetSnapshot morphingContinuousTarget = playerTarget(4, Vector3(1.0F, 0, 8), 1);
    morphingContinuousTarget.Morphing = true;
    const auto morphingContinuousLock = HomingTargetSelection::Select(
        std::span<const HomingTargetSnapshot>(&morphingContinuousTarget, 1), continuousSelection);
    passed &= Check(initialContinuousLock == movedContinuousLock
        && initialContinuousLock.Kind == IntentPacket::ShotTargetKind::Player
        && morphingContinuousLock.Kind == IntentPacket::ShotTargetKind::None,
        "continuous Shock Coil lock tracks a moving target beyond range and applies the morph threshold");

    const bool teamsWereEnabled = GameState::Teams();
    const std::int32_t teamCountWas = GameState::TeamCount();
    GameState::Teams(true);
    GameState::TeamCount(4);
    const std::array<HomingTargetSnapshot, 2> teamCandidates{
        playerTarget(1, Vector3(0, 0, 4), 0),
        playerTarget(2, Vector3(0, 0, 8), 1)
    };
    passed &= Check(HomingTargetSelection::Select(teamCandidates, selection).Slot == 2,
        "homing acquisition skips allied players before comparing target angles");
    GameState::Teams(teamsWereEnabled);
    GameState::TeamCount(teamCountWas);

    const std::array<HomingTargetSnapshot, 1> behindCandidate{
        playerTarget(1, Vector3(0, 0, -4), 1)
    };
    const std::array<HomingTargetSnapshot, 1> destroyedCandidate{
        playerTarget(1, Vector3(0, 0, 4), 1, false)
    };
    passed &= Check(HomingTargetSelection::Select(behindCandidate, selection).Kind
        == IntentPacket::ShotTargetKind::None,
        "a target behind the launch ray is not acquired");
    passed &= Check(HomingTargetSelection::Select(destroyedCandidate, selection).Kind
        == IntentPacket::ShotTargetKind::None,
        "a target already destroyed at selection time is not acquired");

    static_cast<void>(HomingSteeringRules::ConfigureFrequencyHz(HomingSteeringRules::DefaultFrequencyHz));
    if (passed)
    {
        std::cout << "Shared weapon combat rules passed.\n";
    }
    return passed ? 0 : 1;
}
