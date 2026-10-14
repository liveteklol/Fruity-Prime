// A demo's record of the recording player's own intent: the SlotIntent the
// server would have relayed, sections and all. A copy fires only the shot
// events its intent carries, so an intent cut at FullSize replayed that
// player with no shots at all -- missiles and every other projectile.
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "Formats/Enums.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace
{
    using MphRead::Mods::Network::DemoRecorder;
    using MphRead::Mods::Network::IntentPacket;
    using MphRead::Mods::Network::PacketType;

    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    bool passed = true;

    IntentPacket intent{};
    intent.Frame = 900;
    intent.SlotGeneration = 3;
    intent.LifeId = 2;
    intent.HasShot = true;
    intent.ShotOrigin = {1.0F, 2.0F, 3.0F};
    intent.ShotDirection = {0.0F, 0.0F, 1.0F};
    intent.ShotHistory[0] = {41, 896, static_cast<std::uint8_t>(MphRead::BeamType::Missile), 0, 880,
        {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F}};
    intent.ShotHistory[1] = {42, 900, static_cast<std::uint8_t>(MphRead::BeamType::Missile), 12, 884,
        {1.5F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F}};
    intent.ShotHistory[0].WeaponVariantIndex = intent.ShotHistory[0].WeaponId;
    auto& homingShot = intent.ShotHistory[1];
    homingShot.WeaponVariantIndex = static_cast<std::uint8_t>(homingShot.WeaponId
        + MphRead::Mods::Combat::BaseWeaponVariantCount);
    homingShot.ShotFlags = IntentPacket::ShotFlagPrimeHunter | IntentPacket::ShotFlagTargetResolved;
    homingShot.RoleFrame = 895;
    homingShot.Target.Kind = IntentPacket::ShotTargetKind::Player;
    homingShot.Target.Slot = 1;
    homingShot.Target.Generation = 5;
    homingShot.Target.LifeId = 6;
    intent.ShotHistoryLength = 2;
    intent.HasContinuousFire = true;
    intent.ContinuousPrimeHunter = true;
    intent.ContinuousRoleFrame = 897;
    intent.HasContinuousTarget = true;
    intent.ContinuousTarget = homingShot.Target;
    intent.ContinuousTarget.LifeId = 7;
    intent.HasBombs = true;
    intent.Bombs[0] = {7, {4.0F, 5.0F, 6.0F}, false};
    intent.BombsLength = 1;

    const std::vector<std::uint8_t> record = DemoRecorder::OwnIntentRecord(5, intent);
    passed &= Check(record.size() == 2 + intent.WireSize(),
        "the record is the whole wire form, not FullSize");
    passed &= Check(record.size() > 2 + static_cast<std::size_t>(IntentPacket::FullSize),
        "an intent carrying shots is longer than FullSize");
    passed &= Check(record[0] == static_cast<std::uint8_t>(PacketType::SlotIntent) && record[1] == 5,
        "it is recorded as the SlotIntent the server would relay for the slot");

    const IntentPacket read = IntentPacket::Read(std::span<const std::uint8_t>(record).subspan(2));
    passed &= Check(read.Frame == 900 && read.SlotGeneration == 3 && read.LifeId == 2,
        "the fixed block survives");
    passed &= Check(read.ShotHistoryLength == 2
        && read.ShotHistory[0].Sequence == 41 && read.ShotHistory[1].Sequence == 42
        && read.ShotHistory[1].WeaponId == static_cast<std::uint8_t>(MphRead::BeamType::Missile)
        && read.ShotHistory[1].Charge == 12 && read.ShotHistory[1].AckFrame == 884,
        "the shot events survive, so the replayed copy fires them");
    passed &= Check(read.ShotHistory[1].WeaponVariantIndex == homingShot.WeaponVariantIndex
        && read.ShotHistory[1].PrimeHunterAtFire() && read.ShotHistory[1].HasHomingDecision()
        && read.ShotHistory[1].RoleFrame == 895 && read.ShotHistory[1].Target == homingShot.Target,
        "firing variant, role frame and homing decision survive in the demo");
    passed &= Check(read.HasContinuousFire && read.ContinuousPrimeHunter
        && read.ContinuousRoleFrame == 897 && read.HasContinuousTarget
        && read.ContinuousTarget == intent.ContinuousTarget,
        "continuous fire, role frame and lock survive in the demo");
    passed &= Check(read.HasShot && read.ShotOrigin.Z == 3.0F && read.ShotDirection.Z == 1.0F,
        "the fired ray survives");
    passed &= Check(read.HasBombs && read.BombsLength == 1 && read.Bombs[0].Sequence == 7,
        "the bombs survive, so the replayed copy lays them");

    IntentPacket idle{};
    idle.Frame = 901;
    const std::vector<std::uint8_t> idleRecord = DemoRecorder::OwnIntentRecord(0, idle);
    passed &= Check(idleRecord.size() == 2 + idle.WireSize(),
        "an idle intent is recorded at its own (short) wire size");
    const IntentPacket idleRead = IntentPacket::Read(std::span<const std::uint8_t>(idleRecord).subspan(2));
    passed &= Check(idleRead.ShotHistoryLength == 0 && !idleRead.HasShot && !idleRead.HasBombs
        && !idleRead.HasContinuousFire && !idleRead.HasContinuousTarget && idleRead.ContinuousRoleFrame == 0,
        "an idle intent replays with nothing to fire");

    if (passed)
    {
        std::cout << "demo own-intent tests passed\n";
    }
    return passed ? 0 : 1;
}
