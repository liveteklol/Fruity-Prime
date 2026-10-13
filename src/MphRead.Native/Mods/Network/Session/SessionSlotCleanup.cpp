#include "Mods/Network/Session/SessionSlotCleanup.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"

namespace MphRead::Mods::Network
{
    void SessionSlotCleanup::Forget(std::int32_t slot, PeerRegistry& peers,
        ClientReplicaStream& replicaStream, RemotePlayerChannel& remotePlayers)
    {
        if (slot < 0 || slot >= Entities::PlayerEntity::SlotCapacity)
        {
            return;
        }

        SharedContinuousWeaponPhase().ResetSlot(slot);
        replicaStream.ForgetSlot(slot);
        remotePlayers.Forget(slot);
        peers.ResetIntentFrameForSlot(slot);
    }
}
