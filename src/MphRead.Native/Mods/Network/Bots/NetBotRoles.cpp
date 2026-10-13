#include "Mods/Network/Bots/NetBotRoles.hpp"

#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Bots/ServerBots.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/AiPersonality.hpp"
#include "GameState.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    void NetBotRoles::PlayLocalAs(std::optional<std::int32_t> level) noexcept
    {
        _localLevel = level.has_value()
            ? std::optional<std::int32_t>(std::clamp(*level, 0, ServerBots::MaxLevel)) : std::nullopt;
    }

    std::optional<std::int32_t> NetBotRoles::LevelFor(std::int32_t slot) noexcept
    {
        if (NetSession::IsAuthority() && ServerBots::Holds(slot))
        {
            return ServerBots::Level();
        }
        if (_localLevel.has_value() && slot >= 0 && slot == NetSession::LocalSlot())
        {
            return _localLevel;
        }
        return std::nullopt;
    }

    void NetBotRoles::Assign(Entities::PlayerEntity& player)
    {
        const std::optional<std::int32_t> level = LevelFor(player.SlotIndex());
        player.SetIsBot(level.has_value());
        player.SetBotLevel(level.value_or(0));
    }

    void NetBotRoles::PrepareSpawn(Entities::PlayerEntity& player)
    {
        if (!player.IsBot() || !LevelFor(player.SlotIndex()).has_value() || player.AiData == nullptr
            || player.AiData->Personality != nullptr)
        {
            return;
        }
        // The game's own room-load steps for bots, run once for all of them.
        Formats::AiPersonality::LoadAll(GameState::Mode());
        for (const std::shared_ptr<Entities::PlayerEntity>& other : Entities::PlayerEntity::Players())
        {
            if (other != nullptr && other->IsBot() && other->AiData != nullptr)
            {
                other->AiData->InitializeAtLoad();
            }
        }
    }
}
