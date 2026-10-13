#include "Mods/Network/Session/SessionMatchReset.hpp"

#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Session/NetMatchSync.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerSetup.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/SpectatorMode.hpp"

namespace MphRead::Mods::Network
{
    void SessionMatchReset::Apply(RemotePlayerChannel& remotePlayers,
        ClientReplicaStream& replicaStream)
    {
        NetItemState::BeginRoom();
        NetPickupClaims::Reset();
        NetPowerUps::Reset();
        NetPowerUpPrediction::Reset();
        NetPlayerSetup::Reset();
        SpectatorMode::Reset();
        NetMatchSync::Reset();
        NetSlotManager::Reset();
        NetDamage::Reset();
        NetRoomChange::Reset();
        NetMatchEnd::Reset();
        NetPlayerBridge::Reset();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetSmoothing::Reset();
        remotePlayers.InvalidateStates();
        remotePlayers.InvalidateIntents();
        remotePlayers.ClearIntentArrival();
        replicaStream.ResetMatch();
        SharedContinuousWeaponPhase().Reset();
        NetPlayerLifecycle::ResetLives();
    }
}
