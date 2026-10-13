#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimOutbox.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimReplayLedger.hpp"
#include "Mods/Network/Gameplay/Pickups/PredictionBook.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using namespace MphRead::Mods::Network;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    struct FaultedPickupClaimPath final
    {
        static constexpr std::int32_t Slot = 1;

        PickupClaimOutbox ClientOutbox{};
        PickupClaimReplayLedger AuthorityReplay{};
        PredictionBook Predictions{};
        std::uint32_t Frame = 1000;
        std::uint16_t Life = 4;
        std::uint16_t LocalFrames = 900;
        std::int32_t AuthorityEvaluations = 0;

        void Advance(std::int32_t frames)
        {
            for (std::int32_t i = 0; i < frames; i++)
            {
                ClientOutbox.Tick([](std::uint16_t) {});
            }
            Frame += static_cast<std::uint32_t>(frames);
            LocalFrames = static_cast<std::uint16_t>(LocalFrames > frames ? LocalFrames - frames : 0U);
        }

        [[nodiscard]] std::uint16_t Predict(std::int16_t spawnId)
        {
            PickupClaimPacket claim{};
            claim.MatchId = 17;
            claim.AuthorityEpoch = 0x1020304050607080ULL;
            claim.SlotGeneration = 3;
            claim.LifeId = Life;
            claim.SpawnId = spawnId;
            claim.SpawnCount = 2;
            claim.ContactFrame = Frame;
            claim.Position = { 1.0F, 2.0F, 3.0F };
            const std::uint16_t id = ClientOutbox.Enqueue(claim);
            Predictions.Add(id, PowerUp::DoubleDamage, spawnId, claim.SpawnCount, Life, Frame, LocalFrames);
            LocalFrames = PowerUpFrames;
            return id;
        }

        [[nodiscard]] std::vector<PickupVerdictPacket::Entry> AuthorityReceive(
            std::span<const std::uint8_t> payload)
        {
            std::vector<PickupVerdictPacket::Entry> verdicts;
            const std::int32_t count = payload.empty() ? 0
                : std::min<std::int32_t>(payload[0], PickupClaimPacket::MaxPerPacket);
            for (std::int32_t i = 0; i < count; i++)
            {
                const auto offset = static_cast<std::size_t>(1 + i * PickupClaimPacket::Size);
                if (offset + PickupClaimPacket::Size > payload.size())
                {
                    break;
                }
                const PickupClaimPacket claim = PickupClaimPacket::Read(
                    payload.subspan(offset, PickupClaimPacket::Size));
                if (const PickupVerdictPacket::Entry* remembered = AuthorityReplay.Find(Slot, claim.ClaimId);
                    remembered != nullptr)
                {
                    verdicts.push_back(*remembered);
                    continue;
                }
                // The test's authority refuses both pickups; all retry and
                // replay behavior below comes from the production ledgers.
                const PickupVerdictPacket::Entry verdict{
                    claim.ClaimId, PickupVerdictPacket::ResultOutOfReach, 0 };
                AuthorityEvaluations++;
                AuthorityReplay.Remember(Slot, verdict);
                verdicts.push_back(verdict);
            }
            return verdicts;
        }

        void Deliver(PickupVerdictPacket::Entry verdict)
        {
            if (!ClientOutbox.Resolve(verdict.ClaimId))
            {
                return;
            }
            if (const auto rollback = Predictions.Settle(
                verdict.ClaimId, verdict.Result == PickupVerdictPacket::ResultGranted,
                verdict.Sequence, Frame, Life, true))
            {
                LocalFrames = rollback->Frames;
            }
        }

        [[nodiscard]] std::int32_t Compose(std::span<std::uint8_t> payload)
        {
            return ClientOutbox.Compose(payload, 8);
        }
    };
}

int main()
{
    using namespace MphRead::Mods::Network;
    bool passed = true;
    FaultedPickupClaimPath path{};
    path.Predictions.Seen(PowerUp::DoubleDamage, 3);
    const std::uint16_t first = path.Predict(10);
    path.Advance(10);
    const std::uint16_t second = path.Predict(11);
    std::array<std::uint8_t, 1 + PickupClaimPacket::Size * PickupClaimPacket::MaxPerPacket> payload{};

    passed &= Check(first == 1 && second == 2 && path.Compose(payload) > 0 && payload[0] == 2,
        "client sends both predicted pickups in one claim batch");
    // The first claim datagram is lost. The claims stay pending and are resent.
    path.Advance(8);
    const std::int32_t retrySize = path.Compose(payload);
    passed &= Check(retrySize > 0 && payload[0] == 2,
        "lost pickup claims are retried after the resend interval");
    std::vector<PickupVerdictPacket::Entry> lostVerdicts
        = path.AuthorityReceive(std::span<const std::uint8_t>(payload).first(static_cast<std::size_t>(retrySize)));
    passed &= Check(lostVerdicts.size() == 2 && path.AuthorityEvaluations == 2,
        "authority evaluates each retried claim once before its verdict is lost");

    // The retry also reaches the authority; the replay ledger returns the same
    // two refusals. Deliver the newer answer first and lose the older answer.
    path.Advance(8);
    const std::int32_t duplicateSize = path.Compose(payload);
    std::vector<PickupVerdictPacket::Entry> reorderedVerdicts
        = path.AuthorityReceive(std::span<const std::uint8_t>(payload).first(static_cast<std::size_t>(duplicateSize)));
    passed &= Check(duplicateSize > 0 && reorderedVerdicts.size() == 2
        && path.AuthorityEvaluations == 2
        && reorderedVerdicts[0].ClaimId == first
        && reorderedVerdicts[0].Result == PickupVerdictPacket::ResultOutOfReach,
        "duplicate claims replay their original refusal without reevaluation");
    if (reorderedVerdicts.size() == 2)
    {
        path.Deliver(reorderedVerdicts[1]);
        const std::uint16_t afterNewerRefusal = path.LocalFrames;
        path.Deliver(reorderedVerdicts[1]);
        passed &= Check(afterNewerRefusal == path.LocalFrames && path.ClientOutbox.PendingCount() == 1,
            "a duplicate newer verdict is ignored while the older claim remains pending");
    }

    path.Advance(8);
    const std::int32_t finalRetrySize = path.Compose(payload);
    std::vector<PickupVerdictPacket::Entry> finalVerdicts
        = path.AuthorityReceive(std::span<const std::uint8_t>(payload).first(static_cast<std::size_t>(finalRetrySize)));
    passed &= Check(finalRetrySize > 0 && payload[0] == 1 && finalVerdicts.size() == 1
        && finalVerdicts[0].ClaimId == first && path.AuthorityEvaluations == 2,
        "only the unanswered older claim retries and receives its cached refusal");
    if (!finalVerdicts.empty())
    {
        path.Deliver(finalVerdicts[0]);
        const std::uint16_t finalFrames = path.LocalFrames;
        path.Deliver(finalVerdicts[0]);
        passed &= Check(finalFrames == 866 && path.LocalFrames == finalFrames
            && path.ClientOutbox.PendingCount() == 0 && !path.Predictions.Awaiting(PowerUp::DoubleDamage),
            "reordered and duplicated refusals restore the old timer once after packet loss");
    }

    return passed ? 0 : 1;
}
