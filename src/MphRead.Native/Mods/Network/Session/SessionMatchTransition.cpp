#include "Mods/Network/Session/SessionMatchTransition.hpp"

#include "Mods/Network/Session/ClientConnectionHealth.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/NetRole.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"

using ::MphRead::NativeRuntime::ConsoleWriteLine;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void SessionMatchTransition::Apply(const MatchStatePacket& state, bool rotated,
        SessionRuntimeState& runtime, SessionMatchIdentity& matchIdentity,
        ClientReplicaStream& replicaStream, RemotePlayerChannel& remotePlayers,
        SessionAuthorityState& authorityState, ClientConnectionHealth& connectionHealth,
        SessionRoster& roster, HelloSender sendHello)
    {
        static_cast<void>(rotated);
        const SessionMatchIdentity::Transition transition = matchIdentity.Apply(state);
        switch (transition.Rejected)
        {
        case SessionMatchIdentity::Rejection::None:
            break;
        case SessionMatchIdentity::Rejection::StaleAuthority:
            NetPlayerLifecycle::RecordCrossAuthority();
            return;
        case SessionMatchIdentity::Rejection::StaleMatch:
            NetPlayerLifecycle::RecordCrossMatch();
            return;
        case SessionMatchIdentity::Rejection::InvalidIdentity:
        case SessionMatchIdentity::Rejection::EndingRegression:
            return;
        }

        if (transition.NewMatch || transition.NewAuthority)
        {
            replicaStream.BeginStreamTransition();
            remotePlayers.InvalidateIntents();
            remotePlayers.InvalidateStates();
            NetSmoothing::NoteRoomChanged();
            NetUnlagged::Reset();
            NetHitPrediction::ForgetPending();
            NetHitClaims::ForgetPending();
            if (transition.NewAuthority && transition.HadPrevious)
            {
                for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity); slot++)
                {
                    NetPlayerLifecycle::SetOccupant(slot, 0);
                }
                roster.ClearOccupied();
                authorityState.Revoke();
                if (!runtime.IsPlayback() && runtime.Role() == NetRole::Client)
                {
                    connectionHealth.RequestReannouncement();
                    if (sendHello != nullptr)
                    {
                        sendHello();
                    }
                }
            }
            else if (transition.NewMatch)
            {
                NetPlayerLifecycle::ResetLives();
            }
        }

        if (transition.NewMatch || transition.RoomChanged)
        {
            const std::string room = state.RoomKey.value_or(std::string{});
            ConsoleWriteLine("[net] server map: " + room + " ("
                + ::MphRead::ToString(static_cast<GameMode>(state.Mode)) + ", "
                + Runtime::ToString(state.TimeRemaining, "0") + " s left)");
        }
    }

    bool SessionMatchTransition::MatchesStream(std::uint16_t match, std::uint64_t epoch,
        bool host, const SessionMatchIdentity& matchIdentity)
    {
        if (match == 0 || match != matchIdentity.MatchId(host))
        {
            NetPlayerLifecycle::RecordCrossMatch();
            return false;
        }
        if (epoch == 0 || epoch != matchIdentity.AuthorityEpoch(host))
        {
            NetPlayerLifecycle::RecordCrossAuthority();
            return false;
        }
        return true;
    }
}
