#include "Mods/Diagnostics/HomingParityCheck.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/PlayerSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/CollisionDetection.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Combat/BeamObstacleTrace.hpp"
#include "Mods/Combat/WeaponVariant.hpp"
#include "GameState.hpp"
#include "Mods/Diagnostics/HomingCheckScene.hpp"
#include "Mods/Diagnostics/HomingReferenceFlight.hpp"
#include "Mods/Network/Diagnostics/HomingShotTrace.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::BeamFlags;
    using Entities::BeamProjectileEntity;
    using OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Length;
    using ::OpenTK::Mathematics::Normalize;
    using ::OpenTK::Mathematics::Scale;

    namespace
    {
        constexpr float FrameSeconds = 1.0F / 60.0F;
        // Float rounding over a second of flight: the 60 Hz path is the 30 Hz
        // path with the midpoint of each tick drawn between.
        constexpr float PathTolerance = 0.002F;
        // High above every room: free flight meets nothing but the target.
        const Vector3 SkyOrigin(0.0F, 400.0F, 0.0F);

        enum class FlightEvent : std::uint8_t
        {
            None,
            Target,
            Wall,
            Expired
        };

        [[nodiscard]] const char* Describe(FlightEvent value)
        {
            switch (value)
            {
            case FlightEvent::Target: return "target";
            case FlightEvent::Wall: return "wall";
            case FlightEvent::Expired: return "expired";
            default: return "none";
            }
        }

        [[nodiscard]] float AngleDegrees(Vector3 a, Vector3 b)
        {
            const float lengths = std::sqrt(Vector3::Dot(a, a) * Vector3::Dot(b, b));
            if (!(lengths > 0.0F))
            {
                return 0.0F;
            }
            return std::acos(std::clamp(Vector3::Dot(a, b) / lengths, -1.0F, 1.0F))
                * 180.0F / std::numbers::pi_v<float>;
        }

        using TargetPath = std::function<Vector3(float seconds)>;

        struct Flight final
        {
            std::string Name{};
            std::shared_ptr<WeaponInfo> Weapon{};
            std::uint16_t Charge = 0;
            Vector3 Origin = SkyOrigin;
            Vector3 Direction = Vector3(0.0F, 0.0F, 1.0F);
            TargetPath Target{};
            float Seconds = 1.5F;
            bool ExpectHoming = true;
        };

        struct Outcome final
        {
            FlightEvent Event = FlightEvent::None;
            float EventSeconds = -1.0F;
            float ReleaseSeconds = -1.0F;
            float TurnDegrees = 0.0F;
        };

        struct Comparison final
        {
            Outcome Native{};
            Outcome Reference{};
            float MaxDeviation = 0.0F;
            float MaxHeadingDegrees = 0.0F;
            std::int32_t Samples = 0;
            bool AcquiredAtSpawn = false;
            float HomingRaw = 0.0F;
            float SpeedRaw = 0.0F;
            float FinalSpeedRaw = 0.0F;
        };

        // A shot's own reference flight, with the scene's walls and the
        // target's body tested the way the projectile tests them.
        [[nodiscard]] std::optional<std::pair<FlightEvent, float>> ReferenceContact(
            Scene& scene, const HomingReferenceFlight& reference, const CollisionVolume& body,
            float cylinderRadius, bool hitsWalls)
        {
            std::optional<std::pair<FlightEvent, float>> first{};
            if (hitsWalls)
            {
                if (const auto wall = Combat::TraceFirstBeamObstacle(reference.Back(), reference.Position(), scene))
                {
                    first = std::pair{FlightEvent::Wall, wall->Collision.Distance};
                }
            }
            Formats::CollisionResult result{};
            if (Formats::CollisionDetection::CheckCylinderOverlapVolume(
                    &body, reference.Back(), reference.Position(), cylinderRadius, result)
                && (!first.has_value() || result.Distance < first->second))
            {
                first = std::pair{FlightEvent::Target, result.Distance};
            }
            return first;
        }

        [[nodiscard]] Comparison Fly(HomingCheckScene& fixture, const Flight& flight)
        {
            Scene& scene = fixture.Get();
            const std::shared_ptr<HomingCheckTarget> target = fixture.AddTarget(flight.Target(0.0F));
            const std::shared_ptr<BeamProjectileEntity> beam
                = fixture.Fire(flight.Weapon, flight.Charge, flight.Origin, flight.Direction);
            const HomingReferenceFlight::Spec spec = HomingReferenceFlight::FromWeapon(*flight.Weapon, flight.Charge);
            HomingReferenceFlight reference(spec, flight.Origin, flight.Direction);

            Comparison comparison{};
            comparison.AcquiredAtSpawn = beam->Target() == target;
            comparison.HomingRaw = spec.HomingRaw;
            comparison.SpeedRaw = spec.SpeedRaw;
            comparison.FinalSpeedRaw = spec.FinalSpeedRaw;
            const float cylinderRadius = beam->CylinderRadius();
            const bool hitsWalls = ::MphRead::TestFlag(beam->Flags(), BeamFlags::SurfaceCollision);
            const Vector3 nativeStart = beam->Velocity();
            const Vector3 referenceStart = reference.Velocity();
            bool tracking = comparison.AcquiredAtSpawn;
            const std::int32_t frames = static_cast<std::int32_t>(std::lround(flight.Seconds * 60.0F));
            for (std::int32_t frame = 1; frame <= frames; ++frame)
            {
                const float now = static_cast<float>(frame) * FrameSeconds;
                if (comparison.Native.Event == FlightEvent::None)
                {
                    target->MoveTo(flight.Target(now));
                    fixture.Step();
                    if (tracking && beam->Target() == nullptr)
                    {
                        tracking = false;
                        comparison.Native.ReleaseSeconds = now;
                    }
                    if (::MphRead::TestFlag(beam->Flags(), BeamFlags::Collided) || beam->Lifespan() <= 0.0F)
                    {
                        comparison.Native.Event = target->Hits() > 0 ? FlightEvent::Target
                            : beam->Lifespan() <= 0.0F && !::MphRead::TestFlag(beam->Flags(), BeamFlags::Collided)
                                ? FlightEvent::Expired : FlightEvent::Wall;
                        comparison.Native.EventSeconds = now;
                    }
                    else
                    {
                        comparison.Native.TurnDegrees = AngleDegrees(nativeStart, beam->Velocity());
                    }
                }
                if (frame % 2 != 0 || comparison.Reference.Event != FlightEvent::None)
                {
                    continue;
                }
                reference.Move();
                const CollisionVolume body(flight.Target(reference.Seconds()), 0.5F);
                if (const auto contact = ReferenceContact(scene, reference, body, cylinderRadius, hitsWalls))
                {
                    comparison.Reference.Event = contact->first;
                    comparison.Reference.EventSeconds
                        = (static_cast<float>(reference.Tick() - 1) + contact->second) * HomingReferenceFlight::TickSeconds;
                    continue;
                }
                if (comparison.AcquiredAtSpawn
                    && reference.Steer(flight.Target(reference.Seconds())) == HomingReferenceFlight::SteerResult::Released)
                {
                    comparison.Reference.ReleaseSeconds = reference.Seconds();
                }
                comparison.Reference.TurnDegrees = AngleDegrees(referenceStart, reference.Velocity());
                if (comparison.Native.Event == FlightEvent::None)
                {
                    const float deviation = Length(static_cast<Vector3>(beam->Position) - reference.Position());
                    comparison.MaxDeviation = std::max(comparison.MaxDeviation, deviation);
                    comparison.MaxHeadingDegrees = std::max(comparison.MaxHeadingDegrees,
                        AngleDegrees(beam->Velocity(), reference.Velocity()));
                    ++comparison.Samples;
                }
            }
            fixture.RemoveTarget(target);
            return comparison;
        }

        // A free flight's target: ahead at the distance the shot covers in
        // `ticks`, `degrees` off its line, drifting sideways.
        [[nodiscard]] TargetPath AheadOf(const Flight& flight, float ticks, float degrees, float drift)
        {
            const HomingReferenceFlight::Spec spec = HomingReferenceFlight::FromWeapon(*flight.Weapon, flight.Charge);
            const float meanSpeed = (spec.SpeedRaw + (spec.DecayTicks > 0 ? spec.FinalSpeedRaw : spec.SpeedRaw))
                / 2.0F / 4096.0F;
            const float distance = std::max(meanSpeed * ticks, 6.0F);
            const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
            const Vector3 start = flight.Origin
                + Vector3(std::sin(radians) * distance, 0.0F, std::cos(radians) * distance);
            const float driftSpeed = distance * drift;
            return [start, driftSpeed](float seconds) { return start + Vector3(driftSpeed * seconds, 0.0F, 0.0F); };
        }

        [[nodiscard]] std::shared_ptr<WeaponInfo> Row(const std::shared_ptr<const Weapons::WeaponList>& table,
            std::size_t index)
        {
            return Runtime::RequireReference(table).at(index);
        }
    }

    std::int32_t HomingParityCheck::Run(const std::string& room)
    {
        std::int32_t checks = 0;
        std::int32_t failures = 0;
        const auto check = [&](bool ok, const std::string& name)
        {
            ++checks;
            if (!ok)
            {
                ++failures;
            }
            Runtime::ConsoleWriteLine(std::string(ok ? "HOMING PASS " : "HOMING FAIL ") + name);
        };
        try
        {
            HomingCheckScene fixture(room);
            std::vector<Flight> flights{};
            const auto add = [&flights](std::string name, const std::shared_ptr<const Weapons::WeaponList>& table,
                std::size_t index, std::uint16_t charge, float ticks, float degrees, float drift,
                bool expectHoming = true)
            {
                Flight flight{};
                flight.Name = std::move(name);
                flight.Weapon = Row(table, index);
                flight.Charge = charge;
                flight.ExpectHoming = expectHoming;
                flight.Target = AheadOf(flight, ticks, degrees, drift);
                flights.push_back(std::move(flight));
            };
            add("PowerBeam MP:9 charge 35", Weapons::WeaponsMP, 9, 35, 14.0F, 9.0F, 0.15F, false);
            add("PowerBeam MP:9 charge 36", Weapons::WeaponsMP, 9, 36, 14.0F, 9.0F, 0.15F, false);
            add("PowerBeam MP:9 charge 37", Weapons::WeaponsMP, 9, 37, 14.0F, 9.0F, 0.15F);
            add("PowerBeam MP:9 charge 48", Weapons::WeaponsMP, 9, 48, 14.0F, 9.0F, 0.15F);
            add("PowerBeam MP:9 charge 60", Weapons::WeaponsMP, 9, 60, 14.0F, 9.0F, 0.15F);
            add("PowerBeam MP:0 charge 60 (no homing)", Weapons::WeaponsMP, 0, 60, 14.0F, 9.0F, 0.15F, false);
            add("Missile MP:11 charge 90 (accelerating)", Weapons::WeaponsMP, 11, 90, 16.0F, 10.0F, 0.15F);
            add("Missile MP:11 charge 0", Weapons::WeaponsMP, 11, 0, 16.0F, 10.0F, 0.15F, false);
            add("Missile MP:11 charge 89 (no homing)", Weapons::WeaponsMP, 11, 89, 16.0F, 10.0F, 0.15F, false);
            add("VoltDriver MP:10 charge 120 (decelerating)", Weapons::WeaponsMP, 10, 120, 16.0F, 10.0F, 0.15F);
            add("VoltDriver MP:10 charge 0", Weapons::WeaponsMP, 10, 0, 16.0F, 10.0F, 0.15F, false);
            add("VoltDriver MP:10 charge 119", Weapons::WeaponsMP, 10, 119, 16.0F, 10.0F, 0.15F, false);
            add("Missile 1P:11 charge 0", Weapons::Weapons1P, 11, 0, 16.0F, 10.0F, 0.15F);
            add("Missile 1P:11 charge 90", Weapons::Weapons1P, 11, 90, 16.0F, 10.0F, 0.15F);
            add("Enemy:0 charge 61", Weapons::EnemyWeapons, 0, 61, 16.0F, 10.0F, 0.15F);
            add("Enemy:2 charge 120", Weapons::EnemyWeapons, 2, 120, 16.0F, 10.0F, 0.15F);
            add("Enemy:8 charge 61", Weapons::EnemyWeapons, 8, 61, 16.0F, 10.0F, 0.15F);
            add("Boss:2 Cretaphid plasma", Weapons::BossWeapons, 2, 0, 16.0F, 10.0F, 0.15F);
            add("Boss:3 Slench Tear charge 0", Weapons::BossWeapons, 3, 0, 16.0F, 45.0F, 0.15F);
            add("Boss:3 Slench Tear charge 120", Weapons::BossWeapons, 3, 120, 16.0F, 45.0F, 0.15F);
            add("Boss:4 Slench beam charge 120", Weapons::BossWeapons, 4, 120, 16.0F, 10.0F, 0.15F);
            add("Gorea:1 Volt Driver", Weapons::GoreaWeapons, 1, 0, 16.0F, 10.0F, 0.15F);
            add("Platform:0 Sylux ship missile charge 120", Weapons::PlatformWeapons, 0, 120, 16.0F, 10.0F, 0.15F);

            // Turned behind: the target crosses to the shot's back half a
            // second in, which lets it go on the same tick in both flights.
            {
                Flight flight{};
                flight.Name = "Missile MP:11 charge 90, target turns behind";
                flight.Weapon = Row(Weapons::WeaponsMP, 11);
                flight.Charge = 90;
                const TargetPath ahead = AheadOf(flight, 40.0F, 5.0F, 0.0F);
                const Vector3 origin = flight.Origin;
                flight.Target = [ahead, origin](float seconds)
                {
                    return seconds >= 0.5F ? origin + Vector3(30.0F, 0.0F, -40.0F) : ahead(seconds);
                };
                flight.Seconds = 1.2F;
                flights.push_back(std::move(flight));
            }

            for (const Flight& flight : flights)
            {
                const Comparison result = Fly(fixture, flight);
                std::ostringstream line;
                line << "HOMING COMPARE " << flight.Name
                     << " | homing " << Runtime::ToString(result.HomingRaw, "F1")
                     << " speed " << Runtime::ToString(result.SpeedRaw, "F0") << "->"
                     << Runtime::ToString(result.FinalSpeedRaw, "F0")
                     << " | acquired " << (result.AcquiredAtSpawn ? "yes" : "no")
                     << " | max deviation " << Runtime::ToString(result.MaxDeviation, "F4")
                     << " u, heading " << Runtime::ToString(result.MaxHeadingDegrees, "F3") << " deg over "
                     << result.Samples << " ticks"
                     << " | first event 60Hz " << Describe(result.Native.Event) << "@"
                     << Runtime::ToString(result.Native.EventSeconds, "F4")
                     << "s, 30Hz " << Describe(result.Reference.Event) << "@"
                     << Runtime::ToString(result.Reference.EventSeconds, "F4") << "s"
                     << " | release 60Hz " << Runtime::ToString(result.Native.ReleaseSeconds, "F4")
                     << "s, 30Hz " << Runtime::ToString(result.Reference.ReleaseSeconds, "F4") << "s"
                     << " | turn 60Hz " << Runtime::ToString(result.Native.TurnDegrees, "F3")
                     << " deg, 30Hz " << Runtime::ToString(result.Reference.TurnDegrees, "F3") << " deg";
                Runtime::ConsoleWriteLine(line.str());

                check(result.AcquiredAtSpawn == flight.ExpectHoming,
                    flight.Name + ": target taken at spawn only when the row homes at that charge");
                check(result.Native.Event == result.Reference.Event,
                    flight.Name + ": the same thing is hit first");
                check(result.Native.Event == FlightEvent::None
                        || std::abs(result.Native.EventSeconds - result.Reference.EventSeconds)
                            <= FrameSeconds + 0.0005F,
                    flight.Name + ": first contact in the 60 Hz frame holding the reference's contact");
                check((result.Native.ReleaseSeconds < 0.0F) == (result.Reference.ReleaseSeconds < 0.0F)
                        && std::abs(result.Native.ReleaseSeconds - result.Reference.ReleaseSeconds)
                            <= FrameSeconds + 0.0005F,
                    flight.Name + ": a target behind is let go on the same steering tick");
                check(result.MaxDeviation <= PathTolerance,
                    flight.Name + ": path within " + Runtime::ToString(PathTolerance, "F4")
                        + " u of the reference at every tick");
            }

            {
                const auto previousPrime = GameState::PrimeHunter();
                GameState::PrimeHunter(0);
                const auto variant = Combat::ResolveWeaponVariantAtFire(BeamType::PowerBeam,
                    Hunter::Samus, Weapons::GetAffinityBeam(Hunter::Samus), false, true);
                Flight flight{};
                flight.Name = "Prime PowerBeam in flight across role handoff";
                flight.Weapon = Row(Weapons::WeaponsMP, variant.Index);
                flight.Charge = 60;
                const auto path = AheadOf(flight, 16.0F, 10.0F, 0.15F);
                bool handedOff = false;
                flight.Target = [&handedOff, path](float seconds)
                {
                    if (seconds >= 0.3F)
                    {
                        GameState::PrimeHunter(1);
                        handedOff = true;
                    }
                    return path(seconds);
                };
                const auto comparison = Fly(fixture, flight);
                GameState::PrimeHunter(previousPrime);
                check(handedOff && comparison.AcquiredAtSpawn
                        && (comparison.Native.Event == FlightEvent::None
                            || comparison.Native.EventSeconds >= 0.3F),
                    "a real Prime projectile remains airborne when its role changes owner");
                check(comparison.MaxDeviation <= PathTolerance,
                    "role handoff keeps the airborne projectile on its firing-time row trajectory");
                check(comparison.Native.Event == comparison.Reference.Event
                        && std::abs(comparison.Native.EventSeconds - comparison.Reference.EventSeconds)
                            <= FrameSeconds + 0.0005F,
                    "role handoff preserves the firing-time projectile's first contact");
            }

            // Selection on real entities: the best cosine, not the nearest;
            // nothing untargetable; never another target once one is gone.
            {
                const std::shared_ptr<WeaponInfo> missile = Row(Weapons::WeaponsMP, 11);
                const auto nearOffAxis = fixture.AddTarget(SkyOrigin + Vector3(1.8F, 0.0F, 8.0F));
                const auto farOnAxis = fixture.AddTarget(SkyOrigin + Vector3(0.2F, 0.0F, 30.0F));
                const auto dead = fixture.AddTarget(SkyOrigin + Vector3(0.0F, 0.0F, 12.0F));
                dead->SetTargetable(false);
                const auto shot = fixture.Fire(missile, 90, SkyOrigin, Vector3(0.0F, 0.0F, 1.0F));
                check(shot->Target() == farOnAxis,
                    "selection takes the target nearest the line of fire, not the nearest one, and skips the untargetable");
                farOnAxis->Retire();
                fixture.Step();
                fixture.Step();
                check(shot->Target() == nullptr,
                    "a target that leaves the scene in flight is dropped");
                for (std::int32_t i = 0; i < 10; ++i)
                {
                    fixture.Step();
                }
                check(shot->Target() == nullptr,
                    "a shot that lost its target never takes another (none re-acquired with two still ahead)");
                fixture.RemoveTarget(nearOffAxis);
                fixture.RemoveTarget(dead);
                for (std::int32_t i = 0; i < 180; ++i)
                {
                    fixture.Step();
                }
            }

            // Collision order against the room's own wall.
            if (const std::optional<HomingWallSite> wall = fixture.FindWall())
            {
                const Vector3 right(wall->Direction.Z, 0.0F, -wall->Direction.X);
                struct WallCase final
                {
                    std::string Name;
                    Vector3 At;
                    FlightEvent Expected;
                };
                const std::array<WallCase, 2> cases{{
                    {"target behind a wall: the wall stops both flights",
                        wall->From + Scale(wall->Direction, wall->Distance + 3.0F), FlightEvent::Wall},
                    {"target before a wall: the target is hit first in both flights",
                        wall->From + Scale(wall->Direction, wall->Distance * 0.5F) + Scale(right, 1.2F),
                        FlightEvent::Target},
                }};
                for (const auto& [name, at, expected] : cases)
                {
                    Flight flight{};
                    flight.Name = "Missile MP:11 charge 90, " + name;
                    flight.Weapon = Row(Weapons::WeaponsMP, 11);
                    flight.Charge = 90;
                    flight.Origin = wall->From;
                    flight.Direction = wall->Direction;
                    flight.Target = [at](float) { return at; };
                    const Comparison result = Fly(fixture, flight);
                    Runtime::ConsoleWriteLine("HOMING COMPARE " + flight.Name + " | 60Hz "
                        + Describe(result.Native.Event) + "@" + Runtime::ToString(result.Native.EventSeconds, "F4")
                        + "s, 30Hz " + Describe(result.Reference.Event) + "@"
                        + Runtime::ToString(result.Reference.EventSeconds, "F4") + "s");
                    check(result.Native.Event == expected && result.Reference.Event == expected
                            && std::abs(result.Native.EventSeconds - result.Reference.EventSeconds)
                                <= FrameSeconds + 0.0005F,
                        flight.Name);
                }
            }
            else
            {
                check(false, "no wall 14-30 units from a player spawn in " + room);
            }
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("HOMING FAIL " + Runtime::ExceptionToString(std::current_exception()));
            return 1;
        }
        Runtime::ConsoleWriteLine("HOMING " + std::string(failures == 0 ? "PASS " : "FAIL ")
            + std::to_string(checks - failures) + "/" + std::to_string(checks) + " checks");
        return failures == 0 ? 0 : 1;
    }
}
