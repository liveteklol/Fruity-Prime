#include "Mods/Network/Gameplay/Player/NetScoreboard.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"

namespace MphRead::Mods::Network
{
    void NetScoreboard::ForgetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Entities::PlayerEntity::SlotCapacity)
        {
            return;
        }

        GameState::Points()[slot] = 0;
        GameState::Kills()[slot] = 0;
        GameState::Deaths()[slot] = 0;
        GameState::Time()[slot] = 0.0F;
        GameState::Suicides()[slot] = 0;
        GameState::FriendlyKills()[slot] = 0;
        GameState::HeadshotKills()[slot] = 0;
        GameState::DamageCount()[slot] = 0;
        GameState::AltDamageCount()[slot] = 0;
        GameState::BeamDamageDealt()[slot] = 0;
        GameState::BeamDamageMax()[slot] = 0;
        GameState::OctolithScores()[slot] = 0;
        GameState::OctolithDrops()[slot] = 0;
        GameState::OctolithStops()[slot] = 0;
        GameState::NodesCaptured()[slot] = 0;
        GameState::NodesLost()[slot] = 0;
        GameState::KillsAsPrime()[slot] = 0;
        GameState::PrimesKilled()[slot] = 0;
        for (std::int32_t beam = 0; beam < 9; beam++)
        {
            GameState::BeamKills()[slot][beam] = 0;
        }
    }
}
