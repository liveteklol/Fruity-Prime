#include "AimCheck.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Testing/ServerSimFixture.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Entities/BeamEffectEntity.hpp"
#include "../../Scene.hpp"
#include "../../Features.hpp"
#include "../../GameState.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace MphRead::Mods::Diagnostics
{
    int AimCheck::CheckWeapons(Network::ServerSim& sim)
    {
        using OpenTK::Mathematics::Vector3;
        auto& player = *Entities::PlayerEntity::Players()[0];
        int checks = 0;
        const auto check = [&](bool passed, const char* name)
        {
            if (!passed) throw std::runtime_error(name);
            ++checks; std::cout << "AIM WEAPON PASS " << name << '\n';
        };
        player._aimFrame = {};
        player._disruptedTimer = 0;
        player._facingVector = player._gunVec1 = Vector3(0, 0, 1);
        player._gunVec2 = Vector3(1, 0, 0); player._upVector = Vector3(0, 1, 0);
        player._aimY = 0;
        player._ammo.fill(10000);
        for (int weapon = 0; weapon < 9; ++weapon)
        {
            for (bool charged : {false, true})
            for (bool zoom : {false, true})
            {
                std::cout << "AIM WEAPON case=" << weapon << " charged=" << charged << " zoom=" << zoom << '\n';
                check(player.TryEquipWeapon(static_cast<BeamType>(weapon), true), "weapon selection");
                player._equipInfo->ChargeLevel = charged ? player.EquipWeapon().FullCharge * 2 : 0;
                const bool instantArea = (charged && TestFlag(player.EquipWeapon().Flags, WeaponFlags::AoeCharged))
                    || (!charged && TestFlag(player.EquipWeapon().Flags, WeaponFlags::AoeUncharged));
                player._equipInfo->Zoomed = zoom;
                player.UpdateAimX(7); player.UpdateAimY(3);
                player.ProjectAimTarget();
                player.UpdateAimVecs();
                const auto muzzle = player._gunDrawPos + OpenTK::Mathematics::Multiply(player._aimVec, Fixed::ToFloat(player.Values().MuzzleOffset));
                check(OpenTK::Mathematics::LengthSquared(muzzle - player._muzzlePos) < 0.000001F,
                    "visual gun and muzzle remain separate geometric states");
                player._timeSinceShot = 10000;
                player._gunAnimation = Entities::GunAnimation::Idle;
                player._aimTrace.Clear();
                check(player.TryFireWeapon() && player._aimTrace.Get(Input::AimOperation::Shot) == 1,
                    "production shot actually spawns once");
                const auto expected = (player._aimTrace.LastTarget - player._aimTrace.LastMuzzle).Normalized();
                check(Vector3::Dot(expected, player._aimTrace.LastShot) > 0.99999F,
                    "shot direction uses target minus muzzle, independently of visual aim");
                bool projectile = false;
                bool spreadValid = true;
                const float spreadDegrees = static_cast<float>(charged ? player.EquipWeapon().ChargedSpread
                    : player.EquipWeapon().UnchargedSpread) / 4096.0F;
                const float spreadCos = std::cos(spreadDegrees * 3.14159265358979323846F / 180.0F);
                auto beams = Network::ServerSimFixture::RequireScene(sim).GetBeamProjectileEntities().GetEnumerator();
                while (beams.MoveNext())
                {
                    const auto& beam = beams.Current();
                    const bool current = beam->Type == EntityType::BeamProjectile && beam->Lifespan() > 0
                        && beam->Owner().get() == &player && beam->Beam() == static_cast<BeamType>(weapon)
                        && beam->Age() == 0
                        && OpenTK::Mathematics::LengthSquared(beam->SpawnPosition() - player._aimTrace.LastMuzzle) < 0.00001F;
                    if (current)
                    {
                        projectile = true;
                        const auto velocity = beam->Velocity();
                        spreadValid &= OpenTK::Mathematics::LengthSquared(velocity) > 0
                            && Vector3::Dot(expected, velocity.Normalized()) >= spreadCos - 0.00001F;
                    }
                }
                check(instantArea ? !projectile : projectile,
                    "flying type26 lifetime is distinct from immediate area attack");
                check(spreadValid, "actual projectile velocity stays within weapon spread cone around committed shot");
            }
        }
        const auto gun = player._gunVec1;
        for (int type : {0, 1, 3})
        {
            const auto effect = Entities::BeamEffectEntity::Create({type, false, OpenTK::Mathematics::IdentityMatrix()},
                &Network::ServerSimFixture::RequireScene(sim));
            check(type >= 3 ? effect == nullptr : effect && effect->Type == EntityType::BeamEffect,
                "type21 model effect and particle effect paths are distinct");
            check(OpenTK::Mathematics::Equal(gun, player._gunVec1), "effect registration does not rotate committed gun state");
        }
        player._equipInfo->Zoomed = false;
        auto& victim = *Entities::PlayerEntity::Players()[1];
        victim.ModPlaceAt(static_cast<Vector3>(player.Position) + Vector3(0, 0, 6));
        victim.SetHealth(99); victim._spawnInvulnTimer = victim._damageInvulnTimer = 0;
        victim.ModSetSpectating(false);
        player.CameraInfo()->Position = static_cast<Vector3>(player.Position)
            + Vector3(0, Fixed::ToFloat(player.Values().AimYOffset), 0);
        const auto target = static_cast<Vector3>(victim.Position) + Vector3(0, 0.5F, 0);
        player.ModSetAim((target - player.CameraInfo()->Position).Normalized());
        check(player.TryEquipWeapon(BeamType::PowerBeam, true), "collision fixture selects Power Beam");
        player.UpdateAimVecs(); player._timeSinceShot = 10000;
        player._gunAnimation = Entities::GunAnimation::Idle;
        check(player.TryFireWeapon(), "collision fixture actually launches");
        auto beams = Network::ServerSimFixture::RequireScene(sim).GetBeamProjectileEntities().GetEnumerator();
        while (beams.MoveNext())
        {
            const auto& beam = beams.Current();
            if (beam->Owner().get() != &player || beam->Beam() != BeamType::PowerBeam || beam->Age() != 0) continue;
            for (int step = 0; step < 60 && victim.Health() == 99; ++step)
                if (!beam->Process()) break;
        }
        check(victim.Health() < 99, "committed muzzle-to-target shot reaches real collision and damage");
        return checks;
    }

    int AimCheck::CheckHunterWeapons(const std::string& room)
    {
        int checks = 0;
        // Samus was covered in the caller. Rebuild real hunter models and
        // values for every other playable hunter, rather than relabeling one.
        for (std::uint8_t hunter = 1; hunter < 8; ++hunter)
        {
            auto roster = Network::RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1; roster.Count = 2;
            for (std::uint8_t slot = 0; slot < 8; ++slot)
            {
                (*roster.Slots)[slot] = slot; (*roster.Generations)[slot] = 1;
                (*roster.Hunters)[slot] = hunter; (*roster.Names)[slot] = "AIM WEAPON";
            }
            Network::ServerSim sim;
            if (!sim.Start(room, GameMode::Battle, 2, [](auto) {}, [] {},
                    Network::MakeNetSessionServerSimulationPort(), roster))
                throw std::runtime_error("hunter weapon scene could not start");
            try
            {
                Network::MatchStatePacket match{};
                match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
                match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
                Network::NetSession::ApplyMatchState(match, false);
                Network::NetSession::ApplyRoster(roster);
                for (int step = 0; step < 120; ++step) sim.Step();
                auto& player = *Entities::PlayerEntity::Players()[0];
                if (!player.ModIsInPlay() || static_cast<std::uint8_t>(player.Hunter()) != hunter)
                    throw std::runtime_error("hunter weapon fixture did not spawn expected hunter");
                player.SetIsBot(false); player._aimTrace.Enabled = true;
                Network::NetSession::Stop();
                Entities::PlayerEntity::SetMainPlayerIndex(0);
                std::cout << "AIM WEAPON hunter=" << static_cast<int>(hunter) << '\n';
                checks += CheckWeapons(sim);
                if (sim.StepFailures() != 0) throw std::runtime_error("hunter weapon simulation failure");
            }
            catch (...) { sim.Stop(); throw; }
            sim.Stop();
        }
        return checks;
    }
}
