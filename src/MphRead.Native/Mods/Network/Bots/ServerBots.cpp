#include "Mods/Network/Bots/ServerBots.hpp"

#include "Mods/Network/Gameplay/Player/NetPlayerIntent.hpp"
#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Entities/Players/PlayerEntity.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    void ServerBots::Configure(std::int32_t count, std::int32_t level, std::int32_t maxPlayers) noexcept
    {
        _maxPlayers = std::clamp(maxPlayers, 0, Entities::PlayerEntity::SlotCapacity);
        _count = std::clamp(count, 0, std::max(0, _maxPlayers - 1));
        _level = std::clamp(level, 0, MaxLevel);
        _first = _maxPlayers - _count;
    }

    bool ServerBots::Holds(std::int32_t slot) noexcept
    {
        return _count > 0 && slot >= _first && slot < _maxPlayers;
    }

    ::MphRead::Hunter ServerBots::HunterFor(std::int32_t slot) noexcept
    {
        // Kanden first, then round the roster: a room of bots meets every hunter.
        const std::int32_t index = std::max(0, slot - _first);
        return static_cast<::MphRead::Hunter>((index + 1) % 7);
    }

    std::string ServerBots::NameFor(std::int32_t slot)
    {
        return "Bot " + ::MphRead::ToString(HunterFor(slot));
    }

    IntentPacket ServerBots::IntentFor(Entities::PlayerEntity& player,
        std::uint32_t frame, std::uint16_t matchId, std::uint64_t authorityEpoch)
    {
        const std::int32_t slot = player.SlotIndex();
        IntentPacket intent = NetPlayerIntent::CaptureState(player);
        intent.MatchId = matchId;
        intent.AuthorityEpoch = authorityEpoch;
        intent.SlotGeneration = NetPlayerLifecycle::Generation(slot);
        intent.LifeId = NetPlayerLifecycle::Get(slot);
        // The bot plays in the authority's own world, as it stands now.
        intent.Frame = std::max(1U, frame);
        intent.AckFrame = intent.Frame;
        NetInputHistory::Attach(intent, slot);
        // Its shots as events, as a person's client sends its own: a copy
        // that has heard from a player fires their events and nothing else.
        NetShotEvents::AttachFor(slot, intent);
        NetBombs::AttachFor(slot, intent, intent.Frame);
        return intent;
    }
}
