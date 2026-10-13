#include "Mods/Network/Session/SessionLifecycle.hpp"

#include "Mods/Network/Session/SessionClaimChannel.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/Network/Session/RemotePlayerChannel.hpp"
#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "Mods/Network/Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerSetup.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Session/NetMatchSync.hpp"
#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Claims/Pickup/NetPickupClaims.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUps.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Weapons/ContinuousWeaponPhase.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/UI/Voting/MapVote.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/DebugLog.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Mods/Chat/ChatBox.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    namespace
    {
        SessionClaimChannel SessionClaims{};
    }

    void SessionLifecycle::StartServerAuthority(Ports& ports, SnapshotSink sink,
        std::function<void()> matchEnded)
    {
        Stop(ports);
        ports.runtime.SetRole(NetRole::Server);
        ports.simulationCallbacks.Configure(std::move(sink), std::move(matchEnded));
        ports.authorityState.BecomeServerAuthority();
        if (Mods::DebugLog::Active())
        {
            NetLog::Open("server");
        }
        ports.runtime.SetLocalSlot(-1);
        ports.runtime.ResetFrame();
        ports.runtime.ClearLastError();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetPickupClaims::Reset();
        NetPowerUps::Reset();
        FreezeStatusEffectPolicy::Reset();
        NetPowerUpPrediction::Reset();
        NetSmoothing::Reset();
    }

    void SessionLifecycle::StartHost(Ports& ports, std::int32_t port)
    {
        Stop(ports);
        try
        {
            ports.connection.Open(port);
            ports.runtime.SetRole(NetRole::Host);
            ports.runtime.SetLocalSlot(0);
            SessionPeers().ResetHostGenerations();
            SessionPeers().SetHostGeneration(0, 1);
            NetPlayerLifecycle::SetOccupant(0, 1);
            SessionSlots().SetOccupied(0, true);
            MatchStatePacket match{};
            match.MatchId = 1;
            match.AuthorityEpoch = static_cast<std::uint64_t>(Runtime::DateTimeUtcNowTicks());
            ports.matchIdentity.SetAuthoritative(match);
            ports.runtime.ResetFrame();
            ports.runtime.ClearLastError();
            SessionClaims.Start(ports.connection, SessionPeers(), ports.matchIdentity);
            Runtime::ConsoleWriteLine("[net] hosting on UDP " + Runtime::ToString(ports.connection.LocalPort()));
        }
        catch (const std::exception& ex)
        {
            ports.runtime.SetLastError(ex.what());
            Runtime::ConsoleWriteLine("[net] host failed: " + std::string(ex.what()));
            ports.runtime.SetRole(NetRole::Offline);
        }
    }

    void SessionLifecycle::StartClient(Ports& ports, const std::string& address, std::int32_t port,
        Runtime::Guid ownerToken)
    {
        Stop(ports);
        try
        {
            ports.clientLobby.SetOwnerToken(ownerToken);
            ports.connectionHealth.Begin(ports.clock());
            ports.connection.Open(0);
            ports.connection.AnswerPingsImmediately();
            ports.connection.SetHostEndpoint(std::make_shared<System::Net::IPEndPoint>(
                ports.resolveIPv4(address), port));
            ports.runtime.SetRole(NetRole::Client);
            ports.runtime.SetLocalSlot(-1);
            ports.runtime.ResetFrame();
            ports.runtime.ClearLastError();
            NetLog::Open(ports.playerIdentity.Name());
            NetLog::Event("joining " + address + ":" + Runtime::ToString(port)
                + " as \"" + ports.playerIdentity.Name() + "\"");
            ports.sendHello();
            ports.sendIdentify();
            Runtime::ConsoleWriteLine("[net] joining " + address + ":" + Runtime::ToString(port)
                + " as \"" + ports.playerIdentity.Name() + "\"");
        }
        catch (const std::exception& ex)
        {
            ports.runtime.SetLastError(ex.what());
            Runtime::ConsoleWriteLine("[net] join failed: " + std::string(ex.what()));
            ports.runtime.SetRole(NetRole::Offline);
        }
    }

    void SessionLifecycle::StartPlayback(Ports& ports)
    {
        Stop(ports);
        ports.runtime.SetPlayback(true);
        ports.connection.Open(0);
        ports.runtime.SetRole(NetRole::Client);
        ports.runtime.SetLocalSlot(-1);
        ports.runtime.ResetFrame();
        ports.runtime.ClearLastError();
    }

    void SessionLifecycle::RebindSocket(Ports& ports)
    {
        if (ports.runtime.Role() != NetRole::Client || !ports.connection.IsOpen())
        {
            return;
        }
        const std::int32_t wasPort = ports.connection.Rebind();
        Runtime::ConsoleWriteLine("[net] rebound the socket: " + Runtime::ToString(wasPort)
            + " -> " + Runtime::ToString(ports.connection.LocalPort()));
        ports.sendHello();
        ports.sendIdentify();
    }

    void SessionLifecycle::Stop(Ports& ports)
    {
        SessionClaims.Stop();
        ports.resetLobbySession();
        ports.runtime.SetPlayback(false);
        NetPlayerSetup::Reset();
        SpectatorMode::Reset();
        DemoRecorder::Stop();
        NetMatchSync::Reset();
        NetSlotManager::Reset();
        NetDamage::Reset();
        NetRoomChange::Reset();
        NetMatchEnd::Reset();
        NetPlayerBridge::Reset();
        Mods::Chat::ChatBox::Clear();
        ports.authorityState.Reset();
        ports.simulationCallbacks.Reset();
        if (ports.connection.IsOpen()
            && ports.runtime.Role() == NetRole::Client
            && ports.connection.HasHostEndpoint())
        {
            ports.connection.SendToHost(PacketType::Bye, {});
        }
        ports.connection.Close();
        SessionPeers().Clear();
        ports.runtime.SetRole(NetRole::Offline);
        MapVote::Reset();
        ports.connectionHealth.Reset();
        ports.runtime.SetLocalSlot(0);
        RemotePlayers().InvalidateStates();
        RemotePlayers().InvalidateIntents();
        RemotePlayers().ClearIntentArrival();
        SharedContinuousWeaponPhase().Reset();
        SessionSlots().ClearPings();
        ports.replicaStream.Stop();
        NetPlayerLifecycle::Reset();
        SessionSlots().ClearOccupied();
        ports.traffic.Reset();
        ports.matchIdentity.Reset();
        NetUnlagged::Reset();
        NetHitPrediction::Reset();
        NetHitClaims::Reset();
        NetPickupClaims::Reset();
        NetPowerUps::Reset();
        FreezeStatusEffectPolicy::Reset();
        NetPowerUpPrediction::Reset();
        NetSmoothing::Reset();
    }
}
