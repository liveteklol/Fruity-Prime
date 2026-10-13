#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"

#include "Mods/Network/Gameplay/Claims/Common/ClaimResend.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Pickups/PickupReach.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace MphRead::Mods::Network
{
    PickupClaimOutbox NetPickupClaims::_outbox{};
    PickupClaimReplayLedger NetPickupClaims::_replayLedger{};

    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] Entities::PlayerEntity* PlayerAt(std::int32_t slot)
        {
            const auto& players = Entities::PlayerEntity::Players();
            return slot >= 0 && static_cast<std::size_t>(slot) < players.size()
                ? players[static_cast<std::size_t>(slot)].get() : nullptr;
        }

        [[nodiscard]] PickupVerdictPacket::Entry Verdict(std::uint16_t claimId, std::uint8_t result,
            std::uint8_t sequence = 0)
        {
            return PickupVerdictPacket::Entry{ claimId, result, sequence };
        }

        [[nodiscard]] bool Finite(OpenTK::Mathematics::Vector3 value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }
    }

    bool NetPickupClaims::Claiming()
    {
        return NetSession::Active() && !NetSession::IsAuthority() && !NetSession::IsHost()
            && NetSession::LocalSlot() >= 0;
    }

    std::uint16_t NetPickupClaims::Declare(Entities::PlayerEntity& player, const Entities::ItemSpawnEntity& spawner)
    {
        if (!Claiming() || player.SlotIndex() != NetSession::LocalSlot())
        {
            return 0;
        }
        const std::int32_t slot = player.SlotIndex();
        PickupClaimPacket claim{};
        claim.MatchId = NetSession::CurrentMatchId();
        claim.AuthorityEpoch = NetSession::AuthorityEpoch();
        claim.SlotGeneration = NetPlayerLifecycle::Generation(slot);
        claim.LifeId = NetPlayerLifecycle::Get(slot);
        claim.SpawnId = static_cast<std::int16_t>(spawner.Id);
        claim.SpawnCount = spawner.SpawnCount();
        // The world this machine was showing: the frame a shot fired now
        // would be launched in, so the grant and the shots after it agree.
        claim.ContactFrame = NetUnlagged::LaunchFrameFor(player);
        claim.Position = player.Position;
        return _outbox.Enqueue(claim);
    }

    std::int32_t NetPickupClaims::Compose(std::span<std::uint8_t> dest)
    {
        if (dest.empty())
        {
            return 0;
        }
        const std::int32_t slot = NetSession::LocalSlot();
        const std::int32_t interval = ClaimResendInterval(SessionSlots().PingAt(slot));
        return _outbox.Compose(dest, interval);
    }

    void NetPickupClaims::ApplyVerdicts(std::span<const std::uint8_t> payload)
    {
        if (payload.size() < static_cast<std::size_t>(PickupVerdictPacket::HeaderSize)
            || !NetSession::MatchesStream(Runtime::ReadUInt16LittleEndian(payload.subspan(1, 2)),
                Runtime::ReadUInt64LittleEndian(payload.subspan(3, 8)))
            || !NetPlayerLifecycle::Matches(NetSession::LocalSlot(), Runtime::ReadUInt16LittleEndian(payload.subspan(11, 2)),
                Runtime::ReadUInt16LittleEndian(payload.subspan(13, 2))))
        {
            return;
        }
        const std::int32_t count = std::min<std::int32_t>(payload[0], PickupVerdictPacket::MaxPerPacket);
        for (std::int32_t i = 0; i < count; i++)
        {
            const auto at = static_cast<std::size_t>(PickupVerdictPacket::HeaderSize + i * PickupVerdictPacket::EntrySize);
            if (at + PickupVerdictPacket::EntrySize > payload.size())
            {
                break;
            }
            const std::uint16_t id = Runtime::ReadUInt16LittleEndian(payload.subspan(at, 2));
            const std::uint8_t result = payload[at + 2];
            const std::uint8_t sequence = payload[at + 3];
            if (_outbox.Resolve(id))
            {
                if (result < _results.size())
                {
                    _results[result]++;
                }
                NetPowerUpPrediction::Settle(id, result == PickupVerdictPacket::ResultGranted, sequence);
                if (NetLog::Enabled() && result != PickupVerdictPacket::ResultGranted)
                {
                    NetLog::Event("[powerup] claim " + std::to_string(id) + " refused: "
                        + PickupVerdictPacket::Describe(result));
                }
            }
        }
    }

    bool NetPickupClaims::Reachable(std::int32_t slot, const PickupClaimPacket& claim,
        const Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 item)
    {
        if (!Finite(claim.Position) || !WithinPickupReach(claim.Position, item))
        {
            return false;
        }
        // Where the claimant says it stood has to be somewhere this authority
        // had them; without a history to ask, where it has them now.
        if (NetUnlagged::Enabled())
        {
            const std::int32_t ping = SessionSlots().PingAt(slot);
            const FrameWindow window = ContactArrivalWindow(claim.ContactFrame, ping, NetSession::NetFrame());
            return NetUnlagged::PassedNear(slot, claim.SlotGeneration, claim.LifeId, claim.Position,
                CorroborationRadius, window.From, window.To);
        }
        const OpenTK::Mathematics::Vector3 between = static_cast<OpenTK::Mathematics::Vector3>(player.Position) - claim.Position;
        return OpenTK::Mathematics::Vector3::Dot(between, between) <= 4.0F * CorroborationRadius * CorroborationRadius;
    }

    PickupVerdictPacket::Entry NetPickupClaims::Judge(std::int32_t slot, const PickupClaimPacket& claim)
    {
        const std::uint32_t now = NetSession::NetFrame();
        if (claim.ContactFrame == 0 || claim.ContactFrame > now || now - claim.ContactFrame > MaxContactAge)
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultTooOld);
        }
        const std::shared_ptr<Entities::ItemSpawnEntity> spawner = NetItemState::Find(claim.SpawnId);
        const std::optional<PowerUp> kind = spawner != nullptr ? PowerUpOf(spawner->Data().ItemType) : std::nullopt;
        if (!kind.has_value())
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultNotClaimable);
        }
        Entities::PlayerEntity* player = PlayerAt(slot);
        if (player == nullptr || player->Health() <= 0)
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultWrongLife);
        }
        if (spawner->SpawnCount() != claim.SpawnCount)
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultGone);
        }
        // Touched before this spawn came out: not this item.
        const std::optional<std::uint32_t> appeared = NetItemState::SpawnedAt(claim.SpawnId, claim.SpawnCount);
        if (!appeared.has_value() || claim.ContactFrame < *appeared)
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultGone);
        }
        const std::shared_ptr<Entities::ItemInstanceEntity> item = spawner->Item();
        if (item == nullptr || item->DespawnTimer() == 0)
        {
            // This spawn has gone: to this claimant already, the authority
            // having seen the contact first -- or to somebody else.
            if (spawner->LastPickerSlot() == slot)
            {
                return Verdict(claim.ClaimId, PickupVerdictPacket::ResultGranted,
                    NetPowerUps::Current(slot, *kind).Sequence);
            }
            return Verdict(claim.ClaimId, spawner->LastPickerSlot() >= 0
                ? PickupVerdictPacket::ResultTaken : PickupVerdictPacket::ResultGone);
        }
        if (!Reachable(slot, claim, *player, static_cast<OpenTK::Mathematics::Vector3>(item->Position)))
        {
            return Verdict(claim.ClaimId, PickupVerdictPacket::ResultOutOfReach);
        }
        item->OnPickedUp(player);
        const PowerUpLedger::Grant grant = NetPowerUps::Grant(*player, *kind, claim.ContactFrame);
        return Verdict(claim.ClaimId, PickupVerdictPacket::ResultGranted, grant.Sequence);
    }

    void NetPickupClaims::Receive(std::int32_t slot, std::span<const std::uint8_t> payload)
    {
        if (!NetSession::IsAuthority() || slot < 0 || static_cast<std::size_t>(slot) >= Slots || payload.empty())
        {
            return;
        }
        std::array<PickupVerdictPacket::Entry, PickupClaimPacket::MaxPerPacket> verdicts{};
        std::size_t verdictCount = 0;
        const std::int32_t count = std::min<std::int32_t>(payload[0], PickupClaimPacket::MaxPerPacket);
        for (std::int32_t i = 0; i < count; i++)
        {
            const auto at = static_cast<std::size_t>(1 + i * PickupClaimPacket::Size);
            if (at + PickupClaimPacket::Size > payload.size())
            {
                break;
            }
            const PickupClaimPacket claim = PickupClaimPacket::Read(payload.subspan(at, PickupClaimPacket::Size));
            if (!NetSession::MatchesStream(claim.MatchId, claim.AuthorityEpoch) || claim.ClaimId == 0)
            {
                continue;
            }
            if (const PickupVerdictPacket::Entry* remembered = _replayLedger.Find(slot, claim.ClaimId);
                remembered != nullptr)
            {
                // A resend: the same answer, never a second grant.
                verdicts[verdictCount++] = *remembered;
                continue;
            }
            const PickupVerdictPacket::Entry verdict
                = NetPlayerLifecycle::Matches(slot, claim.SlotGeneration, claim.LifeId)
                ? Judge(slot, claim) : Verdict(claim.ClaimId, PickupVerdictPacket::ResultWrongLife);
            _replayLedger.Remember(slot, verdict);
            if (verdict.Result < _results.size())
            {
                _results[verdict.Result]++;
            }
            if (NetLog::Enabled())
            {
                NetLog::Event("[powerup] claim slot=" + std::to_string(slot) + " id=" + std::to_string(claim.ClaimId)
                    + " spawn=" + std::to_string(claim.SpawnId) + "/" + std::to_string(claim.SpawnCount)
                    + " contact=" + std::to_string(claim.ContactFrame) + ": "
                    + PickupVerdictPacket::Describe(verdict.Result));
            }
            verdicts[verdictCount++] = verdict;
        }
        if (verdictCount > 0 && _verdictSink)
        {
            _verdictSink(slot, std::span<const PickupVerdictPacket::Entry>(verdicts.data(), verdictCount));
        }
    }

    void NetPickupClaims::Tick()
    {
        _outbox.Tick([](std::uint16_t claimId)
        {
            NetPowerUpPrediction::Settle(claimId, false, 0);
        });
    }

    void NetPickupClaims::Reset()
    {
        _outbox.Reset();
        _replayLedger.Reset();
        _results = {};
    }

    std::string NetPickupClaims::Describe()
    {
        std::string text = "pickup claims: " + std::to_string(_outbox.Declared()) + " declared, "
            + std::to_string(_outbox.Unanswered()) + " unanswered";
        for (std::size_t result = 0; result < _results.size(); result++)
        {
            if (_results[result] != 0)
            {
                text += ", " + std::to_string(_results[result]) + " "
                    + PickupVerdictPacket::Describe(static_cast<std::uint8_t>(result));
            }
        }
        return text;
    }
}
