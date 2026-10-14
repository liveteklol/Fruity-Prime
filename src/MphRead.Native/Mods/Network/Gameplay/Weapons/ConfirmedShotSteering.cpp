#include "Mods/Network/Gameplay/Weapons/ConfirmedShotSteering.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Session/NetSession.hpp"

namespace MphRead::Mods::Network
{
    using Entities::BeamProjectileEntity;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Length;

    namespace
    {
        void DrawImpact(BeamProjectileEntity& beam)
        {
            beam.ModTouchedTarget = true;
            const PlayerEntity* owner = NetHitPrediction::OwnerOf(&beam);
            NetPlayerBridge::SynthesizeImpact(owner != nullptr ? owner->SlotIndex() : -1, beam.ModTargetSlot,
                static_cast<std::uint8_t>(beam.Beam()), static_cast<std::uint8_t>(beam.ModShooterAck & 0xFFU),
                beam.ModConfirmedOffset);
        }
    }

    void ConfirmedShotSteering::Apply(BeamProjectileEntity& beam)
    {
        ShotPresentation& drawn = beam.ModPresentation;
        const auto& players = PlayerEntity::Players();
        const bool targetKnown = beam.ModTargetSlot >= 0
            && beam.ModTargetSlot < static_cast<std::int32_t>(players.size());
        const Vector3 position = drawn.DrawnPosition(static_cast<Vector3>(beam.Position));
        if (beam.ModHeldUntil != 0)
        {
            if (beam.ModConfirmedTarget)
            {
                beam.ModHeldUntil = 0;
            }
            else if (static_cast<std::int32_t>(NetSession::NetFrame() - beam.ModHeldUntil) >= 0)
            {
                // No word: it did not hit. On it goes, through them.
                beam.ModHeldUntil = 0;
                beam.ModPassedTarget = true;
                NetPlayerBridge::NotePassedLocal(beam);
                if (HitLocation::Enabled() && targetKnown)
                {
                    HitLocation::Passed(beam, *players[static_cast<std::size_t>(beam.ModTargetSlot)], beam.ModHeldPoint);
                }
            }
            else
            {
                // Still waiting: it stays where it met the body.
                if (drawn.Active())
                {
                    drawn.Hold(beam.ModHeldPoint);
                }
                else
                {
                    beam.Position = beam.ModHeldPoint;
                    beam.SetBackPosition(beam.ModHeldPoint);
                }
                return;
            }
        }
        if (!beam.ModConfirmedTarget || beam.ModTouchedTarget || !targetKnown)
        {
            return;
        }
        const PlayerEntity& victim = *players[static_cast<std::size_t>(beam.ModTargetSlot)];
        const Vector3 target = static_cast<Vector3>(victim.Position) + beam.ModConfirmedOffset;
        if (victim.Health() <= 0)
        {
            // Died before it arrived (this hit, or another): a body takes no
            // collision, so the hit is drawn on it here and the shot is spent.
            drawn.Place(target);
            DrawImpact(beam);
            return;
        }
        const Vector3 toTarget = target - position;
        const float distance = Length(toTarget);
        const float speed = Length(drawn.Active() ? drawn.Velocity() : beam.Velocity());
        if (!(distance > 0.0001F) || !(speed > 0.0001F))
        {
            return;
        }
        if (drawn.Active() && distance <= speed)
        {
            drawn.Place(target);
            DrawImpact(beam);
            return;
        }
        // Straight at the spot, at the speed it was already flying: the shot
        // the shooter saw land, arriving where it landed.
        const Vector3 velocity = ::OpenTK::Mathematics::Scale(toTarget, speed / distance);
        if (drawn.Active())
        {
            drawn.Aim(velocity);
        }
        else
        {
            beam.SetVelocity(velocity);
        }
    }
}
