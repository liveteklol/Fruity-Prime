#pragma once

#include "Formats/Enums.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    enum class BeamFlags : std::uint16_t;
}

namespace MphRead::Mods::Network
{
    // The drawn flight of another player's shot, kept beside its physical
    // flight. The physical one is the shooter's ray and the original homing,
    // and is what collides; this one may start along a ray corrected for this
    // screen and may be steered onto a confirmed impact. Neither touches the
    // other, so a display correction can never move a shot that counts.
    class ShotPresentation final
    {
    public:
        [[nodiscard]] bool Active() const noexcept { return _active; }
        [[nodiscard]] OpenTK::Mathematics::Vector3 Position() const noexcept { return _position; }
        [[nodiscard]] OpenTK::Mathematics::Vector3 Velocity() const noexcept { return _velocity; }
        // Where a sound or an effect of this shot is heard or drawn.
        [[nodiscard]] OpenTK::Mathematics::Vector3 DrawnPosition(
            OpenTK::Mathematics::Vector3 physical) const noexcept
        {
            return _active ? _position : physical;
        }

        void Reset() noexcept { *this = {}; }
        // At spawn, after the physical velocity is set: a remote copy of a
        // ballistic shot draws its own flight, along drawnDirection if given.
        void Begin(const Entities::BeamProjectileEntity& beam, OpenTK::Mathematics::Vector3 position,
            std::optional<OpenTK::Mathematics::Vector3> drawnDirection);
        void Reposition(OpenTK::Mathematics::Vector3 offset) noexcept;
        void Place(OpenTK::Mathematics::Vector3 point) noexcept;
        void Hold(OpenTK::Mathematics::Vector3 point) noexcept;
        void Aim(OpenTK::Mathematics::Vector3 velocity) noexcept;

        // One simulation step of the drawn flight, then the confirmed-impact
        // correction; after the physical one has moved.
        void Step(Entities::BeamProjectileEntity& beam, const Scene& scene);
        // A physical shot that already collided may still be drawn flying to
        // the impact the authority confirmed.
        void ContinueAfterCollision(Entities::BeamProjectileEntity& beam, const Scene& scene);
        // Places the shot's effect on the drawn flight; false when inactive.
        bool TransformEffect(Entities::BeamProjectileEntity& beam) const;

        // While alive, the beam holds its drawn flight: what the draw pass
        // reads. Everything is put back on destruction.
        class DrawScope final
        {
        public:
            explicit DrawScope(Entities::BeamProjectileEntity& beam) noexcept;
            ~DrawScope();
            DrawScope(const DrawScope&) = delete;
            DrawScope& operator=(const DrawScope&) = delete;

        private:
            Entities::BeamProjectileEntity* _beam = nullptr;
            OpenTK::Mathematics::Vector3 _position{};
            OpenTK::Mathematics::Vector3 _backPosition{};
            OpenTK::Mathematics::Vector3 _velocity{};
            OpenTK::Mathematics::Vector3 _direction{};
            OpenTK::Mathematics::Vector3 _right{};
            OpenTK::Mathematics::Vector3 _up{};
            std::array<OpenTK::Mathematics::Vector3, 10> _pastPositions{};
            OpenTK::Mathematics::Matrix4 _transform{};
            Entities::BeamFlags _flags{};
        };

    private:
        void Advance(const Entities::BeamProjectileEntity& beam, const Scene& scene);
        void CopyPhysical(const Entities::BeamProjectileEntity& beam);
        [[nodiscard]] OpenTK::Mathematics::Matrix4 Transform() const;

        bool _active = false;
        OpenTK::Mathematics::Vector3 _position{};
        OpenTK::Mathematics::Vector3 _backPosition{};
        OpenTK::Mathematics::Vector3 _velocity{};
        OpenTK::Mathematics::Vector3 _direction{};
        OpenTK::Mathematics::Vector3 _right{};
        OpenTK::Mathematics::Vector3 _up{};
        std::array<OpenTK::Mathematics::Vector3, 10> _pastPositions{};
        std::int32_t _homingPhase = 0;
        std::int32_t _flightPhase = 0;
    };
}
