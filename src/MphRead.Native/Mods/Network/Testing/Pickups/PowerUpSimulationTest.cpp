#include "Mods/Network/Testing/Pickups/PowerUpSimulationTest.hpp"

#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Testing/Harness/SimulationFixture.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "Program.hpp"
#include "Read.hpp"

#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::ItemSpawnEntity;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        void Expect(bool condition, const std::string& what)
        {
            if (!condition)
            {
                throw ProgramException(what);
            }
        }

        [[nodiscard]] PlayerEntity& PlayerAt(std::int32_t slot)
        {
            return *PlayerEntity::Players().at(static_cast<std::size_t>(slot));
        }

        // Where the test wants a player, the way a replicated puppet is put there.
        void Place(PlayerEntity& player, Vector3 position)
        {
            const Vector3 previous = player.Position;
            player.Position = position;
            player.SetPrevPosition(position);
            player.ModRefreshNodeRef(previous);
            player.ModRefreshVolume();
        }

        [[nodiscard]] PickupClaimPacket ClaimFor(std::int32_t slot, const ItemSpawnEntity& spawn, std::uint16_t id,
            std::uint32_t contact, Vector3 position)
        {
            PickupClaimPacket claim{};
            claim.MatchId = NetSession::CurrentMatchId();
            claim.AuthorityEpoch = NetSession::AuthorityEpoch();
            claim.SlotGeneration = NetPlayerLifecycle::Generation(slot);
            claim.LifeId = NetPlayerLifecycle::Get(slot);
            claim.ClaimId = id;
            claim.SpawnId = static_cast<std::int16_t>(spawn.Id);
            claim.SpawnCount = spawn.SpawnCount();
            claim.ContactFrame = contact;
            claim.Position = position;
            return claim;
        }

        // Sends one claim as a client would and returns the authority's answer.
        [[nodiscard]] PickupVerdictPacket::Entry Submit(std::int32_t slot, const PickupClaimPacket& claim)
        {
            std::vector<std::uint8_t> payload(1 + PickupClaimPacket::Size);
            payload[0] = 1;
            claim.Write(std::span(payload).subspan(1));
            std::optional<PickupVerdictPacket::Entry> answer;
            NetPickupClaims::VerdictSink([&](std::int32_t to, std::span<const PickupVerdictPacket::Entry> verdicts)
            {
                if (to == slot && !verdicts.empty())
                {
                    answer = verdicts.front();
                }
            });
            NetPickupClaims::Receive(slot, payload);
            NetPickupClaims::VerdictSink(nullptr);
            Expect(answer.has_value(), "a claim was not answered");
            return *answer;
        }

        [[nodiscard]] std::shared_ptr<ItemSpawnEntity> OutPowerUp()
        {
            for (const std::shared_ptr<ItemSpawnEntity>& candidate : NetItemState::RegisteredSpawns())
            {
                if (candidate->Active && candidate->Item() != nullptr && candidate->Item()->DespawnTimer() != 0
                    && PowerUpOf(candidate->Data().ItemType).has_value())
                {
                    return candidate;
                }
            }
            return nullptr;
        }
    }

    std::int32_t PowerUpSimulationTest::Run(const std::string& room)
    {
        ServerSim sim{};
        std::int32_t result = 1;
        try
        {
            const SessionStatePacket session = SimulationFixture::Session(room);
            const RosterPacket roster = SimulationFixture::Roster("PowerUp");
            std::vector<std::uint8_t> latest;
            Expect(sim.Start(room, GameMode::Battle, SimulationFixture::Players,
                [&latest](std::span<const std::uint8_t> payload) { latest.assign(payload.begin(), payload.end()); },
                []() {}, MakeNetSessionServerSimulationPort(), roster, session),
                "the authority could not load the room");
            // Occupied slots, as clients joining make them: players spawn.
            NetSession::ApplyRoster(roster);
            // Spawned, and on the authority's record for longer than any claim reaches back.
            for (std::int32_t i = 0; i < 180; i++)
            {
                sim.Step();
            }
            std::shared_ptr<ItemSpawnEntity> spawn{};
            for (std::int32_t i = 0; i < 1800 && spawn == nullptr; i++)
            {
                sim.Step();
                spawn = OutPowerUp();
            }
            if (spawn == nullptr)
            {
                Runtime::ConsoleWriteLine("[powerupsimtest] SKIP " + room + ": no power-up out in 30 seconds");
                sim.Stop();
                NetSession::Stop();
                Read::ClearCache();
                return 2;
            }
            const ::MphRead::ItemType type = spawn->Data().ItemType;
            const PowerUp kind = *PowerUpOf(type);
            const Vector3 item = spawn->Item()->Position;
            constexpr std::int32_t claimant = 1;
            constexpr std::int32_t rival = 2;
            Expect(PlayerAt(claimant).Health() > 0 && PlayerAt(rival).Health() > 0, "the test players are not alive");

            // Near enough for a claim, too far for the game's own pickup: only
            // the claim can take it. Stand there long enough to be on record.
            const Vector3 beside(item.X + 1.4F, item.Y - 0.65F, item.Z);
            for (std::int32_t i = 0; i < 10; i++)
            {
                Place(PlayerAt(claimant), beside);
                sim.Step();
            }
            Expect(spawn->Item() != nullptr && spawn->Item()->DespawnTimer() != 0,
                "the game picked it up from beside it; the test cannot tell the claim apart");
            const Vector3 stood = PlayerAt(claimant).Position;
            const std::uint32_t now = NetSession::NetFrame();
            const std::uint32_t contact = now - 6;

            Expect(Submit(claimant, ClaimFor(claimant, *spawn, 1, now - 500, stood)).Result
                == PickupVerdictPacket::ResultTooOld, "a contact long ago was taken");
            const std::uint8_t far = Submit(claimant, ClaimFor(claimant, *spawn, 2, contact,
                Vector3(item.X + 9.0F, item.Y, item.Z))).Result;
            Expect(far == PickupVerdictPacket::ResultOutOfReach,
                "a contact out of reach was answered: " + PickupVerdictPacket::Describe(far)
                + " (frame " + std::to_string(now) + ")");
            const std::optional<std::uint32_t> appeared = NetItemState::SpawnedAt(
                static_cast<std::int16_t>(spawn->Id), spawn->SpawnCount());
            Expect(appeared.has_value() && *appeared <= contact, "the spawn's appearance was not recorded");
            const std::uint8_t elsewhen = Submit(claimant, ClaimFor(claimant, *spawn, 6, contact,
                Vector3(item.X - 1.4F, item.Y - 0.65F, item.Z))).Result;
            Expect(elsewhen == PickupVerdictPacket::ResultOutOfReach,
                "a spot the claimant never stood on was taken: " + PickupVerdictPacket::Describe(elsewhen));
            PickupClaimPacket wrongSpawn = ClaimFor(claimant, *spawn, 3, contact, stood);
            wrongSpawn.SpawnCount = static_cast<std::uint16_t>(wrongSpawn.SpawnCount + 1);
            Expect(Submit(claimant, wrongSpawn).Result == PickupVerdictPacket::ResultGone,
                "a claim for another spawn was taken");

            const PickupClaimPacket claim = ClaimFor(claimant, *spawn, 4, contact, stood);
            const PickupVerdictPacket::Entry granted = Submit(claimant, claim);
            Expect(granted.Result == PickupVerdictPacket::ResultGranted && granted.Sequence == 1,
                "a good claim was refused: " + PickupVerdictPacket::Describe(granted.Result));
            Expect(spawn->Item() == nullptr || spawn->Item()->DespawnTimer() == 0, "a granted item stayed out");
            Expect(spawn->LastPickerSlot() == claimant, "the spawner does not know who took it");
            const std::uint16_t left = PlayerAt(claimant).ItemEffectFrames(type);
            Expect(left == PowerUpFrames - (now - contact),
                "the grant does not run from the contact: " + std::to_string(left) + " frames left");
            const std::uint16_t life = NetPlayerLifecycle::Get(claimant);
            Expect(NetPowerUps::RanAt(claimant, kind, contact, life) && !NetPowerUps::RanAt(claimant, kind, contact - 1, life)
                && !NetPowerUps::RanAt(claimant, kind, contact, static_cast<std::uint16_t>(life + 1)),
                "the ledger does not start at the contact");

            const PickupVerdictPacket::Entry again = Submit(claimant, claim);
            Expect(again.Result == PickupVerdictPacket::ResultGranted && again.Sequence == granted.Sequence
                && NetPowerUps::Current(claimant, kind).Sequence == granted.Sequence,
                "a resent claim was granted twice");
            const PickupVerdictPacket::Entry rivalAnswer = Submit(rival, ClaimFor(rival, *spawn, 1, contact,
                PlayerAt(rival).Position));
            Expect(rivalAnswer.Result == PickupVerdictPacket::ResultTaken || rivalAnswer.Result == PickupVerdictPacket::ResultOutOfReach,
                "a second claimant was granted the same item");

            for (std::int32_t i = 0; i < 2; i++)
            {
                sim.Step();
            }
            const SnapshotHeader header = SnapshotHeader::Read(latest);
            bool published = false;
            for (std::size_t i = 0; i < header.PlayerCount; i++)
            {
                const PlayerState state = PlayerState::Read(std::span<const std::uint8_t>(latest).subspan(
                    SnapshotHeader::Size + i * PlayerState::Size, PlayerState::Size));
                if (state.SlotIndex == claimant)
                {
                    const PowerUpReport report = state.PowerUps[IndexOf(kind)];
                    published = report.Sequence == granted.Sequence && report.Remaining > 0
                        && report.Remaining <= PowerUpFrames;
                }
            }
            Expect(published, "the snapshot does not carry the grant");

            // The authority's own sight of a contact: the next spawn, walked onto.
            const std::int32_t respawn = static_cast<std::int32_t>(spawn->Data().SpawnInterval) * 2 + 120;
            for (std::int32_t i = 0; i < respawn && (spawn->Item() == nullptr || spawn->Item()->DespawnTimer() == 0); i++)
            {
                sim.Step();
            }
            Expect(spawn->Item() != nullptr, "the power-up never came back: interval "
                + std::to_string(static_cast<std::int32_t>(spawn->Data().SpawnInterval)) + ", spawns "
                + std::to_string(spawn->SpawnCount()) + "/" + std::to_string(static_cast<std::int32_t>(spawn->Data().MaxSpawnCount))
                + (spawn->Active ? ", active" : ", inactive"));
            // Touched the frame before it came back: not this spawn.
            for (std::int32_t i = 0; i < 4; i++)
            {
                sim.Step();
            }
            const std::optional<std::uint32_t> back = NetItemState::SpawnedAt(
                static_cast<std::int16_t>(spawn->Id), spawn->SpawnCount());
            Expect(back.has_value(), "the respawn's appearance was not recorded");
            const std::uint8_t early = Submit(claimant, ClaimFor(claimant, *spawn, 7, *back - 1,
                PlayerAt(claimant).Position)).Result;
            Expect(early == PickupVerdictPacket::ResultGone,
                "a contact before the item came out was answered: " + PickupVerdictPacket::Describe(early));
            for (std::int32_t i = 0; i < 8 && spawn->LastPickerSlot() != rival; i++)
            {
                Place(PlayerAt(rival), Vector3(item.X, item.Y - 0.65F, item.Z));
                sim.Step();
            }
            Expect(spawn->LastPickerSlot() == rival && NetPowerUps::Current(rival, kind).Sequence == 1
                && PlayerAt(rival).ItemEffectFrames(type) > 0, "a contact the authority saw was not granted");
            // A death ends the claimant's grant there, and its next life has none.
            PlayerAt(claimant).TakeDamage(0, Entities::DamageFlags::Death, std::nullopt, nullptr);
            for (std::int32_t i = 0; i < 2; i++)
            {
                sim.Step();
            }
            Expect(!NetPowerUps::RanAt(claimant, kind, NetSession::NetFrame() - 1, life),
                "a dead player's grant still ran");
            Expect(sim.StepFailures() == 0, "the authority had failed steps");
            Runtime::ConsoleWriteLine("[powerupsimtest] PASS " + room + ": " + std::string(NameOf(kind))
                + " claimed from the contact, resend answered once, stale/far/elsewhere/early/other-spawn/rival refused, "
                + "ended by a death, "
                + "authority contact granted, published.");
            result = 0;
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("[powerupsimtest] FAIL " + room + ": "
                + Runtime::ExceptionToString(std::current_exception()));
            result = 1;
        }
        sim.Stop();
        NetSession::Stop();
        Read::ClearCache();
        return result;
    }
}
