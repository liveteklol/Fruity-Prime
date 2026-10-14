#include "Mods/Network/Gameplay/Weapons/ShotPresentation.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Formats/Effects.hpp"
#include "Mods/Combat/ProjectileHoming.hpp"
#include "Mods/Combat/ProjectileFlight.hpp"
#include "Mods/Network/Gameplay/Weapons/ConfirmedShotSteering.hpp"
#include "Scene.hpp"

namespace MphRead::Mods::Network
{
    using Entities::BeamFlags;
    using Entities::BeamProjectileEntity;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::IsZero;
    using ::OpenTK::Mathematics::Length;
    using ::OpenTK::Mathematics::Normalize;
    using ::OpenTK::Mathematics::SetRow3;

    namespace
    {
        [[nodiscard]] Vector3 SpawnRight(Vector3 direction) noexcept
        {
            const Vector3 right(direction.Z, 0.0F, -direction.X);
            return !IsZero(right) ? Normalize(right) : Vector3(1.0F, 0.0F, 0.0F);
        }
    }

    void ShotPresentation::Begin(const BeamProjectileEntity& beam, Vector3 position,
        std::optional<Vector3> drawnDirection)
    {
        Reset();
        if (beam.ModShooterAck == 0 || ::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous))
        {
            return;
        }
        const Vector3 physicalDirection = Normalize(beam.Direction());
        const Vector3 direction = drawnDirection.has_value() && !IsZero(*drawnDirection)
            ? Normalize(*drawnDirection) : physicalDirection;
        const bool unretargeted = Vector3::Dot(direction, physicalDirection) > 0.999999F;
        _active = true;
        _position = position;
        _backPosition = position;
        _velocity = unretargeted ? beam.Velocity() : ::OpenTK::Mathematics::Scale(direction, Length(beam.Velocity()));
        _direction = direction;
        _right = SpawnRight(direction);
        _up = Normalize(Vector3::Cross(_direction, _right));
        _pastPositions.fill(position);
    }

    void ShotPresentation::Reposition(Vector3 offset) noexcept
    {
        if (!_active)
        {
            return;
        }
        _position = _position + offset;
        _backPosition = _backPosition + offset;
        for (Vector3& past : _pastPositions)
        {
            past = past + offset;
        }
    }

    void ShotPresentation::Place(Vector3 point) noexcept
    {
        if (_active)
        {
            _position = point;
        }
    }

    void ShotPresentation::Hold(Vector3 point) noexcept
    {
        if (_active)
        {
            _position = point;
            _backPosition = point;
        }
    }

    void ShotPresentation::Aim(Vector3 velocity) noexcept
    {
        if (!_active)
        {
            return;
        }
        _velocity = velocity;
        _direction = Normalize(velocity);
        _right = BeamProjectileEntity::ModCrossVector(_direction);
        _up = Normalize(Vector3::Cross(_direction, _right));
    }

    void ShotPresentation::CopyPhysical(const BeamProjectileEntity& beam)
    {
        _position = static_cast<Vector3>(beam.Position);
        _backPosition = beam.BackPosition();
        _velocity = beam.Velocity();
        _direction = beam.Direction();
        _right = beam.Right();
        _up = beam.Up();
        _pastPositions = beam.PastPositions();
    }

    void ShotPresentation::Advance(const BeamProjectileEntity& beam, const Scene& scene)
    {
        if (!_active || beam.ModTouchedTarget)
        {
            return;
        }
        const bool confirmedFlight = beam.ModConfirmedTarget && !beam.ModPassedTarget;
        if (::MphRead::TestFlag(beam.Flags(), BeamFlags::Collided) && !confirmedFlight)
        {
            CopyPhysical(beam);
            return;
        }
        if (beam.ModHeldUntil != 0 && !beam.ModConfirmedTarget)
        {
            _position = beam.ModHeldPoint;
            _backPosition = beam.ModHeldPoint;
            return;
        }
        if (::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous))
        {
            _position = static_cast<Vector3>(beam.Position);
            _backPosition = beam.BackPosition();
            return;
        }

        _backPosition = _position;
        if (scene.FrameCount() % 2 == 0)
        {
            for (std::size_t i = _pastPositions.size() - 1; i > 0; --i)
            {
                _pastPositions[i] = _pastPositions[i - 1];
            }
            _pastPositions[0] = _position;
        }
        Combat::ProjectileFlightState flight{_position, _velocity, beam.Speed(), _flightPhase};
        Combat::ProjectileFlight::Move(Combat::ProjectileFlight::ParametersOf(beam), beam.Age(), flight);
        _position = flight.Position;
        _velocity = flight.Velocity;
        _flightPhase = flight.TickPhase;
        static_cast<void>(Combat::ProjectileHoming::Steer(beam, _homingPhase, _position, _velocity));
        if (!IsZero(_velocity))
        {
            _direction = Normalize(_velocity);
            _right = BeamProjectileEntity::ModCrossVector(_direction);
            _up = Normalize(Vector3::Cross(_direction, _right));
        }
    }

    void ShotPresentation::Step(BeamProjectileEntity& beam, const Scene& scene)
    {
        if (!_active)
        {
            return;
        }
        Advance(beam, scene);
        ConfirmedShotSteering::Apply(beam);
    }

    void ShotPresentation::ContinueAfterCollision(BeamProjectileEntity& beam, const Scene& scene)
    {
        if (!_active)
        {
            return;
        }
        beam.SetAge(beam.Age() + scene.FrameTime());
        Advance(beam, scene);
        ConfirmedShotSteering::Apply(beam);
        static_cast<void>(TransformEffect(beam));
    }

    Matrix4 ShotPresentation::Transform() const
    {
        Matrix4 transform = Entities::EntityBase::GetTransformMatrix(_direction, _up);
        SetRow3(transform, _position);
        return transform;
    }

    bool ShotPresentation::TransformEffect(BeamProjectileEntity& beam) const
    {
        if (!_active)
        {
            return false;
        }
        if (const auto effect = beam.Effect(); effect != nullptr)
        {
            effect->Transform(_position, ::OpenTK::Mathematics::ClearScale(Transform()));
        }
        return true;
    }

    ShotPresentation::DrawScope::DrawScope(BeamProjectileEntity& beam) noexcept
        : _beam(beam.ModPresentation.Active() ? &beam : nullptr)
    {
        if (_beam == nullptr)
        {
            return;
        }
        _position = static_cast<Vector3>(beam.Position);
        _backPosition = beam.BackPosition();
        _velocity = beam.Velocity();
        _direction = beam.Direction();
        _right = beam.Right();
        _up = beam.Up();
        _pastPositions = beam.PastPositions();
        _transform = static_cast<Matrix4>(beam.Transform);
        _flags = beam.Flags();

        const ShotPresentation& drawn = beam.ModPresentation;
        beam.Position = drawn._position;
        beam.SetBackPosition(drawn._backPosition);
        beam.SetVelocity(drawn._velocity);
        beam.SetDirection(drawn._direction);
        beam.SetRight(drawn._right);
        beam.SetUp(drawn._up);
        beam.PastPositions() = drawn._pastPositions;
        // A confirmed shot still flying to its impact is drawn as a shot,
        // not as the collision its physical copy already had.
        if (beam.ModConfirmedTarget && !beam.ModTouchedTarget && !beam.ModPassedTarget)
        {
            beam.SetFlags(beam.Flags() & ~BeamFlags::Collided);
        }
        beam.Transform = drawn.Transform();
    }

    ShotPresentation::DrawScope::~DrawScope()
    {
        if (_beam == nullptr)
        {
            return;
        }
        _beam->Position = _position;
        _beam->SetBackPosition(_backPosition);
        _beam->SetVelocity(_velocity);
        _beam->SetDirection(_direction);
        _beam->SetRight(_right);
        _beam->SetUp(_up);
        _beam->PastPositions() = _pastPositions;
        _beam->SetFlags(_flags);
        _beam->Transform = _transform;
    }
}
