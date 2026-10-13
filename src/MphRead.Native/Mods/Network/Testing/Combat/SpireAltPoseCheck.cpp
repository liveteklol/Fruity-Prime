#include "Mods/Network/Testing/Combat/SpireAltPoseCheck.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Testing/ServerSimFixture.hpp"
#include "Mods/Headless.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using OpenTK::Mathematics::Vector3;

    std::int32_t SpireAltPoseCheck::Run(const std::string& room)
    {
        std::string reason;
        if (!ServerSim::Available(reason))
        {
            Runtime::ConsoleWriteLine("SPIREPOSE FAIL " + reason);
            return 1;
        }
        ServerSim sim{};
        if (!sim.Start(room, GameMode::Battle, 2, [](std::span<const std::uint8_t>) {}, []() {},
                MakeNetSessionServerSimulationPort()))
        {
            return 1;
        }
        std::int32_t result = 1;
        try
        {
            MatchStatePacket match{};
            match.MatchId = 1; match.AuthorityEpoch = 1; match.RoomKey = room;
            match.Mode = static_cast<std::uint8_t>(GameMode::Battle);
            NetSession::ApplyMatchState(match, false);
            RosterPacket roster = RosterPacket::Create();
            roster.MatchId = 1; roster.AuthorityEpoch = 1; roster.Revision = 1;
            (*roster.Generations)[0] = 1;
            (*roster.Slots)[0] = 0;
            (*roster.Hunters)[0] = static_cast<std::uint8_t>(Hunter::Spire);
            (*roster.Colors)[0] = 0;
            (*roster.Pings)[0] = 0;
            (*roster.Names)[0] = "SPIREPOSE";
            roster.Count = 1;
            NetSession::ApplyRoster(roster);

            bool morphSent = false;
            bool attackSent = false;
            std::int32_t activeFrames = 0;
            std::int32_t movingFramesLeft = 0;
            std::int32_t movingFramesRight = 0;
            float maxLeftOffset = 0;
            float maxRightOffset = 0;
            float maxLeftChange = 0;
            float maxRightChange = 0;
            Vector3 firstLeft = Vector3::Zero;
            Vector3 firstRight = Vector3::Zero;
            Vector3 previousLeft = Vector3::Zero;
            Vector3 previousRight = Vector3::Zero;
            Entities::PlayerEntity& player = Runtime::RequireReference(Entities::PlayerEntity::Players()[0]);
            const auto spawned = [&player]() { return ::MphRead::TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned); };
            bool failed = false;
            int nativeSamples = 0;
            bool nativePoseOk = true;
            for (std::uint32_t frame = 1; frame <= 240; frame++)
            {
                IntentButtons buttons = spawned() && player.Health() > 0 ? IntentButtons::InPlayState : IntentButtons::None;
                if (morphSent || player.IsAltForm())
                {
                    buttons |= IntentButtons::AltFormState;
                }
                if (!morphSent && frame >= 30 && spawned())
                {
                    buttons |= IntentButtons::Morph;
                    morphSent = true;
                }
                else if (morphSent && !attackSent && player.IsAltForm() && !player.IsMorphing())
                {
                    buttons |= IntentButtons::AltAttack;
                    attackSent = true;
                }
                auto presses = std::make_shared<std::vector<std::uint32_t>>(IntentPacket::PressHistory);
                (*presses)[0] = static_cast<std::uint32_t>(buttons & (IntentButtons::Morph | IntentButtons::AltAttack));
                IntentPacket intent{};
                intent.MatchId = NetSession::CurrentMatchId();
                intent.AuthorityEpoch = NetSession::AuthorityEpoch();
                intent.SlotGeneration = NetPlayerLifecycle::Generation(0);
                intent.LifeId = NetPlayerLifecycle::Get(0);
                intent.Frame = frame;
                intent.Buttons = buttons;
                intent.Presses = presses;
                intent.Aim = Vector3(0.0F, 0.0F, 1.0F);
                intent.Position = player.Position;
                intent.WeaponSelect = 0xFF;
                intent.AmmoUa = 400;
                intent.AmmoMissiles = 50;
                NetSession::AcceptSlotIntent(0, intent);
                const bool wasAttacking = TestFlag(player.Flags2(), Entities::PlayerFlags2::AltAttack);
                const auto nextFrame = ServerSimFixture::RequireScene(sim).FrameCount(); // increments after entity processing
                const auto tick = Entities::DialancheNativeCollision::NativeTick(nextFrame);
                const auto prior = player._dialancheNativeCollision.PoseForHit(tick);
                const auto priorNext = player._dialancheNativeCollision.PoseForHit(tick + 1);
                sim.Step();
                if (sim.StepFailures() != 0)
                {
                    Runtime::ConsoleWriteLine("SPIREPOSE FAIL simulation step threw");
                    failed = true;
                    break;
                }
                if (!::MphRead::TestFlag(player.Flags2(), Entities::PlayerFlags2::AltAttack))
                {
                    continue;
                }
                const auto [left, right] = player.ModSpireAltCollisionPose();
                const auto sampled = player._dialancheNativeCollision.PoseForHit(tick);
                const auto nextSampled = player._dialancheNativeCollision.PoseForHit(tick + 1);
                if (Entities::DialancheNativeCollision::IsNativeCollisionStep(nextFrame))
                {
                    ++nativeSamples;
                    nativePoseOk &= OpenTK::Mathematics::Equal(nextSampled.Left, left) && OpenTK::Mathematics::Equal(nextSampled.Right, right);
                    if (wasAttacking) nativePoseOk &= OpenTK::Mathematics::Equal(sampled.Left, prior.Left) && OpenTK::Mathematics::Equal(sampled.Right, prior.Right);
                }
                else if (wasAttacking)
                {
                    nativePoseOk &= OpenTK::Mathematics::Equal(nextSampled.Left, priorNext.Left) && OpenTK::Mathematics::Equal(nextSampled.Right, priorNext.Right);
                }
                const Vector3 position = player.Position;
                const Vector3 localLeft = left - position;
                const Vector3 localRight = right - position;
                if (activeFrames == 0)
                {
                    firstLeft = localLeft;
                    firstRight = localRight;
                }
                else
                {
                    if (OpenTK::Mathematics::Length(localLeft - previousLeft) > 0.001F)
                    {
                        movingFramesLeft++;
                    }
                    if (OpenTK::Mathematics::Length(localRight - previousRight) > 0.001F)
                    {
                        movingFramesRight++;
                    }
                }
                previousLeft = localLeft;
                previousRight = localRight;
                activeFrames++;
                maxLeftOffset = std::max(maxLeftOffset, OpenTK::Mathematics::Length(localLeft));
                maxRightOffset = std::max(maxRightOffset, OpenTK::Mathematics::Length(localRight));
                maxLeftChange = std::max(maxLeftChange, OpenTK::Mathematics::Length(localLeft - firstLeft));
                maxRightChange = std::max(maxRightChange, OpenTK::Mathematics::Length(localRight - firstRight));
            }
            if (!failed)
            {
                const bool ok = Mods::Headless::Active() && morphSent && player.IsAltForm() && attackSent
                    && activeFrames >= 3 && movingFramesLeft >= 2 && movingFramesRight >= 2
                    && maxLeftOffset > 0.1F && maxRightOffset > 0.1F
                    && maxLeftChange > 0.05F && maxRightChange > 0.05F
                    && nativeSamples >= 2 && nativePoseOk;
                const auto text = [](bool value) { return value ? std::string("True") : std::string("False"); };
                Runtime::ConsoleWriteLine("SPIREPOSE " + std::string(ok ? "ok" : "FAIL") + " " + room
                    + " | headless " + text(Mods::Headless::Active()) + " | spawned " + text(spawned())
                    + " | morph sent " + text(morphSent) + " | morphed " + text(player.IsAltForm())
                    + " | attack sent " + text(attackSent) + " | active frames " + std::to_string(activeFrames)
                    + " | native samples " + std::to_string(nativeSamples) + " | native pose " + text(nativePoseOk)
                    + " | moving L/R " + std::to_string(movingFramesLeft) + "/" + std::to_string(movingFramesRight)
                    + " | offset L/R " + Runtime::ToString(maxLeftOffset, "0.000") + "/" + Runtime::ToString(maxRightOffset, "0.000")
                    + " | change L/R " + Runtime::ToString(maxLeftChange, "0.000") + "/" + Runtime::ToString(maxRightChange, "0.000"));
                result = ok ? 0 : 1;
            }
        }
        catch (...)
        {
            sim.Stop();
            throw;
        }
        sim.Stop();
        return result;
    }
}
