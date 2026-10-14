#include "Mods/Diagnostics/HomingDisplayCheck.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Diagnostics/HomingCheckScene.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotPresentation.hpp"
#include "Mods/Render/FrameTiming.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::BeamFlags;
    using Entities::BeamProjectileEntity;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Length;
    using ::OpenTK::Mathematics::Normalize;

    namespace
    {
        constexpr std::int32_t Steps = 75;
        constexpr std::array<std::int32_t, 4> DisplayRates{60, 144, 240, 540};
        const Vector3 ConfirmedOffset(0.0F, 1.0F, 0.0F);

        struct PhysicalSample final
        {
            Vector3 Position{};
            Vector3 Velocity{};
            BeamFlags Flags{};
        };

        [[nodiscard]] bool Same(Vector3 a, Vector3 b) noexcept
        {
            return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
        }

        // Bit for bit: a picture may not move the shot at all.
        [[nodiscard]] bool operator==(const PhysicalSample& a, const PhysicalSample& b) noexcept
        {
            return Same(a.Position, b.Position) && Same(a.Velocity, b.Velocity) && a.Flags == b.Flags;
        }

        struct DrawnRun final
        {
            std::vector<PhysicalSample> Physical{};
            std::vector<Vector3> Drawn{};
            std::int32_t Draws = 0;
            std::int32_t DrawsThatMoved = 0;
            float MaxDrawnFromPhysical = 0.0F;
            float StartAngleDegrees = 0.0F;
            bool Touched = false;
            Vector3 EndDrawn{};
            Vector3 EndPhysical{};
            bool Collided = false;
        };

        [[nodiscard]] PhysicalSample Sample(const BeamProjectileEntity& beam)
        {
            return PhysicalSample{static_cast<Vector3>(beam.Position), beam.Velocity(), beam.Flags()};
        }

        [[nodiscard]] Vector3 DrawnPosition(BeamProjectileEntity& beam)
        {
            const Network::ShotPresentation::DrawScope drawn(beam);
            return static_cast<Vector3>(beam.Position);
        }

        [[nodiscard]] float AngleDegrees(Vector3 a, Vector3 b)
        {
            const float lengths = std::sqrt(Vector3::Dot(a, a) * Vector3::Dot(b, b));
            return lengths > 0.0F
                ? std::acos(std::clamp(Vector3::Dot(a, b) / lengths, -1.0F, 1.0F)) * 57.29578F : 0.0F;
        }

        // One remote copy of a shot that misses on the shooter's ray, drawn
        // at `rate` pictures a second.
        [[nodiscard]] DrawnRun Fly(HomingCheckScene& fixture, std::int32_t rate, bool confirmed, bool retarget,
            Vector3 victim)
        {
            const Vector3 origin = victim + Vector3(-24.0F, 1.2F, 0.0F);
            const Vector3 physicalDirection = Normalize(victim + Vector3(0.0F, 1.2F, 1.8F) - origin);
            const Vector3 drawnDirection = Normalize(victim + ConfirmedOffset - origin);
            // A Power Beam shot: no splash, so a miss leaves the victim
            // exactly where it stood for the next run.
            const auto weapon = Runtime::RequireReference(Weapons::WeaponsMP).at(static_cast<std::size_t>(BeamType::PowerBeam));
            const std::shared_ptr<BeamProjectileEntity> beam = fixture.Fire(weapon, 0, origin, physicalDirection);
            // What the session gives a remote copy: the shooter's ack, then
            // a drawn flight along this screen's corrected ray, if any.
            beam->ModShooterAck = 1;
            beam->ModPresentation.Begin(*beam, origin, retarget ? std::optional<Vector3>(drawnDirection) : std::nullopt);
            if (confirmed)
            {
                beam->ModConfirmedTarget = true;
                beam->ModTargetSlot = 1;
                beam->ModConfirmedOffset = ConfirmedOffset;
            }

            DrawnRun run{};
            run.StartAngleDegrees = AngleDegrees(beam->ModPresentation.Velocity(), beam->Velocity());
            Render::FrameTiming::Reset();
            while (static_cast<std::int32_t>(run.Physical.size()) < Steps && beam->Lifespan() > 0.0F)
            {
                const std::int32_t taken = Render::FrameTiming::Advance(1.0 / rate);
                for (std::int32_t i = 0; i < taken && static_cast<std::int32_t>(run.Physical.size()) < Steps; ++i)
                {
                    fixture.Step();
                    run.Physical.push_back(Sample(*beam));
                    const Vector3 drawn = DrawnPosition(*beam);
                    run.Drawn.push_back(drawn);
                    if (!run.Collided)
                    {
                        run.MaxDrawnFromPhysical = std::max(run.MaxDrawnFromPhysical,
                            Length(drawn - static_cast<Vector3>(beam->Position)));
                    }
                    run.Collided = run.Collided || ::MphRead::TestFlag(beam->Flags(), BeamFlags::Collided);
                }
                // The picture: the beam's own draw, which wears the drawn
                // flight for its duration and must hand everything back.
                const PhysicalSample before = Sample(*beam);
                const Matrix4 transform = static_cast<Matrix4>(beam->Transform);
                beam->GetDrawInfo();
                ++run.Draws;
                const Matrix4 current = static_cast<Matrix4>(beam->Transform);
                if (!(Sample(*beam) == before) || std::memcmp(&current, &transform, sizeof(Matrix4)) != 0)
                {
                    ++run.DrawsThatMoved;
                }
            }
            run.Touched = beam->ModTouchedTarget;
            run.EndDrawn = run.Drawn.empty() ? Vector3::Zero : run.Drawn.back();
            run.EndPhysical = static_cast<Vector3>(beam->Position);
            return run;
        }

        [[nodiscard]] float MaxDistance(const std::vector<Vector3>& a, const std::vector<Vector3>& b)
        {
            if (a.size() != b.size())
            {
                return 1.0e9F;
            }
            float worst = 0.0F;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                worst = std::max(worst, Length(a[i] - b[i]));
            }
            return worst;
        }
    }

    std::int32_t HomingDisplayCheck::Run(const std::string& room)
    {
        std::int32_t checks = 0;
        std::int32_t failures = 0;
        const auto check = [&](bool ok, const std::string& name)
        {
            ++checks;
            failures += ok ? 0 : 1;
            Runtime::ConsoleWriteLine(std::string(ok ? "DISPLAY PASS " : "DISPLAY FAIL ") + name);
        };
        try
        {
            HomingCheckScene fixture(room);
            for (std::int32_t i = 0; i < 120; ++i)
            {
                fixture.Step();
            }
            PlayerEntity& victimPlayer = Runtime::RequireReference(
                Runtime::ManagedAt(PlayerEntity::Players(), 1));
            const Vector3 victim = static_cast<Vector3>(victimPlayer.Position);

            std::map<std::tuple<std::int32_t, bool, bool>, DrawnRun> runs{};
            for (const std::int32_t rate : DisplayRates)
            {
                for (const bool confirmed : {false, true})
                {
                    for (const bool retarget : {false, true})
                    {
                        victimPlayer.SetHealth(99);
                        const DrawnRun run = Fly(fixture, rate, confirmed, retarget, victim);
                        std::ostringstream line;
                        line << "DISPLAY " << rate << " fps | confirmed impacts " << (confirmed ? "on " : "off")
                             << " | retarget " << (retarget ? "on " : "off")
                             << " | " << run.Physical.size() << " steps, " << run.Draws << " pictures"
                             << " | drawn start " << Runtime::ToString(run.StartAngleDegrees, "F2")
                             << " deg off the physical ray, at most "
                             << Runtime::ToString(run.MaxDrawnFromPhysical, "F3") << " u from it"
                             << " | touched " << (run.Touched ? "yes" : "no")
                             << " | pictures that moved the shot " << run.DrawsThatMoved;
                        Runtime::ConsoleWriteLine(line.str());
                        runs.emplace(std::tuple{rate, confirmed, retarget}, run);
                        for (std::int32_t i = 0; i < 90; ++i)
                        {
                            fixture.Step();
                        }
                    }
                }
            }

            const DrawnRun& baseline = runs.at({60, false, false});
            for (const auto& [key, run] : runs)
            {
                const auto& [rate, confirmed, retarget] = key;
                const std::string name = std::to_string(rate) + " fps, confirmed "
                    + (confirmed ? "on" : "off") + ", retarget " + (retarget ? "on" : "off");
                check(run.DrawsThatMoved == 0, name + ": no picture moves the physical shot");
                check(run.Physical == baseline.Physical,
                    name + ": the physical flight is identical to 60 fps with both corrections off");
                const DrawnRun& sameAt60 = runs.at({60, confirmed, retarget});
                check(MaxDistance(run.Drawn, sameAt60.Drawn) == 0.0F,
                    name + ": the drawn flight is the same step for step as at 60 fps");
                if (!confirmed && !retarget)
                {
                    check(run.MaxDrawnFromPhysical < 0.00001F && run.StartAngleDegrees < 0.001F,
                        name + ": with nothing to correct, the drawn shot is the physical shot");
                }
                if (retarget)
                {
                    check(run.StartAngleDegrees > 1.0F,
                        name + ": the drawn shot leaves along this screen's corrected ray");
                }
                if (confirmed)
                {
                    check(run.Touched && Length(run.EndDrawn - (victim + ConfirmedOffset)) < 0.05F,
                        name + ": the drawn shot lands on the confirmed impact");
                }
                else
                {
                    check(!run.Touched && Length(run.EndDrawn - run.EndPhysical) < 0.00001F,
                        name + ": an unconfirmed drawn shot ends where the physical shot ends");
                }
            }
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("DISPLAY FAIL " + Runtime::ExceptionToString(std::current_exception()));
            return 1;
        }
        Runtime::ConsoleWriteLine("DISPLAY " + std::string(failures == 0 ? "PASS " : "FAIL ")
            + std::to_string(checks - failures) + "/" + std::to_string(checks) + " checks");
        return failures == 0 ? 0 : 1;
    }
}
