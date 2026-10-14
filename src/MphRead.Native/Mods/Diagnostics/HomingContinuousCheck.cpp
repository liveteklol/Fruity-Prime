#include "Mods/Diagnostics/HomingContinuousCheck.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Diagnostics/HomingCheckScene.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <cmath>
#include <functional>
#include <numbers>
#include <sstream>

namespace MphRead::Mods::Diagnostics
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::BeamProjectileEntity;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Normalize;
    using ::OpenTK::Mathematics::Scale;

    namespace
    {
        constexpr std::size_t PlainRow = 7;
        constexpr std::size_t AffinityRow = 16;

        struct Burst final
        {
            std::int32_t Frames = 0;
            std::int32_t Locked = 0;
            std::int32_t TargetDamage = 0;
            std::int32_t OwnerGain = 0;
        };

        [[nodiscard]] PlayerEntity& Target()
        {
            return Runtime::RequireReference(Runtime::ManagedAt(PlayerEntity::Players(), 1));
        }

        [[nodiscard]] Vector3 CentreOf(PlayerEntity& player)
        {
            Vector3 position{};
            player.GetPosition(position);
            return position;
        }

        [[nodiscard]] Vector3 RotateY(Vector3 value, float degrees)
        {
            const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
            return Vector3(value.X * std::cos(radians) + value.Z * std::sin(radians), value.Y,
                -value.X * std::sin(radians) + value.Z * std::cos(radians));
        }

        // The trigger held for `frames`: one beam a tick from `origin`,
        // `degrees` off the line to the target, which stands where `at` says.
        [[nodiscard]] Burst Hold(HomingCheckScene& fixture, std::size_t row, std::int32_t frames,
            const std::function<Vector3(std::int32_t)>& at, Vector3 origin, float degrees = 0.0F)
        {
            PlayerEntity& owner = fixture.Shooter();
            PlayerEntity& target = Target();
            const auto weapon = Runtime::RequireReference(Weapons::WeaponsMP).at(row);
            owner.SetHealth(40);
            target.SetHealth(99);
            Burst burst{};
            for (std::int32_t frame = 0; frame < frames; ++frame)
            {
                target.ModPlaceAt(at(frame));
                const Vector3 aim = RotateY(Normalize(CentreOf(target) - origin), degrees);
                const std::shared_ptr<BeamProjectileEntity> beam = fixture.Fire(weapon, 0, origin, aim);
                burst.Locked += beam->Target().get() == &target ? 1 : 0;
                fixture.Step();
                ++burst.Frames;
            }
            burst.TargetDamage = 99 - target.Health();
            burst.OwnerGain = owner.Health() - 40;
            return burst;
        }

        void Settle(HomingCheckScene& fixture, std::int32_t frames)
        {
            for (std::int32_t i = 0; i < frames; ++i)
            {
                fixture.Step();
            }
        }

        [[nodiscard]] bool Morph(HomingCheckScene& fixture, Vector3 at)
        {
            PlayerEntity& target = Target();
            for (std::int32_t i = 0; i < 90 && !target.IsAltForm(); ++i)
            {
                target.ModPlaceAt(at);
                const bool press = i == 0;
                target.Controls().Morph().SetIsDown(press);
                target.Controls().Morph().SetIsPressed(press);
                fixture.Step();
            }
            target.Controls().Morph().SetIsDown(false);
            target.Controls().Morph().SetIsPressed(false);
            return target.IsAltForm();
        }

        [[nodiscard]] std::string Describe(const std::string& name, const Burst& burst)
        {
            std::ostringstream line;
            line << "CONTINUOUS " << name << " | locked " << burst.Locked << "/" << burst.Frames
                 << " ticks | target lost " << burst.TargetDamage << " | shooter regained " << burst.OwnerGain;
            return line.str();
        }
    }

    std::int32_t HomingContinuousCheck::Run(const std::string& room)
    {
        std::int32_t checks = 0;
        std::int32_t failures = 0;
        const auto check = [&](bool ok, const std::string& name)
        {
            ++checks;
            failures += ok ? 0 : 1;
            Runtime::ConsoleWriteLine(std::string(ok ? "CONTINUOUS PASS " : "CONTINUOUS FAIL ") + name);
        };
        try
        {
            HomingCheckScene fixture(room);
            // Long enough for the target's spawn invulnerability to run out.
            Settle(fixture, 360);
            const Vector3 stand = static_cast<Vector3>(Target().Position);
            Runtime::ConsoleWriteLine("CONTINUOUS target slot 1: load " + Entities::ToString(Target().LoadFlags())
                + ", health " + std::to_string(Target().Health()) + ", invulnerable "
                + (Target().ModInvulnerable() ? "yes" : "no") + ", in play " + (Target().ModIsInPlay() ? "yes" : "no"));
            const Vector3 origin = stand + Vector3(-5.0F, 1.0F, 0.0F);
            const auto still = [stand](std::int32_t) { return stand; };

            const Burst plain = Hold(fixture, PlainRow, 45, still, origin);
            Runtime::ConsoleWriteLine(Describe("plain row MP:7, still target", plain));
            check(plain.Locked == plain.Frames, "plain Shock Coil locks the target on every tick");
            check(plain.TargetDamage > 0 && plain.OwnerGain == 0, "plain Shock Coil damages and drains nothing");
            Settle(fixture, 60);

            const Burst affinity = Hold(fixture, AffinityRow, 45, still, origin);
            Runtime::ConsoleWriteLine(Describe("affinity row MP:16, still target", affinity));
            check(affinity.Locked == affinity.Frames, "affinity Shock Coil locks the target on every tick");
            check(affinity.TargetDamage > 0 && affinity.OwnerGain > 0 && affinity.OwnerGain <= affinity.TargetDamage,
                "affinity Shock Coil drains back what it takes, never more");
            Settle(fixture, 60);

            const auto moving = [stand](std::int32_t frame)
            {
                const float t = static_cast<float>(frame) / 60.0F;
                return stand + Vector3(0.0F, 0.0F, 2.5F * std::sin(t * 3.0F));
            };
            const Burst followed = Hold(fixture, AffinityRow, 60, moving, origin);
            Runtime::ConsoleWriteLine(Describe("affinity row, target strafing", followed));
            check(followed.Locked == followed.Frames, "the lock follows a moving target on every tick");
            Settle(fixture, 60);
            const Burst plainMoving = Hold(fixture, PlainRow, 60, moving, origin);
            check(plainMoving.Locked == plainMoving.Frames && plainMoving.OwnerGain == 0,
                "the plain row follows a moving target and grants no drain");
            Settle(fixture, 60);

            const Burst wideStanding = Hold(fixture, PlainRow, 20, still, origin, 18.0F);
            Runtime::ConsoleWriteLine(Describe("18 deg off a standing target", wideStanding));
            check(wideStanding.Locked == wideStanding.Frames, "a standing target 18 deg off the beam is locked");
            Settle(fixture, 60);
            if (Morph(fixture, stand))
            {
                const Burst wideMorphed = Hold(fixture, PlainRow, 20, still, origin, 18.0F);
                Runtime::ConsoleWriteLine(Describe("18 deg off a morphed target", wideMorphed));
                check(wideMorphed.Locked == 0, "a morphed target 18 deg off the beam is not locked (narrower cone)");
                const Burst nearMorphed = Hold(fixture, PlainRow, 20, still, origin, 6.0F);
                Runtime::ConsoleWriteLine(Describe("6 deg off a morphed target", nearMorphed));
                check(nearMorphed.Locked == nearMorphed.Frames, "a morphed target 6 deg off the beam is locked");
                const Burst affinityWide = Hold(fixture, AffinityRow, 20, still, origin, 18.0F);
                check(affinityWide.Locked == 0 && affinityWide.OwnerGain == 0,
                    "the affinity row rejects a morphed target outside the narrow cone");
                const Burst affinityNear = Hold(fixture, AffinityRow, 20, still, origin, 6.0F);
                check(affinityNear.Locked == affinityNear.Frames,
                    "the affinity row locks a morphed target inside the narrow cone");
            }
            else
            {
                check(false, "the target could be put in its alt form");
            }
            Settle(fixture, 120);

            // An ally is never a target: the lock and a homing missile both
            // pass over a player of the shooter's own team.
            {
                const bool teams = GameState::Teams();
                const std::int32_t teamCount = GameState::TeamCount();
                const std::int32_t shooterTeam = fixture.Shooter().TeamIndex();
                const std::int32_t targetTeam = Target().TeamIndex();
                GameState::Teams(true);
                GameState::TeamCount(2);
                fixture.Shooter().SetTeamIndex(0);
                Target().SetTeamIndex(0);
                const Burst ally = Hold(fixture, PlainRow, 20, still, origin);
                Runtime::ConsoleWriteLine(Describe("plain row at a team-mate", ally));
                check(ally.Locked == 0, "a team-mate is never locked by the Shock Coil");
                const auto missile = Runtime::RequireReference(Weapons::WeaponsMP).at(11);
                const auto shot = fixture.Fire(missile, 90, origin, Normalize(CentreOf(Target()) - origin));
                check(shot->Target() == nullptr, "a charged homing missile does not take a team-mate");
                fixture.Step();
                Target().SetTeamIndex(1);
                Target().ModPlaceAt(stand);
                Target().SetHealth(99);
                const auto enemyShot = fixture.Fire(missile, 90, origin, Normalize(CentreOf(Target()) - origin));
                check(enemyShot->Target().get() == &Target(), "the same missile takes the player once on the other team");
                fixture.Shooter().SetTeamIndex(shooterTeam);
                Target().SetTeamIndex(targetTeam);
                GameState::Teams(teams);
                GameState::TeamCount(teamCount);
                Settle(fixture, 120);
            }

            if (const std::optional<HomingWallSite> wall = fixture.FindWall())
            {
                const Vector3 behind = wall->From + Scale(wall->Direction, wall->Distance + 1.5F) - Vector3(0.0F, 1.0F, 0.0F);
                const Burst shielded = Hold(fixture, AffinityRow, 30, [behind](std::int32_t) { return behind; },
                    wall->From);
                Runtime::ConsoleWriteLine(Describe("target behind a wall", shielded));
                check(shielded.TargetDamage == 0 && shielded.OwnerGain == 0,
                    "a target behind a wall takes no damage and gives no drain (the wall stops the beam)");
                check(shielded.Locked == shielded.Frames,
                    "the lock itself is taken through the wall, as the selection has no line of sight");
                const Burst plainShielded = Hold(fixture, PlainRow, 30,
                    [behind](std::int32_t) { return behind; }, wall->From);
                check(plainShielded.Locked == plainShielded.Frames
                        && plainShielded.TargetDamage == 0 && plainShielded.OwnerGain == 0,
                    "the plain row locks through the wall but neither damages nor drains");
            }
            else
            {
                check(false, "no wall 14-30 units from a player spawn in " + room);
            }
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("CONTINUOUS FAIL " + Runtime::ExceptionToString(std::current_exception()));
            return 1;
        }
        Runtime::ConsoleWriteLine("CONTINUOUS " + std::string(failures == 0 ? "PASS " : "FAIL ")
            + std::to_string(checks - failures) + "/" + std::to_string(checks) + " checks");
        return failures == 0 ? 0 : 1;
    }
}
