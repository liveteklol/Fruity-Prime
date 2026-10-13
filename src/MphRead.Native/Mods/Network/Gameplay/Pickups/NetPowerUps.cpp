#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"

#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Protocol/PowerUpSync.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace
    {
        // A contact older than this is not one the authority starts a grant at.
        constexpr std::uint32_t MaxContactAge = 120;

        [[nodiscard]] Entities::PlayerEntity* PlayerAt(std::size_t slot)
        {
            const auto& players = Entities::PlayerEntity::Players();
            return slot < players.size() ? players[slot].get() : nullptr;
        }
    }

    std::uint32_t NetPowerUps::ContactFrame(const Entities::PlayerEntity& player)
    {
        const std::uint32_t now = NetSession::NetFrame();
        const std::int32_t slot = player.SlotIndex();
        if (player.IsBot() || slot < 0 || slot == NetSession::LocalSlot()
            || static_cast<std::size_t>(slot) >= NetSession::RemotePlayerSlots
            || !NetSession::HasRemoteIntent(slot))
        {
            return now;
        }
        // Where this copy stands is where its owner stood in the world the
        // owner's intent acknowledged: that is when the owner touched it.
        const std::uint32_t ack = NetSession::RemoteIntent(static_cast<std::size_t>(slot)).AckFrame;
        return ack != 0 && ack <= now && now - ack <= MaxContactAge ? ack : now;
    }

    void NetPowerUps::GrantContact(Entities::PlayerEntity& player, ::MphRead::ItemType type)
    {
        if (const std::optional<PowerUp> kind = PowerUpOf(type))
        {
            static_cast<void>(Grant(player, *kind, ContactFrame(player)));
        }
    }

    PowerUpLedger::Grant NetPowerUps::Grant(Entities::PlayerEntity& player, PowerUp kind, std::uint32_t start)
    {
        const std::uint32_t now = NetSession::NetFrame();
        const PowerUpLedger::Grant grant = _ledger.Begin(player.SlotIndex(), kind, std::min(start, now), PowerUpFrames,
            NetPlayerLifecycle::Get(player.SlotIndex()));
        if (grant.Sequence == 0)
        {
            return grant;
        }
        _granted++;
        const auto left = static_cast<std::uint16_t>(grant.Expire > now ? grant.Expire - now : 0U);
        player.GrantItemEffect(ItemTypeOf(kind), left);
        if (NetLog::Enabled())
        {
            NetLog::Event("[powerup] granted slot=" + std::to_string(player.SlotIndex())
                + " kind=" + std::string(NameOf(kind)) + " seq=" + std::to_string(grant.Sequence)
                + " start=" + std::to_string(grant.Start) + " now=" + std::to_string(now)
                + " left=" + std::to_string(left));
        }
        return grant;
    }

    void NetPowerUps::Observe()
    {
        if (!NetSession::Active() || !NetSession::IsAuthority())
        {
            return;
        }
        const std::uint32_t now = NetSession::NetFrame();
        for (std::size_t slot = 0; slot < Slots; slot++)
        {
            const Entities::PlayerEntity* player = PlayerAt(slot);
            if (player == nullptr)
            {
                continue;
            }
            for (std::size_t k = 0; k < PowerUpCount; k++)
            {
                const auto kind = static_cast<PowerUp>(k);
                const PowerUpLedger::Grant grant = _ledger.Current(static_cast<std::int32_t>(slot), kind);
                if (grant.RunningAt(now) && player->ItemEffectFrames(ItemTypeOf(kind)) == 0)
                {
                    _ledger.End(static_cast<std::int32_t>(slot), kind, now);
                    _endedEarly++;
                }
            }
        }
    }

    bool NetPowerUps::RanAt(std::int32_t slot, PowerUp kind, std::uint32_t frame, std::uint16_t life)
    {
        return _ledger.RanAt(slot, kind, frame, life);
    }

    void NetPowerUps::BeginLife(std::int32_t slot)
    {
        if (NetSession::Active() && NetSession::IsAuthority())
        {
            _ledger.EndAll(slot, NetSession::NetFrame());
        }
    }

    PowerUpLedger::Grant NetPowerUps::Current(std::int32_t slot, PowerUp kind)
    {
        return _ledger.Current(slot, kind);
    }

    void NetPowerUps::Capture(const Entities::PlayerEntity& player, PlayerState& state)
    {
        for (std::size_t k = 0; k < PowerUpCount; k++)
        {
            const auto kind = static_cast<PowerUp>(k);
            state.PowerUps[k] = PowerUpReport{ _ledger.Current(player.SlotIndex(), kind).Sequence,
                player.ItemEffectFrames(ItemTypeOf(kind)) };
        }
    }

    void NetPowerUps::Apply(Entities::PlayerEntity& player, const PlayerState& state, bool isLocal)
    {
        const std::int32_t slot = player.SlotIndex();
        if (slot < 0 || static_cast<std::size_t>(slot) >= Slots)
        {
            return;
        }
        auto& seen = _seen[static_cast<std::size_t>(slot)];
        for (std::size_t k = 0; k < PowerUpCount; k++)
        {
            const auto kind = static_cast<PowerUp>(k);
            const ::MphRead::ItemType type = ItemTypeOf(kind);
            const PowerUpReport reported = state.PowerUps[k];
            const bool awaiting = isLocal && NetPowerUpPrediction::Awaiting(kind);
            switch (ReconcilePowerUp(reported, seen[k], player.ItemEffectFrames(type), awaiting, player.Health() > 0))
            {
            case PowerUpStep::Start:
                player.GrantItemEffect(type, reported.Remaining);
                _started++;
                break;
            case PowerUpStep::Resume:
                player.GrantItemEffect(type, reported.Remaining, false);
                _resumed++;
                break;
            case PowerUpStep::Retime:
                player.SetItemEffectFrames(type, reported.Remaining);
                _retimed++;
                break;
            case PowerUpStep::End:
                player.SetItemEffectFrames(type, 0);
                _ended++;
                break;
            case PowerUpStep::Keep:
                break;
            }
            seen[k] = reported.Sequence;
            if (isLocal)
            {
                NetPowerUpPrediction::Seen(kind, reported.Sequence);
            }
        }
    }

    void NetPowerUps::ForgetSlot(std::int32_t slot)
    {
        _ledger.Forget(slot);
        if (slot >= 0 && static_cast<std::size_t>(slot) < Slots)
        {
            _seen[static_cast<std::size_t>(slot)] = {};
        }
    }

    void NetPowerUps::Reset()
    {
        _ledger.Reset();
        _seen = {};
        _granted = 0;
        _endedEarly = 0;
        _started = 0;
        _retimed = 0;
        _resumed = 0;
        _ended = 0;
    }

    std::string NetPowerUps::Describe()
    {
        return "power-ups: " + std::to_string(_granted) + " granted here (" + std::to_string(_endedEarly)
            + " ended early), copies started " + std::to_string(_started) + ", retimed "
            + std::to_string(_retimed) + ", resumed " + std::to_string(_resumed) + ", ended " + std::to_string(_ended);
    }
}
