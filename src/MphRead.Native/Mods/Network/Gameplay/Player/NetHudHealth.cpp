#include "Mods/Network/Gameplay/Player/NetHudHealth.hpp"

#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include "Mods/SpectatorMode.hpp"
#include "Entities/Players/PlayerEntity.hpp"

namespace MphRead::Mods::Network
{
    bool NetHudHealth::HideOpponents()
    {
        // NetSession.ServerSession is { Match.HideOpponentHealth: true }.
        if (!NetSession::Active())
        {
            return false;
        }
        const std::optional<SessionStatePacket> session = NetSession::ServerSession();
        return session.has_value() && session->Match.HideOpponentHealth;
    }

    bool NetHudHealth::Visible(std::int32_t slot)
    {
        return !HideOpponents()
            || (slot == NetSession::LocalSlot() && !SpectatorMode::IsSpectating() && !DemoPlayback::IsActive());
    }

    HudHealthSample NetHudHealth::Sample(const ::MphRead::Entities::PlayerEntity& player)
    {
        const std::int32_t slot = player.SlotIndex();
        if (NetSession::Active() && !NetSession::IsAuthority() && !NetSession::IsHost()
            && slot >= 0 && slot < static_cast<std::int32_t>(NetSession::RemotePlayerSlots)
            && NetSession::HasRemoteState(slot))
        {
            const PlayerState& state = NetSession::RemoteState(static_cast<std::size_t>(slot));
            if (state.SlotIndex == slot && state.LifeId != 0
                && NetPlayerLifecycle::Matches(slot, state.SlotGeneration, state.LifeId))
            {
                return HudHealthSample{state.Health, NetSession::LastSnapshotFrame(), state.LifeId, true};
            }
        }
        const bool authoritative = !NetSession::Active() || NetSession::IsAuthority() || NetSession::IsHost();
        return HudHealthSample{player.Health(), authoritative ? NetSession::NetFrame() : 0U,
            NetPlayerLifecycle::Get(slot), authoritative};
    }
}
