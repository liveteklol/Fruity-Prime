#include "Mods/Network/Server/Match/ServerMatchLifecycle.hpp"

#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Server/Match/ServerMatchRotationController.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotChannel.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "GameState.hpp"
#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "Program.hpp"

#include <algorithm>
#include <span>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    ServerMatchLifecycle::ServerMatchLifecycle(Ports ports) noexcept
        : _ports(ports)
    {
    }

    double ServerMatchLifecycle::EndSequenceSeconds()
    {
        return 3.0 + GameState::MatchEndingSeconds + 1.0;
    }

    MatchDefinition ServerMatchLifecycle::CurrentDefinition() const
    {
        return _ports.CurrentDefinition != nullptr
            ? _ports.CurrentDefinition(_ports.Context) : MatchDefinition{};
    }

    void ServerMatchLifecycle::Log(const std::string& message) const
    {
        if (_ports.WriteLog != nullptr)
        {
            _ports.WriteLog(_ports.Context, message);
        }
    }

    bool ServerMatchLifecycle::Simulating() const
    {
        return _ports.Simulation->Running();
    }

    double ServerMatchLifecycle::EndSequenceFor() const
    {
        bool allReady = true;
        std::int32_t counted = 0;
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            ++counted;
            if (!peer->Lobby.PostMatchReady())
            {
                allReady = false;
                break;
            }
        }
        const double readyWait = counted == 0 || allReady ? AllReadySeconds : ReadyWaitSeconds;
        return std::max(EndSequenceSeconds(), readyWait);
    }

    void ServerMatchLifecycle::EndMatch(double now, const std::string& reason)
    {
        if (*_ports.MatchEndedAt >= 0 || _ports.Lobby->Phase() != SessionPhase::InMatch)
        {
            return;
        }
        *_ports.MatchEndedAt = now;
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            peer->Lobby.ClearPostMatchReady();
        }
        _ports.LobbySession->SetPhase(SessionPhase::PostMatch);
        _ports.Voting->OpenBallot();
        Log("match over on " + CurrentDefinition().RoomKey.value_or("") + " (" + reason + "); "
            + (*_ports.Rotation)->Next()->RoomKey + " in "
            + Runtime::ToString(_ports.EndSequenceSeconds, "0") + " s"
            + (_ports.Voting->BallotOpen() ? "; ballot open" : ""));
        _ports.State->BroadcastMatchState(now);
        _ports.Voting->BroadcastMapChoices();
    }

    bool ServerMatchLifecycle::RecoverFromBrokenRoom(double now)
    {
        if (now - _recoveredAt < RecoveryCooldownSeconds || _ports.Lobby->Phase() != SessionPhase::InMatch)
        {
            return false;
        }
        const std::string broken = CurrentDefinition().RoomKey.value_or("");
        const auto known = [this](const std::string& room)
        {
            return std::any_of(_brokenRooms.begin(), _brokenRooms.end(), [&](const std::string& other)
            {
                return Runtime::StringEqualsOrdinalIgnoreCase(other, room);
            });
        };
        if (!broken.empty() && !known(broken))
        {
            _brokenRooms.push_back(broken);
        }
        std::string room;
        GameMode mode = GameMode::Battle;
        if (!_previousRoom.empty() && !known(_previousRoom))
        {
            room = _previousRoom;
            mode = _previousMode;
        }
        else
        {
            for (const std::shared_ptr<const RotationEntry>& entry : (*_ports.Rotation)->Entries())
            {
                if (!known(entry->RoomKey))
                {
                    room = entry->RoomKey;
                    mode = entry->Mode;
                    break;
                }
            }
        }
        _recoveredAt = now;
        if (room.empty())
        {
            Log("the room \"" + broken + "\" cannot be played and there is no other map to go back to");
            return false;
        }
        Log("the room \"" + broken + "\" cannot be played (every simulation step fails); going back to " + room);
        (*_ports.Rotation)->PlayNext(room, mode);
        AdvanceMap(now);
        return true;
    }

    void ServerMatchLifecycle::AdvanceMap(double now)
    {
        if (const std::shared_ptr<const RotationEntry> before = (*_ports.Rotation)->Current())
        {
            _previousRoom = before->RoomKey;
            _previousMode = before->Mode;
        }
        const std::shared_ptr<const RotationEntry> entry = (*_ports.Rotation)->Advance();
        _ports.Lobby->SetPhase(SessionPhase::InMatch);
        const MatchDefinition nextDefinition = CurrentDefinition();
        ServerMatchRotationController::Advance(nextDefinition, now, *_ports.Lobby,
            *_ports.Peers, *_ports.Slots, *_ports.SnapshotStream, *_ports.MatchId,
            *_ports.MatchStarted, *_ports.MatchEndedAt);
        _ports.LobbySession->TouchRevision("rotation advanced");
        _ports.Voting->CancelForMapAdvance(now);
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            peer->Lobby.ClearPostMatchReady();
        }
        _ports.Voting->CloseBallot();
        _ports.Voting->BroadcastMapChoices();
        Log("rotating to " + entry->ToString() + ", resources "
            + Multiplayer::ToString(_ports.Lobby->MapWorldProfile().Resources) + " for "
            + std::to_string(_ports.Peers->All().size()) + " player(s)");
        if (Simulating())
        {
            _ports.Session->ApplySessionState(_ports.State->BuildSessionState());
        }
        _ports.State->BroadcastSessionState();
        MatchStatePacket state = _ports.State->BuildMatchState(now);
        if (Simulating())
        {
            _ports.Session->ApplyMatchState(state, true);
        }
        state.Write(*_ports.Scratch);
        for (const ServerPeerRegistry::PeerHandle& peer : _ports.Peers->All())
        {
            if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
            {
                (*_ports.Transport)->Send(peer->EndPoint, PacketType::MapChange,
                    std::span<const std::uint8_t>(*_ports.Scratch).first(MatchStatePacket::Size));
            }
        }
    }

    void ServerMatchLifecycle::StartSimulation()
    {
        if (!*_ports.RunsTheMatch)
        {
            return;
        }
        std::string why;
        if (!ServerSimulationController::Available(why))
        {
            Log("cannot run the match: " + why);
            Log("a dedicated server runs the match itself now, so this one will not "
                "start. Put the game files on this machine and paths.txt beside "
                "the binary -- see SERVER.md");
            throw ProgramException("the server cannot run the match: " + why);
        }
        const MatchDefinition entry = CurrentDefinition();
        const std::string room = entry.RoomKey.value_or("");
        if (!_ports.Simulation->Start(entry, _ports.MaxPlayers,
                [this](std::span<const std::uint8_t> payload)
                {
                    _ports.SnapshotChannel->Publish(payload, _ports.Transport->get());
                },
                [this]() { EndMatch(*_ports.Now, "score"); }, _ports.Session->Simulation,
                _ports.State->BuildRoster(), _ports.State->BuildSessionState()))
        {
            Log("cannot run the match: the room \"" + room + "\" would not load");
            throw ProgramException("the server could not load \"" + room + "\"");
        }
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            _ports.Claims->Start(**_ports.Transport);
        }
        SynchronizeSimulationState(*_ports.Now);
    }

    void ServerMatchLifecycle::SynchronizeSimulationState(double now)
    {
        if (!Simulating())
        {
            return;
        }
        _ports.Session->ApplySessionState(_ports.State->BuildSessionState());
        _ports.Session->ApplyMatchState(_ports.State->BuildMatchState(now), false);
        _ports.Session->ApplyRoster(_ports.State->BuildRoster());
    }

    void ServerMatchLifecycle::HandleMatchEnd(const ReceivedPacket& packet, double now)
    {
        const ServerPeerRegistry::PeerHandle peer = _ports.Peers->Find(packet.Sender);
        if (peer == nullptr)
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        // Match completion is decided by the server simulation. A client
        // hit claim may change its score, but a client cannot end the world.
    }
}
