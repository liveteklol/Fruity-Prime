#include "Mods/Network/Session/NetMatchEnd.hpp"

#include "GameState.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace MphRead::Mods::Network
{
    std::uint32_t NetMatchEnd::_lastReport = 0;
    bool NetMatchEnd::_reported = false;
    bool NetMatchEnd::_acknowledged = false;
    std::uint32_t NetMatchEnd::_strandedSince = 0;

    void NetMatchEnd::Reset()
    {
        _lastReport = 0;
        _reported = false;
        _acknowledged = false;
        _strandedSince = 0;
    }

    bool NetMatchEnd::MayEndOnScore()
    {
        return !NetSession::Active()
            || NetSession::IsAuthority()
            || NetSession::IsHost();
    }

    bool NetMatchEnd::InIntermission()
    {
        if (!NetSession::Active())
        {
            return false;
        }
        if (!(GameState::MatchState() == MphRead::MatchState::InProgress))
        {
            return true;
        }
        const std::optional<MatchStatePacket> serverMatch
            = NetSession::ServerMatch();
        return serverMatch.has_value() && serverMatch->Ending();
    }

    void NetMatchEnd::Sync()
    {
        if (!NetSession::Active())
        {
            return;
        }

        const std::optional<MatchStatePacket> serverMatch
            = NetSession::ServerMatch();
        const bool serverEnding = serverMatch.has_value() && serverMatch->Ending();
        if (serverEnding && (GameState::MatchState() == MphRead::MatchState::InProgress))
        {
            GameState::MatchTime(0.0F);
        }

        RecoverIfStranded(serverEnding);
        if (!NetSession::IsAuthority()
            && !NetSession::IsHost())
        {
            return;
        }
        if ((GameState::MatchState() == MphRead::MatchState::InProgress))
        {
            _reported = false;
            _acknowledged = false;
            return;
        }
        if (serverEnding)
        {
            _reported = true;
            _acknowledged = true;
            return;
        }
        if (_acknowledged)
        {
            return;
        }
        if (_reported
            && NetSession::NetFrame() - _lastReport < ReportInterval)
        {
            return;
        }

        _reported = true;
        _lastReport = NetSession::NetFrame();
        NetSession::SendMatchEnd();
    }

    void NetMatchEnd::RecoverIfStranded(bool serverEnding)
    {
        const std::optional<MatchStatePacket> state
            = NetSession::ServerMatch();
        const bool serverRunning = state.has_value() && !serverEnding
            && (state->Flags & MatchStatePacket::FlagInProgress) != 0;
        if (!serverRunning || (GameState::MatchState() == MphRead::MatchState::InProgress))
        {
            _strandedSince = 0;
            return;
        }
        if (_strandedSince == 0)
        {
            _strandedSince = std::max(
                NetSession::NetFrame(), std::uint32_t{1});
            return;
        }
        if (NetSession::NetFrame() - _strandedSince < StrandedFrames)
        {
            return;
        }

        _strandedSince = 0;
        NativeRuntime::ConsoleWriteLine(
            "[net] the server's match is still running; leaving the results screen");
        NetLog::Event("recovered from a results screen the server did not ask for");
        GameState::ResetMatchProgress();
        GameState::MatchTime(state->TimeRemaining);
    }

    bool NetMatchEnd::ShouldLeaveAfterMatch()
    {
        return !NetSession::Active();
    }
}
