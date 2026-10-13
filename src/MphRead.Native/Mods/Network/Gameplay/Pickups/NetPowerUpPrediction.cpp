#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"

#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"

#include <algorithm>
#include <optional>

namespace MphRead::Mods::Network
{
    namespace
    {
        [[nodiscard]] Entities::PlayerEntity* LocalPlayer()
        {
            const std::int32_t slot = NetSession::LocalSlot();
            const auto& players = Entities::PlayerEntity::Players();
            return slot >= 0 && static_cast<std::size_t>(slot) < players.size()
                ? players[static_cast<std::size_t>(slot)].get() : nullptr;
        }

        [[nodiscard]] std::uint16_t LocalLife()
        {
            return NetPlayerLifecycle::Get(NetSession::LocalSlot());
        }

        // The item sync no longer shows the predicted spawn out.
        [[nodiscard]] bool SpawnSettled(std::int16_t spawnId, std::uint16_t spawnCount)
        {
            ItemSpawnState state{};
            return NetItemState::TryGet(spawnId, state) && (!state.Available || state.SpawnCount != spawnCount);
        }
    }

    void NetPowerUpPrediction::Predict(Entities::PlayerEntity& player, Entities::ItemInstanceEntity& item)
    {
        Entities::ItemSpawnEntity* spawner = item.Owner();
        const std::optional<PowerUp> kind = PowerUpOf(item.ItemType());
        if (spawner == nullptr || !kind.has_value())
        {
            return;
        }
        const auto spawnId = static_cast<std::int16_t>(spawner->Id);
        const std::uint16_t spawnCount = spawner->SpawnCount();
        if (_book.Holds(spawnId, spawnCount) || !_book.HasRoom())
        {
            // A full book leaves it to the authority, which sees the same
            // contact a trip later.
            return;
        }
        const std::uint16_t claimId = NetPickupClaims::Declare(player, *spawner);
        if (claimId == 0)
        {
            return;
        }
        _book.Add(claimId, *kind, spawnId, spawnCount, LocalLife(), std::max(1U, NetSession::NetFrame()),
            player.ItemEffectFrames(item.ItemType()));
        player.GrantItemEffect(item.ItemType(), PowerUpFrames);
        item.SetDespawnTimer(0);
        _predicted++;
        if (NetLog::Enabled())
        {
            NetLog::Event("[powerup] predicted " + std::string(NameOf(*kind)) + " spawn="
                + std::to_string(spawnId) + "/" + std::to_string(spawnCount) + " claim=" + std::to_string(claimId));
        }
    }

    bool NetPowerUpPrediction::Holds(std::int16_t spawnId, std::uint16_t spawnCount) noexcept
    {
        return _book.Holds(spawnId, spawnCount);
    }

    bool NetPowerUpPrediction::Awaiting(PowerUp kind) noexcept
    {
        return _book.Awaiting(kind);
    }

    void NetPowerUpPrediction::Settle(std::uint16_t claimId, bool granted, std::uint8_t sequence)
    {
        Entities::PlayerEntity* player = LocalPlayer();
        (granted ? _confirmed : _refused)++;
        const std::optional<PredictionBook::Rollback> rollback = _book.Settle(claimId, granted, sequence,
            NetSession::NetFrame(), LocalLife(), player != nullptr && player->Health() > 0);
        if (!rollback.has_value() || player == nullptr)
        {
            return;
        }
        player->SetItemEffectFrames(ItemTypeOf(rollback->Kind), rollback->Frames);
        _undone++;
        if (NetLog::Enabled())
        {
            NetLog::Event("[powerup] prediction undone " + std::string(NameOf(rollback->Kind)) + " claim="
                + std::to_string(claimId) + ", back to " + std::to_string(rollback->Frames) + " frames");
        }
    }

    void NetPowerUpPrediction::Seen(PowerUp kind, std::uint8_t sequence)
    {
        _book.Seen(kind, sequence);
    }

    void NetPowerUpPrediction::Tick()
    {
        _book.BeginLife(LocalLife());
        _book.Tick(NetSession::NetFrame(), SpawnSettled);
    }

    void NetPowerUpPrediction::Reset()
    {
        _book.Reset();
        _predicted = 0;
        _confirmed = 0;
        _refused = 0;
        _undone = 0;
    }

    std::string NetPowerUpPrediction::Describe()
    {
        return "power-up pickups: " + std::to_string(_predicted) + " predicted, " + std::to_string(_confirmed)
            + " granted, " + std::to_string(_refused) + " refused (" + std::to_string(_undone) + " taken back)";
    }
}
