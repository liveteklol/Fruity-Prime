#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"

#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace MphRead::Mods::Network
{
    namespace
    {
        std::string Num(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.4f", static_cast<double>(value));
            return buffer;
        }

        std::int64_t WallMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // The shot key's frame as this machine knows it. The shooter and the
        // authority stamp the same one (the authority frame the shooter was
        // drawing); a remote shot drawn on a third machine carries the ack of
        // the intent that fired it, which is that same frame.
        std::uint32_t LaunchOf(const Entities::BeamProjectileEntity& beam)
        {
            return beam.ModShooterAck != 0 ? beam.ModShooterAck : beam.ModLaunchFrame;
        }
    }

    void HitLocation::Open(const std::string& path)
    {
        auto writer = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
        if (!writer->good())
        {
            std::cout << "[hitlog] could not open " << path << '\n';
            return;
        }
        *writer << "ev,wall_ms,frame,snap,auth,local,shooter,victim,beam,launch,splash,hs,dmg,"
                   "px,py,pz,vx,vy,vz,fx,fz,dy,hpct,ang,rad,alt,extra\n";
        _writer = std::move(writer);
        std::cout << "[hitlog] writing hit locations to " << path << '\n';
    }

    void HitLocation::Close()
    {
        if (_writer)
        {
            _writer->flush();
            _writer.reset();
        }
    }

    HitLocation::Body HitLocation::BodyOf(const Entities::PlayerEntity& victim, OpenTK::Mathematics::Vector3 point)
    {
        Body body{};
        const OpenTK::Mathematics::Vector3 position = victim.Position;
        const OpenTK::Mathematics::Vector3 d = point - position;
        const float minY = Fixed::ToFloat(victim.Values().MinPickupHeight);
        const float maxY = Fixed::ToFloat(victim.Values().MaxPickupHeight);
        body.Dy = d.Y;
        body.Pct = maxY > minY ? (d.Y - minY) * 100.0F / (maxY - minY) : 0.0F;
        body.Radius = std::sqrt(d.X * d.X + d.Z * d.Z);
        const OpenTK::Mathematics::Vector3 facing = victim.FacingVector();
        const float flat = std::sqrt(facing.X * facing.X + facing.Z * facing.Z);
        if (flat > 0.0001F && body.Radius > 0.0001F)
        {
            const float fx = facing.X / flat;
            const float fz = facing.Z / flat;
            // Right of a hunter facing +Z is -X in this game's handedness.
            const float ahead = d.X * fx + d.Z * fz;
            const float right = d.X * -fz + d.Z * fx;
            body.Angle = std::atan2(right, ahead) * 57.29578F;
        }
        return body;
    }

    void HitLocation::Row(const char* event, std::int32_t shooter, const Entities::PlayerEntity& victim,
        std::int32_t beam, std::uint32_t launch, bool splash, bool headshot, std::int32_t damage,
        OpenTK::Mathematics::Vector3 point, const std::string& extra)
    {
        if (!_writer)
        {
            return;
        }
        const Body body = BodyOf(victim, point);
        const OpenTK::Mathematics::Vector3 position = victim.Position;
        const OpenTK::Mathematics::Vector3 facing = victim.FacingVector();
        std::string line;
        line.reserve(256);
        line += event;
        line += ',' + std::to_string(WallMs());
        line += ',' + std::to_string(NetSession::NetFrame());
        line += ',' + std::to_string(NetSession::AppliedSnapshotFrame());
        line += NetSession::IsAuthority() || NetSession::IsHost() ? ",1" : ",0";
        line += ',' + std::to_string(NetSession::LocalSlot());
        line += ',' + std::to_string(shooter);
        line += ',' + std::to_string(victim.SlotIndex());
        line += ',' + std::to_string(beam);
        line += ',' + std::to_string(launch);
        line += splash ? ",1" : ",0";
        line += headshot ? ",1" : ",0";
        line += ',' + std::to_string(damage);
        line += ',' + Num(point.X) + ',' + Num(point.Y) + ',' + Num(point.Z);
        line += ',' + Num(position.X) + ',' + Num(position.Y) + ',' + Num(position.Z);
        line += ',' + Num(facing.X) + ',' + Num(facing.Z);
        line += ',' + Num(body.Dy) + ',' + Num(body.Pct) + ',' + Num(body.Angle) + ',' + Num(body.Radius);
        line += victim.IsAltForm() ? ",1" : ",0";
        line += ',' + extra;
        line += '\n';
        *_writer << line;
        const std::uint32_t now = NetSession::NetFrame();
        if (now - _lastFlush >= 60U)
        {
            _lastFlush = now;
            _writer->flush();
        }
    }

    void HitLocation::Contact(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
        OpenTK::Mathematics::Vector3 point, bool headshot, std::uint32_t damage, bool splash, bool blocked)
    {
        // A body already down on this machine takes no damage (TakeDamage
        // stops at zero health), so nothing is claimed for it either.
        if (!_writer || !NetSession::Active() || victim.Health() <= 0)
        {
            return;
        }
        Entities::PlayerEntity* owner = NetHitPrediction::OwnerOf(
            const_cast<Entities::BeamProjectileEntity*>(&beam));
        if (owner == nullptr || owner == &victim)
        {
            return;
        }
        Row("hit", owner->SlotIndex(), victim, static_cast<std::int32_t>(beam.Beam()), LaunchOf(beam), splash,
            headshot, static_cast<std::int32_t>(damage), point,
            std::to_string(beam.ModLaunchFrame) + (blocked ? ";1" : ";0"));
    }

    void HitLocation::NearMiss(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
        OpenTK::Mathematics::Vector3 point, float distance)
    {
        if (!_writer || !NetSession::Active())
        {
            return;
        }
        Entities::PlayerEntity* owner = NetHitPrediction::OwnerOf(
            const_cast<Entities::BeamProjectileEntity*>(&beam));
        if (owner == nullptr || owner == &victim)
        {
            return;
        }
        Row("miss", owner->SlotIndex(), victim, static_cast<std::int32_t>(beam.Beam()), LaunchOf(beam), false,
            false, 0, point, Num(distance));
    }

    void HitLocation::Claim(std::int32_t shooter, const Entities::PlayerEntity& victim, std::uint8_t beam,
        std::uint32_t launch, std::int32_t damage, std::uint8_t flags, OpenTK::Mathematics::Vector3 drawn,
        OpenTK::Mathematics::Vector3 history, bool historyKnown, std::int32_t verdict, const std::string& checks)
    {
        if (!_writer)
        {
            return;
        }
        // The point columns carry where the shooter drew the victim; extra
        // carries the authority's history for that frame and the verdict.
        const std::string extra = (historyKnown
                ? Num(history.X) + ';' + Num(history.Y) + ';' + Num(history.Z)
                : std::string("na;na;na"))
            + ';' + std::to_string(verdict) + ';' + std::to_string(flags) + (checks.empty() ? "" : ";" + checks);
        Row("claim", shooter, victim, beam, launch, false, (flags & 0x01U) != 0, damage, drawn, extra);
    }

    void HitLocation::Passed(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
        OpenTK::Mathematics::Vector3 point)
    {
        if (!_writer)
        {
            return;
        }
        Entities::PlayerEntity* owner = NetHitPrediction::OwnerOf(const_cast<Entities::BeamProjectileEntity*>(&beam));
        Row("pass", owner != nullptr ? owner->SlotIndex() : -1, victim, static_cast<std::int32_t>(beam.Beam()),
            LaunchOf(beam), false, false, 0, point, std::string());
    }

    void HitLocation::Synthesized(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
        std::uint32_t launch, OpenTK::Mathematics::Vector3 point, bool headshot, bool blast)
    {
        if (!_writer)
        {
            return;
        }
        // A blast already drawn is logged as the splash it was (no new impact).
        Row(blast ? "blast" : "hit", attacker, victim, beam, launch, blast, headshot, 0, point,
            std::to_string(launch) + (blast ? ";0;blast" : ";0;synth"));
    }

    void HitLocation::ObservedDamage(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
        std::int32_t damage, bool headshot)
    {
        if (!_writer)
        {
            return;
        }
        Row("odmg", attacker, victim, beam, 0, false, headshot, damage, victim.Position, std::string());
    }

    void HitLocation::Damage(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
        std::int32_t damage, bool headshot)
    {
        if (!_writer)
        {
            return;
        }
        Row("dmg", attacker, victim, beam, 0, false, headshot, damage, victim.Position, std::string());
    }

    std::array<HitLocation::Track, 8> HitLocation::_tracks{};
    std::array<HitLocation::JumpStats, 2> HitLocation::_jumps{};

    void HitLocation::Watch()
    {
        if (!NetSession::Active() || NetSession::IsAuthority() || NetSession::IsHost())
        {
            return;
        }
        const std::int32_t local = NetSession::LocalSlot();
        const auto& players = Entities::PlayerEntity::Players();
        const Entities::PlayerEntity* me = local >= 0 && static_cast<std::size_t>(local) < players.size()
            ? players[static_cast<std::size_t>(local)].get() : nullptr;
        // What this machine is firing: the weapon a rise or a jump happened under.
        const std::int32_t weapon = me != nullptr ? static_cast<std::int32_t>(me->CurrentWeapon()) : -1;
        for (std::size_t i = 0; i < players.size() && i < _tracks.size(); i++)
        {
            const Entities::PlayerEntity* player = players[i].get();
            Track& track = _tracks[i];
            if (player == nullptr || !::MphRead::TestFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                || !::MphRead::TestFlag(player->LoadFlags(), Entities::LoadFlags::Spawned))
            {
                track.Seen = false;
                continue;
            }
            const auto slot = static_cast<std::int32_t>(i);
            const std::uint16_t life = NetPlayerLifecycle::Get(slot);
            const std::int32_t health = player->Health();
            const OpenTK::Mathematics::Vector3 position = player->Position;
            if (!track.Seen || track.Life != life)
            {
                track = Track{true, life, health, 1, position, position};
                continue;
            }
            if (health != track.Health)
            {
                _healthChanges++;
                const std::string what = std::to_string(track.Health) + ';' + std::to_string(health) + ';'
                    + std::to_string(life);
                // A rise in the same life with nothing that heals in the room:
                // a bar that went down here and was put back.
                const bool rise = health > track.Health && track.Health > 0;
                if (rise)
                {
                    _healthRises++;
                }
                if (_writer)
                {
                    Row(rise ? "hpup" : "hp", -1, *player, weapon, 0, false, false, health - track.Health,
                        position, what);
                }
                track.Health = health;
            }
            if (health <= 0)
            {
                track.Samples = 0; // a body falling is not a jump
                track.P1 = track.P2 = position;
                continue;
            }
            if (track.Samples >= 2)
            {
                const OpenTK::Mathematics::Vector3 step = position - track.P1;
                const OpenTK::Mathematics::Vector3 before = track.P1 - track.P2;
                const float residual = OpenTK::Mathematics::Length(step - before);
                JumpStats& stats = _jumps[slot == local ? 0 : 1];
                stats.Samples++;
                stats.Over25 += residual > 0.25F ? 1 : 0;
                stats.Over50 += residual > 0.5F ? 1 : 0;
                stats.Over100 += residual > 1.0F ? 1 : 0;
                stats.Worst = std::max(stats.Worst, residual);
                if (residual > JumpThreshold && _writer)
                {
                    // How far the step turned from the one before (cosine):
                    // -1 is straight back the way the player came.
                    const float lengths = OpenTK::Mathematics::Length(step) * OpenTK::Mathematics::Length(before);
                    const float turn = lengths > 1e-6F
                        ? OpenTK::Mathematics::Vector3::Dot(step, before) / lengths : 1.0F;
                    Row("jump", -1, *player, weapon, 0, false, false, 0, position,
                        Num(residual) + ';' + Num(OpenTK::Mathematics::Length(step)) + ';'
                            + Num(OpenTK::Mathematics::Length(before)) + ';' + std::to_string(life) + ';' + Num(turn));
                }
            }
            track.P2 = track.P1;
            track.P1 = position;
            track.Samples++;
        }
    }

    void HitLocation::Placed(const Entities::PlayerEntity& player)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot >= 0 && static_cast<std::size_t>(slot) < _tracks.size())
        {
            _tracks[static_cast<std::size_t>(slot)].Samples = 0;
        }
        if (_writer)
        {
            Row("placed", -1, player, -1, 0, false, false, 0, player.Position, std::string());
        }
    }

    std::string HitLocation::DescribeWatch()
    {
        const auto line = [](const char* who, const JumpStats& stats)
        {
            return std::string(who) + " " + std::to_string(stats.Over25) + "/" + std::to_string(stats.Over50) + "/"
                + std::to_string(stats.Over100) + " of " + std::to_string(stats.Samples) + " (worst " + Num(stats.Worst) + ")";
        };
        return "rewind watch: steps off the motion >0.25/>0.5/>1.0: " + line("own", _jumps[0]) + ", "
            + line("others", _jumps[1]) + "; health changes " + std::to_string(_healthChanges) + ", rises "
            + std::to_string(_healthRises);
    }
}
