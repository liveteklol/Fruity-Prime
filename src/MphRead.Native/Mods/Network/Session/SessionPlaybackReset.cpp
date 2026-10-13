#include "Mods/Network/Session/SessionPlaybackReset.hpp"

#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"

namespace MphRead::Mods::Network
{
    void SessionPlaybackReset::Apply(SessionRuntimeState& runtime,
        ClientReplicaStream& replicaStream, RemotePlayerChannel& remotePlayers,
        SessionTrafficMetrics& traffic)
    {
        runtime.ResetFrame();
        SharedContinuousWeaponPhase().Reset();
        NetPlayerLifecycle::ResetLives();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetPickupClaims::Reset();
        NetPowerUps::Reset();
        FreezeStatusEffectPolicy::Reset();
        NetPowerUpPrediction::Reset();
        NetSmoothing::Reset();
        replicaStream.RewindPlayback();
        remotePlayers.InvalidateStates();
        remotePlayers.InvalidateIntents();
        traffic.Reset();
        NetPlayerBridge::Reset();
        NetDamage::Reset();
    }
}
