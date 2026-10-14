#pragma once

#include "Metadata/Metadata.hpp"
#include "Mods/Combat/ProjectileValueCurve.hpp"
#include "Mods/Combat/WeaponCharge.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Diagnostics
{
    // A homing projectile flown the way the DS flies one: one update every
    // 1/30 s, in the weapon table's own units (x/4096 world units per tick).
    // It is written apart from the projectile code on purpose -- it is what
    // the 60 Hz flight is measured against, so it must not share its steps.
    class HomingReferenceFlight final
    {
    public:
        static constexpr float TickSeconds = 1.0F / 30.0F;

        struct Spec final
        {
            float SpeedRaw = 0.0F;
            float FinalSpeedRaw = 0.0F;
            std::int32_t DecayTicks = 0;
            std::int32_t Interpolation = 0;
            float GravityRaw = 0.0F;
            float HomingRaw = 0.0F;
        };

        enum class SteerResult : std::uint8_t
        {
            Idle,
            Steered,
            Released
        };

        // What a weapon row fires at a charge level, read from the table.
        [[nodiscard]] static Spec FromWeapon(const WeaponInfo& weapon, std::uint16_t chargeLevel)
        {
            const Combat::WeaponChargeState charge = Combat::ResolveWeaponCharge(
                weapon.Flags, weapon.MinCharge, weapon.FullCharge, chargeLevel);
            const auto amount = [&charge](std::int32_t uncharged, std::int32_t minimum, std::int32_t full)
            {
                return Combat::InterpolateWeaponChargeAmount(uncharged, minimum, full, charge);
            };
            const std::size_t stage = charge.Charged ? 1U : 0U;
            Spec spec{};
            spec.SpeedRaw = amount(weapon.UnchargedSpeed, weapon.MinChargeSpeed, weapon.ChargedSpeed);
            spec.FinalSpeedRaw = amount(weapon.UnchargedFinalSpeed, weapon.MinChargeFinalSpeed,
                weapon.ChargedFinalSpeed);
            spec.DecayTicks = (*weapon.SpeedDecayTimes)[stage];
            spec.Interpolation = (*weapon.SpeedInterpolations)[stage];
            spec.GravityRaw = amount(weapon.UnchargedGravity, weapon.MinChargeGravity, weapon.ChargedGravity);
            spec.HomingRaw = amount(weapon.UnchargedHoming, weapon.MinChargeHoming, weapon.ChargedHoming);
            return spec;
        }

        // One velocity correction: toward the target at the current speed,
        // by at most maximumDelta; a target behind the travel is let go.
        [[nodiscard]] static std::optional<OpenTK::Mathematics::Vector3> SteerVelocity(
            OpenTK::Mathematics::Vector3 position, OpenTK::Mathematics::Vector3 target,
            OpenTK::Mathematics::Vector3 velocity, float speed, float maximumDelta)
        {
            using OpenTK::Mathematics::Vector3;
            Vector3 desired = target - position;
            const float desiredLength = std::sqrt(Vector3::Dot(desired, desired));
            desired = desiredLength > 0.0F
                ? ::OpenTK::Mathematics::Scale(desired, speed / desiredLength)
                : ::OpenTK::Mathematics::Scale(Vector3(1.0F, 0.0F, 0.0F), speed);
            if (Vector3::Dot(desired, velocity) < 0.0F)
            {
                return std::nullopt;
            }
            Vector3 delta = desired - velocity;
            const float deltaLength = std::sqrt(Vector3::Dot(delta, delta));
            if (deltaLength > maximumDelta)
            {
                delta = ::OpenTK::Mathematics::Scale(delta, maximumDelta / deltaLength);
            }
            return velocity + delta;
        }

        HomingReferenceFlight(const Spec& spec, OpenTK::Mathematics::Vector3 origin,
            OpenTK::Mathematics::Vector3 direction)
            : _spec(spec), _speed(spec.SpeedRaw / 4096.0F), _position(origin), _back(origin)
        {
            const float length = std::sqrt(OpenTK::Mathematics::Vector3::Dot(direction, direction));
            _velocity = ::OpenTK::Mathematics::Scale(direction, _speed / length);
        }

        [[nodiscard]] std::int32_t Tick() const noexcept { return _tick; }
        [[nodiscard]] float Seconds() const noexcept { return static_cast<float>(_tick) * TickSeconds; }
        [[nodiscard]] OpenTK::Mathematics::Vector3 Position() const noexcept { return _position; }
        [[nodiscard]] OpenTK::Mathematics::Vector3 Back() const noexcept { return _back; }
        [[nodiscard]] OpenTK::Mathematics::Vector3 Velocity() const noexcept { return _velocity; }
        [[nodiscard]] bool Tracking() const noexcept { return _tracking; }

        // Moves one tick: travel, gravity, the speed curve.
        void Move()
        {
            using OpenTK::Mathematics::Vector3;
            ++_tick;
            _back = _position;
            _position = _position + _velocity;
            _velocity.Y += _spec.GravityRaw / 4096.0F;
            const float age = Seconds();
            const float decaySeconds = static_cast<float>(_spec.DecayTicks) * TickSeconds;
            if (decaySeconds > 0.0F && age <= decaySeconds)
            {
                const float length = std::sqrt(Vector3::Dot(_velocity, _velocity));
                if (length > 0.0F)
                {
                    _speed = Combat::InterpolateProjectileValue(_spec.Interpolation,
                        _spec.SpeedRaw / 4096.0F, _spec.FinalSpeedRaw / 4096.0F, age / decaySeconds);
                    _velocity = ::OpenTK::Mathematics::Scale(_velocity, _speed / length);
                }
            }
        }

        // Steers after the tick's collision test, while it still has a target.
        SteerResult Steer(OpenTK::Mathematics::Vector3 target)
        {
            if (!_tracking || !(_spec.HomingRaw > 0.0F))
            {
                return SteerResult::Idle;
            }
            const auto steered = SteerVelocity(_position, target, _velocity, _speed, _spec.HomingRaw / 4096.0F);
            if (!steered.has_value())
            {
                _tracking = false;
                return SteerResult::Released;
            }
            _velocity = *steered;
            return SteerResult::Steered;
        }

    private:
        Spec _spec{};
        float _speed = 0.0F;
        OpenTK::Mathematics::Vector3 _position{};
        OpenTK::Mathematics::Vector3 _back{};
        OpenTK::Mathematics::Vector3 _velocity{};
        std::int32_t _tick = 0;
        bool _tracking = true;
    };
}
