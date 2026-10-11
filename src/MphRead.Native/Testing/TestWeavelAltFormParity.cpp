#include "../Entities/Players/HalfturretEntity.hpp"
#include "../Formats/NodeData.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Mods/Gameplay/NativeGameplayClock.hpp"
#include "../Entities/Players/WeavelLungeInput.hpp"
#include "../Entities/Players/WeavelOwnedTurret.hpp"
#include "../Entities/Players/WeavelReplicaTransition.hpp"
#include "../Mods/Network/NetProtocol.hpp"
#include "../Mods/Network/NetHealthSync.hpp"
#include "../Mods/Network/NetMatchTimeSync.hpp"
#include "../Mods/Network/NetHealthSyncTest.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace
{
    int checks = 0;
    void Expect(bool ok, const char* reason)
    {
        if (!ok) throw std::runtime_error(reason);
        ++checks;
    }

    void FireRateAndClock()
    {
        using Rate = MphRead::Mods::Combat::HalfturretFireRate;
        using Clock = MphRead::Mods::Gameplay::NativeGameplayClock;
        Expect(Rate::Normal == 6144 && Rate::Floor == 2867, "EU1.1 raw constants");
        const std::array<std::uint32_t, 5> damage{0, 1, 12, 36, 100};
        const std::array<std::int32_t, 5> factor{6144, 6083, 5412, 3948, 2867};
        const std::array<std::uint32_t, 5> threshold{15, 15, 13, 10, 7};
        for (std::size_t i = 0; i < damage.size(); ++i)
        {
            Expect(Rate::AfterDamage(Rate::Normal, damage[i]) == factor[i], "damage raw arithmetic");
            Expect(Rate::Threshold(10, factor[i]) == threshold[i], "fixed rounded shot threshold");
        }
        Expect(Rate::AfterDamage(Rate::Normal, std::numeric_limits<std::uint32_t>::max())
            == Rate::Floor, "huge damage cannot wrap");
        Expect(Rate::AfterDamage(Rate::Floor, 1) == Rate::Floor, "floor remains clamped");
        auto raw = Rate::Floor;
        for (int i = 0; i < 53; ++i) raw = Rate::Recover(raw);
        Expect(raw == 6100, "53 native recovery ticks");
        Expect(Rate::Recover(raw) == Rate::Normal, "54th native tick clamps to normal");
        Expect(Rate::Recover(6206) == Rate::Normal, "above normal double truncation");
        Expect(Rate::Recover(Rate::Normal) == Rate::Normal, "normal remains stable");
        Expect(Rate::Threshold(1, Rate::Normal) == 7, "minimum shot threshold");
        Expect(!Clock::IsNativeTick(0), "initialization is not a gameplay tick");
        int ticks = 0;
        for (std::uint64_t frame = 1; frame <= 120; ++frame)
        {
            Expect(Clock::IsNativeTick(frame) == (frame % 2 == 0), "common scene phase");
            ticks += Clock::IsNativeTick(frame);
        }
        Expect(ticks == 60, "60 Hz scene has 30 Hz native cadence");
    }

    void InputAndProtocol()
    {
        using namespace MphRead::Mods::Network;
        MphRead::Entities::WeavelLungeInput input;
        input.Capture(true);
        Expect(!input.Consume(false), "odd input edge waits for native tick");
        input.Capture(false);
        Expect(input.Consume(true), "native tick consumes preserved input edge");
        Expect(!input.Consume(true), "hold or rejected edge never repeats");
        input.Capture(true); input.Reset();
        Expect(!input.Consume(true), "spawn reset discards previous input edge");

        // The owner's own turret: the authority's health, and the authority's
        // destruction -- never a turret it has not placed yet.
        {
            using Own = MphRead::Entities::WeavelOwnedTurret;
            Own own;
            Expect(own.Decide(true, true, true, false) == Own::Step::Keep,
                "a turret the authority has not placed yet is kept");
            Expect(own.Decide(true, true, true, true) == Own::Step::AdoptHealth, "a standing one takes its health");
            Expect(own.Decide(true, true, false, false) == Own::Step::Keep, "unmorphing there is not destruction");
            Expect(own.Decide(true, true, true, false) == Own::Step::Destroy, "gone while still alt is destroyed");
            Expect(own.Decide(true, false, true, false) == Own::Step::Keep, "and once is enough");
            Expect(own.Decide(false, false, false, false) == Own::Step::Keep
                && own.Decide(true, true, true, false) == Own::Step::Keep,
                "a new alt life waits for the authority's turret again");
        }

        // A remote Weavel's copy plays its transformations instead of snapping.
        using Step = MphRead::Entities::WeavelReplicaTransition::Step;
        MphRead::Entities::WeavelReplicaTransition replica;
        Expect(replica.Decide(true, false, false, false, true, 100) == Step::StartMorph, "replica Alt starts the morph");
        Expect(replica.Decide(true, false, true, false, true, 120) == Step::Wait, "and lets it play");
        Expect(replica.Decide(true, true, false, false, true, 141) == Step::Finalize && replica.Started(),
            "a finished morph is finalized and reported");
        replica.Close();
        Expect(replica.Decide(false, true, false, false, true, 300) == Step::StartUnmorph, "replica biped starts the unmorph");
        Expect(replica.Decide(false, false, false, true, true, 330) == Step::Wait, "and lets it play");
        Expect(replica.Decide(false, false, false, true, true, 390) == Step::Finalize, "a stalled unmorph snaps");
        replica.Close();
        Expect(replica.Decide(true, false, false, false, false, 500) == Step::Finalize, "a dead copy never starts one");
        Expect(replica.Decide(true, true, false, false, true, 510) == Step::Finalize && !replica.Started(),
            "an unchanged form is only finalized");
        // Protocol 17 added the 14 Weavel bytes; 19 adds 3 for the newest
        // damage event's confirmed impact: 54 + 4 x 15 + 14 + 3. 20 changes
        // only the intent (shot events); 21 the shot events and the claims;
        // 22 the meaning of two spare Weavel flag bits; 23 only the claim;
        // 24 only the intent (bombs); 25 only what a claim may name (a
        // player's hits on itself); 26 a claim's cause (a Death Alt).
        Expect(NetConfig::ProtocolVersion == 26 && PlayerState::Size == 131
            && PlayerState::Size - PlayerState::LegacySize == 17, "Weavel and the impact add 17 bytes per player");
        {
            // A transition under way on the authority is the form being
            // entered, so a watcher's copy starts its animation with it.
            PlayerState heading;
            heading.Flags = PlayerState::FlagActive | PlayerState::FlagSpawned;
            Expect(!heading.HeadingAlt(), "biped and still: biped");
            heading.WeavelFlags = PlayerState::WeavelFlagMorphing;
            Expect(heading.HeadingAlt(), "morphing: heading for Alt before FlagAltForm says so");
            heading.Flags |= PlayerState::FlagAltForm;
            heading.WeavelFlags = PlayerState::WeavelFlagUnmorphing;
            Expect(!heading.HeadingAlt(), "unmorphing: heading for biped while FlagAltForm still holds");
            std::vector<std::uint8_t> bytes(PlayerState::Size, 0);
            heading.Write(bytes);
            Expect(PlayerState::Read(bytes).WeavelFlags == PlayerState::WeavelFlagUnmorphing,
                "the transition flags round trip");
        }
        for (int mode = 0; mode < 3; ++mode)
        {
            PlayerState state;
            state.Flags = PlayerState::FlagActive | PlayerState::FlagSpawned
                | (mode < 2 ? PlayerState::FlagAltForm : 0);
            state.WeavelFlags = mode == 0 ? PlayerState::WeavelFlagTurretActive | PlayerState::WeavelFlagTurretGrounded : 0;
            state.HalfturretHealth = mode == 0 ? 50 : 0;
            state.HalfturretPosition = mode == 0 ? OpenTK::Mathematics::Vector3(1, 2, -3) : OpenTK::Mathematics::Vector3{};
            state.Health = 51; state.SlotGeneration = 7; state.LifeId = 9;
            state.DamageEventId = 1; state.Damage0.EventId = 1; state.Damage0.Damage = 12;
            state.Points = -5; state.Kills = 8; state.Deaths = 4;
            std::vector<std::uint8_t> bytes(PlayerState::Size + 1, 0xAC);
            state.Write(bytes);
            const auto replica = PlayerState::Read(bytes);
            Expect(replica.Flags == state.Flags && replica.WeavelFlags == state.WeavelFlags
                && replica.HalfturretHealth == state.HalfturretHealth
                && OpenTK::Mathematics::Equal(replica.HalfturretPosition, state.HalfturretPosition),
                "active turret, dead turret Alt, and biped round trip independently");
            Expect(replica.Health == 51 && replica.SlotGeneration == 7 && replica.LifeId == 9
                && replica.Damage0.Damage == 12 && replica.Points == -5 && replica.Kills == 8 && replica.Deaths == 4,
                "existing lifecycle, damage and score fields remain aligned");
            Expect(bytes.back() == 0xAC, "PlayerState does not overwrite following payload");
        }
        bool rejected = false;
        try { (void)PlayerState::Read(std::vector<std::uint8_t>(PlayerState::LegacySize)); }
        catch (const std::out_of_range&) { rejected = true; }
        Expect(rejected, "legacy protocol 16 player layout is rejected");
        constexpr auto end = SnapshotHeader::Size + 8 * PlayerState::Size + NetMatchTimeSync::Size;
        static_assert(end + NetHealthSync::HeaderSize + 1 < NetConfig::MaxPacketSize);
        std::vector<std::uint8_t> bytes(end + NetHealthSync::HeaderSize, 0);
        SnapshotHeader header; header.Frame = 456; header.PlayerCount = 8; header.Write(bytes);
        for (std::uint8_t i = 0; i < 8; ++i)
        {
            PlayerState state; state.SlotIndex = i; state.HalfturretHealth = i + 1;
            state.HalfturretPosition = {static_cast<float>(i), 1, 2};
            state.Write(std::span(bytes).subspan(SnapshotHeader::Size + i * PlayerState::Size));
        }
        for (std::uint8_t i = 0; i < 8; ++i)
        {
            auto state = PlayerState::Read(std::span(bytes).subspan(SnapshotHeader::Size + i * PlayerState::Size));
            Expect(state.SlotIndex == i && state.HalfturretHealth == i + 1
                && state.HalfturretPosition.X == i, "all eight players have independent wire boundaries");
        }
        Expect(SnapshotHeader::Read(bytes).Frame == 456 && bytes[end] == 0, "snapshot header and health tail remain aligned");
        // 16 before protocol 19; the 3 bytes of confirmed impact per player
        // (24 at eight) leave 13 -- a pickup cycle of 56 spawns takes five
        // snapshots instead of four.
        Expect(NetHealthSync::PacketEntries(NetConfig::MaxPacketSize - 1 - end) == 13,
            "eight-player packet reserves bounded health capacity without omitting players");
    }
}

namespace MphRead::Entities
{
    // Exercise the real reused object's reset and damage hooks without game assets.
    class WeavelAltFormParityTest final
    {
    public:
        static void Run()
        {
            using Rate = Mods::Combat::HalfturretFireRate;
            auto turret = std::make_shared<HalfturretEntity>(nullptr, nullptr);
            const auto equip = turret->EquipInfo();
            // Current is selected by room loading; asset-free tests bind the MP table directly.
            const auto weapon = Weapons::WeaponsMP->at(3);
            equip->Weapon = weapon;
            const auto defaultDamage = equip->UnchargedDamage();
            const auto defaultHeadshot = equip->HeadshotDamage();
            const auto defaultSplash = equip->SplashDamage();
            const auto defaultMinSplash = equip->MinChargeSplashDamage();
            const auto defaultChargedSplash = equip->ChargedSplashDamage();
            turret->OnTakeDamage(turret, 36);
            Expect(turret->CooldownFactorRaw() == 3948 && turret->NativeShotThreshold() == 10,
                "production damage hook and threshold");
            Expect(turret->_target == turret && turret->_targetTimer == 30, "native retaliation timer");
            turret->_closestNode = std::make_shared<Formats::NodeData3>(Vector3Fx{});
            turret->_health = 50;
            turret->_timeSinceDamage = 2;
            turret->_timeSinceFrozen = 61;
            turret->OnFrozen();
            Expect(turret->_freezeTimer == 75, "long freeze uses native timer");
            turret->_timeSinceFrozen = 0;
            turret->_freezeTimer = 0;
            turret->OnFrozen();
            Expect(turret->_freezeTimer == 15, "short freeze uses native timer");
            turret->_burnTimer = 150;
            turret->_ySpeed = -0.7F;
            turret->_grounded = true;
            turret->_aimVector = {1, 2, 3};
            turret->_cooldownTimer = 65;
            equip->UnchargedDamage(3);
            equip->HeadshotDamage(3);
            equip->SplashDamage(3);
            equip->MinChargeSplashDamage(3);
            equip->ChargedSplashDamage(3);
            equip->DmgDirTypes = {0, 0};
            equip->ChargeLevel = 20;
            equip->SmokeLevel = 7;
            equip->InfiniteAmmo = true;
            equip->GetAmmo = [] { return 999; };
            equip->SetAmmo = [](int) {};
            turret->ResetForSpawn();
            Expect(turret->_target == nullptr && turret->_closestNode == nullptr,
                "reset clears previous life references");
            Expect(turret->_health == 0 && turret->_timeSinceDamage == 65535
                && turret->_timeSinceFrozen == 0 && turret->_freezeTimer == 0
                && turret->_burnTimer == 0 && turret->_burnEffect == nullptr, "reset clears damage and afflictions");
            Expect(turret->_ySpeed == 0 && !turret->_grounded
                && OpenTK::Mathematics::IsZero(turret->_aimVector), "reset clears physical state");
            Expect(turret->_targetTimer == 0 && turret->_cooldownTimer == 0
                && turret->CooldownFactorRaw() == Rate::Normal, "reset clears native combat state");
            Expect(equip == turret->EquipInfo() && equip->Weapon == nullptr && equip->Beams == nullptr
                && equip->ChargeLevel == 0 && equip->SmokeLevel == 0 && !equip->InfiniteAmmo
                && !equip->GetAmmo && !equip->SetAmmo, "fresh EquipInfo without replacing reused object");
            Expect(equip->DmgDirTypes == std::array<std::uint8_t, 2>{255, 255}, "reset damage direction overrides");
            equip->Weapon = weapon; // same binding Initialize performs after reset
            Expect(equip->UnchargedDamage() == defaultDamage && equip->HeadshotDamage() == defaultHeadshot
                && equip->SplashDamage() == defaultSplash && equip->MinChargeSplashDamage() == defaultMinSplash
                && equip->ChargedSplashDamage() == defaultChargedSplash, "Story damage overrides do not survive reset");
            Expect(weapon->UnchargedDamage == defaultDamage, "shared Battlehammer metadata remains unchanged");
            Expect(turret->NativeShotThreshold() == 15, "new life starts at normal shot threshold");
            turret->ResetForSpawn();
            Expect(turret->CooldownFactorRaw() == Rate::Normal && turret->Health() == 0,
                "repeated reset is stable");
        }
    };
}

int main()
{
    try
    {
        FireRateAndClock();
        InputAndProtocol();
        MphRead::Mods::Network::NetHealthSyncTest::Run();
        MphRead::Entities::WeavelAltFormParityTest::Run();
        std::printf("WeavelAltFormParity PASS %d checks (fire rate, cadence, reused turret reset)\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "WeavelAltFormParity FAIL: %s\n", error.what());
        return 1;
    }
}
