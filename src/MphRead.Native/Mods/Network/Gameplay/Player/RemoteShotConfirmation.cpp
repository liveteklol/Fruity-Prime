#include "Mods/Network/Gameplay/Player/RemoteShotConfirmation.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    namespace
    {
        using ::MphRead::Entities::BeamProjectileEntity;
        using ::MphRead::Entities::PlayerEntity;

        // The same shot's launch frame as two machines stamped it: the
        // victim's copy is the ack of whichever intent was newest when the
        // trigger pull arrived, a frame or two either side of the shooter's.
        [[nodiscard]] bool SameLaunch(std::uint8_t a, std::uint8_t b) noexcept
        {
            const auto diff = static_cast<std::uint8_t>(a - b);
            return diff <= 3U || diff >= 253U;
        }

        [[nodiscard]] bool ConfirmableBeam(std::uint8_t beam) noexcept
        {
            // The Shock Coil is one continuous beam drawn every frame: there
            // is no projectile to bring in, and its ticks would each be an
            // impact. It is drawn as the shooter aims it.
            return beam < 9 && static_cast<::MphRead::BeamType>(beam) != ::MphRead::BeamType::ShockCoil;
        }

        // A remote human's projectile on a machine that only draws it.
        [[nodiscard]] PlayerEntity* RemoteShooterOf(BeamProjectileEntity& beam) noexcept
        {
            if (!NetSession::Active() || NetSession::IsAuthority() || NetSession::IsHost())
            {
                return nullptr;
            }
            PlayerEntity* owner = NetHitPrediction::OwnerOf(&beam);
            if (owner == nullptr || owner->IsBot() || owner->SlotIndex() == NetHooks::LocalSlot()
                || owner->SlotIndex() < 0)
            {
                return nullptr;
            }
            return owner;
        }
    }

    void RemoteShotConfirmation::ConfirmIncoming(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
        bool keyed, std::uint8_t launchLow, ImpactOffset impact, bool headshot)
    {
        if (!keyed)
        {
            // An older event of the history, which carries no shot key: the
            // hit is still drawn, at the chest, with no shot brought in.
            const std::int32_t me = NetHooks::LocalSlot();
            if (_confirmedImpacts && attackerSlot >= 0 && attackerSlot != me && victimSlot >= 0
                && attackerSlot != victimSlot && ConfirmableBeam(beam) && (victimSlot == me || _observedImpacts))
            {
                NativeRuntime::IncrementInPlace(_unkeyedImpacts);
                SynthesizeImpact(attackerSlot, victimSlot, beam, 0, OpenTK::Mathematics::Vector3(0.0F, 0.3F, 0.0F), headshot);
            }
            return;
        }
        using OpenTK::Mathematics::Vector3;
        const auto& players = Entities::PlayerEntity::Players();
        const std::int32_t local = NetHooks::LocalSlot();
        // The shooter's own machine predicted this hit itself; any other
        // machine is the victim's or an observer's.
        if (_confirmedImpacts && attackerSlot >= 0 && attackerSlot < static_cast<std::int32_t>(_coilVictim.size())
            && attackerSlot != local && victimSlot >= 0 && victimSlot != attackerSlot
            && beam == static_cast<std::uint8_t>(::MphRead::BeamType::ShockCoil)
            && (victimSlot == local || _observedImpacts) && impact.Known())
        {
            // A tick of the continuous beam: nothing to bring in, but the
            // beam itself is aimed at where the ticks land for a few frames.
            const auto a = static_cast<std::size_t>(attackerSlot);
            _coilVictim[a] = victimSlot;
            _coilOffset[a] = impact.Value();
            _coilUntil[a] = NetSession::NetFrame() + 12U;
            NativeRuntime::IncrementInPlace(_coilTicks);
            return;
        }
        if (!_confirmedImpacts || local < 0 || attackerSlot < 0 || attackerSlot == local || attackerSlot == victimSlot
            || victimSlot < 0 || static_cast<std::size_t>(attackerSlot) >= players.size()
            || static_cast<std::size_t>(victimSlot) >= players.size() || !ConfirmableBeam(beam)
            || (victimSlot != local && !_observedImpacts))
        {
            NativeRuntime::IncrementInPlace(_confirmsIgnored);
            return;
        }
        if (victimSlot != local)
        {
            NativeRuntime::IncrementInPlace(_observedConfirms);
        }
        const Entities::PlayerEntity& victim = *players[static_cast<std::size_t>(victimSlot)];
        // Where the shooter saw it land; the chest when the authority
        // resolved the hit itself and nobody said.
        const Vector3 offset = impact.Known() ? impact.Value() : Vector3(0.0F, 0.3F, 0.0F);
        Entities::PlayerEntity& shooter = *players[static_cast<std::size_t>(attackerSlot)];
        if (shooter.EquipInfo() != nullptr && shooter.EquipInfo()->Beams != nullptr)
        {
            // The closest launch frame wins: a fast weapon has a shot in the
            // air every few frames, and the neighbour is not the one that hit.
            auto& beams = *shooter.EquipInfo()->Beams;
            Entities::BeamProjectileEntity* best = nullptr;
            std::int32_t bestGap = 4;
            for (std::int32_t i = 0; i < beams.Length(); ++i)
            {
                Entities::BeamProjectileEntity* shot = beams[i].get();
                if (shot == nullptr || shot->Lifespan() <= 0 || shot->ModShooterAck == 0 || shot->ModConfirmedTarget
                    || ::MphRead::TestFlag(shot->Flags(), Entities::BeamFlags::Collided)
                    || (shot->ModTargetSlot >= 0 && shot->ModTargetSlot != victimSlot))
                {
                    continue;
                }
                const auto diff = static_cast<std::int8_t>(static_cast<std::uint8_t>(shot->ModShooterAck & 0xFFU) - launchLow);
                const std::int32_t gap = std::abs(static_cast<std::int32_t>(diff));
                if (gap < bestGap)
                {
                    bestGap = gap;
                    best = shot;
                }
            }
            if (best != nullptr && !best->ModPassedTarget && !best->ModTouchedTarget && best->ModHeldUntil == 0)
            {
                // Bringing it in must not mean turning it round: a shot that
                // has gone past, or would have to bend more than 60 degrees,
                // is drawn landing on the spot instead.
                const Vector3 toTarget = static_cast<Vector3>(victim.Position) + offset
                    - static_cast<Vector3>(best->Position);
                const Vector3 velocity = best->Velocity();
                const float lengths = OpenTK::Mathematics::Length(toTarget) * OpenTK::Mathematics::Length(velocity);
                const float cosine = lengths > 0.0001F ? Vector3::Dot(toTarget, velocity) / lengths : 1.0F;
                if (cosine < 0.5F)
                {
                    best->ModTargetSlot = victimSlot;
                    best->ModPassedTarget = true;
                    NoteRemoteShotGone(*best);
                    SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
                    return;
                }
                const double angle = std::acos(std::clamp(static_cast<double>(cosine), -1.0, 1.0)) * 57.29578;
                _homeAngleSum += angle;
                _homeAngleMax = std::max(_homeAngleMax, angle);
            }
            if (best != nullptr && !best->ModPassedTarget && !best->ModTouchedTarget)
            {
                if (best->ModHeldUntil != 0)
                {
                    NoteConfirmDelay(NetSession::NetFrame() - best->ModHeldSince);
                }
                best->ModTargetSlot = victimSlot;
                best->ModConfirmedTarget = true;
                best->ModConfirmedOffset = offset;
                NativeRuntime::IncrementInPlace(_confirmsInFlight);
                return;
            }
            if (best != nullptr)
            {
                // It went by before the word came: draw the hit where it was.
                SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
                return;
            }
        }
        const GoneShot* gone = RecentlyGone(attackerSlot, launchLow);
        if (gone != nullptr && gone->Blast > 0.0F
            && OpenTK::Mathematics::Length(gone->Where - static_cast<Vector3>(victim.Position)) <= gone->Blast + 1.6F)
        {
            // It went out in a blast that reached them here: that explosion is
            // the hit they saw. Another impact on top would be one too many.
            NativeRuntime::IncrementInPlace(_seenAsBlast);
            HitLocation::Synthesized(attackerSlot, victim, beam, 0, gone->Where, headshot, true);
            return;
        }
        if (gone == nullptr)
        {
            // Not drawn yet: the damage outran the relayed trigger pull. Wait
            // a few frames for the shot to appear before drawing it ourselves.
            for (Pending& pending : _pendingConfirms)
            {
                if (!pending.Live)
                {
                    pending = Pending{attackerSlot, launchLow, beam, offset,
                        NetSession::NetFrame() + ConfirmWaitFrames, true, headshot, victimSlot};
                    return;
                }
            }
        }
        SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot);
    }

    void RemoteShotConfirmation::OnRemoteShotSpawned(Entities::BeamProjectileEntity& beam)
    {
        if (!_confirmedImpacts || beam.ModShooterAck == 0)
        {
            return;
        }
        Entities::PlayerEntity* owner = RemoteShooterOf(beam);
        if (owner == nullptr)
        {
            return;
        }
        const auto low = static_cast<std::uint8_t>(beam.ModShooterAck & 0xFFU);
        for (Pending& pending : _pendingConfirms)
        {
            if (pending.Live && pending.Attacker == owner->SlotIndex() && SameLaunch(pending.LaunchLow, low))
            {
                beam.ModTargetSlot = pending.Victim;
                beam.ModConfirmedTarget = true;
                beam.ModConfirmedOffset = pending.Offset;
                pending.Live = false;
                NativeRuntime::IncrementInPlace(_confirmsAtSpawn);
                return;
            }
        }
    }

    void RemoteShotConfirmation::NoteRemoteShotGone(const Entities::BeamProjectileEntity& beam)
    {
        if (beam.ModShooterAck == 0)
        {
            return;
        }
        Entities::PlayerEntity* owner = RemoteShooterOf(const_cast<Entities::BeamProjectileEntity&>(beam));
        if (owner == nullptr)
        {
            return;
        }
        const bool blast = beam.SplashDamage() > 0.0F
            && ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Collided);
        _goneShots[_goneNext] = GoneShot{owner->SlotIndex(),
            static_cast<std::uint8_t>(beam.ModShooterAck & 0xFFU), std::max(1U, NetSession::NetFrame()),
            static_cast<OpenTK::Mathematics::Vector3>(beam.Position), blast ? beam.SplashRadius() : 0.0F};
        _goneNext = (_goneNext + 1) % _goneShots.size();
    }

    MphRead::Scene* RemoteShotConfirmation::_scene = nullptr;
    bool RemoteShotConfirmation::_confirmedImpacts = true;
    std::array<RemoteShotConfirmation::Pending, 16> RemoteShotConfirmation::_pendingConfirms{};
    std::array<RemoteShotConfirmation::GoneShot, 32> RemoteShotConfirmation::_goneShots{};
    std::size_t RemoteShotConfirmation::_goneNext = 0;
    std::int64_t RemoteShotConfirmation::_confirmsInFlight = 0;
    std::int64_t RemoteShotConfirmation::_confirmsAtSpawn = 0;
    std::int64_t RemoteShotConfirmation::_impactsSynthesized = 0;
    std::int64_t RemoteShotConfirmation::_passedThrough = 0;
    std::int64_t RemoteShotConfirmation::_confirmsIgnored = 0;
    double RemoteShotConfirmation::_homeAngleSum = 0;
    double RemoteShotConfirmation::_homeAngleMax = 0;
    bool RemoteShotConfirmation::_observedImpacts = true;
    std::array<std::int32_t, 8> RemoteShotConfirmation::_coilVictim{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<OpenTK::Mathematics::Vector3, 8> RemoteShotConfirmation::_coilOffset{};
    std::array<std::uint32_t, 8> RemoteShotConfirmation::_coilUntil{};
    std::int64_t RemoteShotConfirmation::_coilTicks = 0;
    std::int64_t RemoteShotConfirmation::_unkeyedImpacts = 0;
    std::int64_t RemoteShotConfirmation::_observedConfirms = 0;
    std::int64_t RemoteShotConfirmation::_seenAsBlast = 0;
    std::array<std::uint8_t, 64> RemoteShotConfirmation::_confirmDelays{};
    std::size_t RemoteShotConfirmation::_confirmDelayCount = 0;
    std::size_t RemoteShotConfirmation::_confirmDelayNext = 0;

    const RemoteShotConfirmation::GoneShot* RemoteShotConfirmation::RecentlyGone(std::int32_t attackerSlot, std::uint8_t launchLow)
    {
        const std::uint32_t now = NetSession::NetFrame();
        for (const GoneShot& gone : _goneShots)
        {
            if (gone.Frame != 0 && gone.Attacker == attackerSlot && SameLaunch(gone.LaunchLow, launchLow)
                && now - gone.Frame < 120U)
            {
                return &gone;
            }
        }
        return nullptr;
    }

    bool RemoteShotConfirmation::PassesThrough(Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& player)
    {
        if (!_confirmedImpacts || (beam.ModConfirmedTarget && beam.ModTargetSlot == player.SlotIndex())
            || ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Continuous))
        {
            return false;
        }
        const Entities::PlayerEntity* owner = RemoteShooterOf(beam);
        return owner != nullptr && owner != &player
            && (player.SlotIndex() == NetHooks::LocalSlot() || _observedImpacts);
    }

    void RemoteShotConfirmation::NotePassedLocal(const Entities::BeamProjectileEntity& beam)
    {
        NativeRuntime::IncrementInPlace(_passedThrough);
        NoteRemoteShotGone(beam);
    }

    void RemoteShotConfirmation::TickConfirms()
    {
        const std::uint32_t now = NetSession::NetFrame();
        for (Pending& pending : _pendingConfirms)
        {
            if (pending.Live && static_cast<std::int32_t>(now - pending.Until) >= 0)
            {
                pending.Live = false;
                SynthesizeImpact(pending.Attacker, pending.Victim, pending.Beam, pending.LaunchLow, pending.Offset,
                    pending.Headshot);
            }
        }
    }

    void RemoteShotConfirmation::SynthesizeImpact(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
        std::uint8_t launchLow, OpenTK::Mathematics::Vector3 offset, bool headshot)
    {
        // Drawn on a body that is falling as well: the shot that kills you
        // is the one most worth seeing.
        const auto& players = Entities::PlayerEntity::Players();
        if (victimSlot < 0 || static_cast<std::size_t>(victimSlot) >= players.size())
        {
            return;
        }
        const Entities::PlayerEntity& victim = *players[static_cast<std::size_t>(victimSlot)];
        const OpenTK::Mathematics::Vector3 point = static_cast<OpenTK::Mathematics::Vector3>(victim.Position) + offset;
        Entities::BeamProjectileEntity::ModSpawnImpact(_scene, static_cast<::MphRead::BeamType>(beam), point,
            OpenTK::Mathematics::Vector3(offset.X, 0.0F, offset.Z).LengthSquared() > 0.0001F
                ? OpenTK::Mathematics::Vector3(offset.X, 0.0F, offset.Z).Normalized()
                : OpenTK::Mathematics::Vector3(0.0F, 1.0F, 0.0F));
        NativeRuntime::IncrementInPlace(_impactsSynthesized);
        // The shot key's full frame, from the newest ack this shooter sent.
        std::uint32_t launch = launchLow;
        const auto s = static_cast<std::size_t>(attackerSlot);
        if (attackerSlot >= 0 && s < NetSession::RemotePlayerSlots && NetSession::HasRemoteIntent(attackerSlot))
        {
            const std::uint32_t newest = NetSession::RemoteIntent(s).AckFrame;
            launch = newest - static_cast<std::uint8_t>(static_cast<std::uint8_t>(newest & 0xFFU) - launchLow);
        }
        HitLocation::Synthesized(attackerSlot, victim, beam, launch, point, headshot);
    }

    bool RemoteShotConfirmation::CoilAimFor(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 muzzle,
        OpenTK::Mathematics::Vector3& aim)
    {
        const std::int32_t slot = shooter.SlotIndex();
        const auto& players = Entities::PlayerEntity::Players();
        if (slot < 0 || slot >= static_cast<std::int32_t>(_coilVictim.size())
            || shooter.CurrentWeapon() != ::MphRead::BeamType::ShockCoil)
        {
            return false;
        }
        const auto s = static_cast<std::size_t>(slot);
        const std::int32_t victim = _coilVictim[s];
        if (victim < 0 || static_cast<std::size_t>(victim) >= players.size()
            || static_cast<std::int32_t>(NetSession::NetFrame() - _coilUntil[s]) >= 0)
        {
            return false;
        }
        const OpenTK::Mathematics::Vector3 to = static_cast<OpenTK::Mathematics::Vector3>(players[static_cast<std::size_t>(victim)]->Position)
            + _coilOffset[s] - muzzle;
        const float length = OpenTK::Mathematics::Length(to);
        const float scale = OpenTK::Mathematics::Length(aim);
        if (!(length > 0.0001F))
        {
            return false;
        }
        aim = OpenTK::Mathematics::Scale(to, (scale > 0.0001F ? scale : 1.0F) / length);
        return true;
    }

    std::uint32_t RemoteShotConfirmation::HoldFrames() noexcept
    {
        if (_confirmDelayCount < 16)
        {
            return 6;
        }
        std::array<std::uint8_t, 64> sorted = _confirmDelays;
        const auto used = static_cast<std::ptrdiff_t>(std::min(_confirmDelayCount, sorted.size()));
        std::sort(sorted.begin(), sorted.begin() + used);
        const std::uint32_t p90 = sorted[static_cast<std::size_t>((used - 1) * 9 / 10)];
        return std::clamp(p90 + 1U, 2U, 8U);
    }

    void RemoteShotConfirmation::NoteConfirmDelay(std::uint32_t frames) noexcept
    {
        _confirmDelays[_confirmDelayNext] = static_cast<std::uint8_t>(std::min(frames, 255U));
        _confirmDelayNext = (_confirmDelayNext + 1) % _confirmDelays.size();
        _confirmDelayCount++;
    }

    std::string RemoteShotConfirmation::DescribeConfirms()
    {
        return "confirmed impacts (hold " + std::to_string(HoldFrames()) + " frames): " + std::to_string(_confirmsInFlight) + " shots in the air brought in (turned "
            + NativeRuntime::ToString(_confirmsInFlight > 0 ? _homeAngleSum / static_cast<double>(_confirmsInFlight) : 0.0, "F1")
            + " deg on average, at most " + NativeRuntime::ToString(_homeAngleMax, "F1") + "), "
            + std::to_string(_confirmsAtSpawn) + " brought in from the moment they appeared, "
            + std::to_string(_impactsSynthesized) + " drawn on the spot (shot gone or never drawn), "
            + std::to_string(_seenAsBlast) + " already seen as the blast that reached them, "
            + std::to_string(_coilTicks) + " Shock Coil ticks aiming its beam, "
            + std::to_string(_unkeyedImpacts) + " older events drawn at the chest, "
            + std::to_string(_passedThrough) + " unconfirmed shots let through, "
            + std::to_string(_observedConfirms) + " of them on other players (observed), "
            + std::to_string(_confirmsIgnored) + " not applicable (continuous beam, own or unknown shooter)";
    }

}
