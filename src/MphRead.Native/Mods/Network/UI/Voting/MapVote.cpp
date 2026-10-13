#include "Mods/Network/UI/Voting/MapVote.hpp"

#include "Formats/Types.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <wctype.h>
#endif

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringIsNullOrWhiteSpace;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace
{
    void ReplaceHit(
        MphRead::Mods::EndScreen::Hit& target,
        const MphRead::Mods::EndScreen::Hit& source) noexcept
    {
        target.~Hit();
        ::new (static_cast<void*>(std::addressof(target)))
            MphRead::Mods::EndScreen::Hit(source);
    }

    [[nodiscard]] const std::string& OrEmpty(
        const std::optional<std::string>& value) noexcept
    {
        static const std::string empty;
        return value.has_value() ? *value : empty;
    }
}

namespace MphRead::Mods::Network
{
    bool MapVote::_active = false;
    std::optional<std::string> MapVote::_roomKey = std::string();
    std::optional<std::string> MapVote::_proposer = std::string();
    std::int32_t MapVote::_yes = 0;
    std::int32_t MapVote::_no = 0;
    std::int32_t MapVote::_eligible = 0;
    std::int32_t MapVote::_needed = 0;
    std::int32_t MapVote::_seconds = 0;
    bool MapVote::_answered = false;
    bool MapVote::_supported = false;
    bool MapVote::_disabled = false;
    MphRead::Mods::EndScreen::Hit MapVote::_hitAccept{};
    MphRead::Mods::EndScreen::Hit MapVote::_hitDeny{};

    std::string MapVote::WhyNotProposing()
    {
        if (!NetSession::Active())
        {
            return "You are not in an online match.";
        }
        if (!_supported)
        {
            return "This server has not answered about voting; it may be an older build.";
        }
        if (_disabled)
        {
            return "Voting is switched off on this server.";
        }
        if (_active)
        {
            return "A vote is already running: " + OrEmpty(_roomKey) + ".";
        }
        if (_seconds > 0)
        {
            return "Another vote may be called in " + std::to_string(_seconds) + " s.";
        }
        return "";
    }

    void MapVote::NoteLayout(
        MphRead::Mods::EndScreen::Hit accept,
        MphRead::Mods::EndScreen::Hit deny) noexcept
    {
        ReplaceHit(_hitAccept, accept);
        ReplaceHit(_hitDeny, deny);
    }

    bool MapVote::HandleClick()
    {
        if (!_active || _answered)
        {
            return false;
        }
        const float x = MphRead::Mods::EndScreen::PointerX();
        const float y = MphRead::Mods::EndScreen::PointerY();
        if (_hitAccept.Contains(x, y))
        {
            Cast(true);
            return true;
        }
        if (_hitDeny.Contains(x, y))
        {
            Cast(false);
            return true;
        }
        return false;
    }

    std::shared_ptr<std::vector<float>> MapVote::TouchTargets()
    {
        if (!_active || _answered || _hitAccept.Right <= _hitAccept.Left)
        {
            static const std::shared_ptr<std::vector<float>> empty
                = std::make_shared<std::vector<float>>();
            return empty;
        }
        return std::make_shared<std::vector<float>>(std::initializer_list<float>{
            _hitAccept.Left, _hitAccept.Top, _hitAccept.Right, _hitAccept.Bottom,
            _hitDeny.Left, _hitDeny.Top, _hitDeny.Right, _hitDeny.Bottom
        });
    }

    void MapVote::Reset()
    {
        ReplaceHit(_hitAccept, MphRead::Mods::EndScreen::Hit{});
        ReplaceHit(_hitDeny, MphRead::Mods::EndScreen::Hit{});
        _active = false;
        _roomKey = std::string();
        _proposer = std::string();
        _seconds = 0;
        _needed = 0;
        _eligible = 0;
        _no = 0;
        _yes = 0;
        _answered = false;
        _supported = false;
        _disabled = false;
    }

    void MapVote::Apply(VoteStatePacket state)
    {
        _supported = true;
        _disabled = state.State != VoteStatePacket::StateRunning
            && state.Seconds == std::numeric_limits<std::uint16_t>::max();
        const bool wasActive = _active;
        const std::optional<std::string> wasRoom = _roomKey;
        _active = state.State == VoteStatePacket::StateRunning;
        _roomKey = state.RoomKey;
        _proposer = state.Proposer;
        _yes = state.Yes;
        _no = state.No;
        _eligible = state.Eligible;
        _needed = state.Needed;
        _seconds = _disabled ? 0 : state.Seconds;
        if (!_active || !wasActive || wasRoom != _roomKey)
        {
            if (!_active || wasRoom != _roomKey)
            {
                _answered = false;
            }
        }
    }

    void MapVote::Propose(const std::optional<std::string>& roomKey)
    {
        if (!NetSession::Active() || StringIsNullOrWhiteSpace(roomKey))
        {
            return;
        }
        NetSession::SendVote(VotePacket::KindPropose, *roomKey);
    }

    void MapVote::Cast(bool yes)
    {
        if (!_active || _answered || !NetSession::Active())
        {
            return;
        }
        _answered = true;
        NetSession::SendVote(
            yes ? VotePacket::KindYes : VotePacket::KindNo,
            "");
    }

    std::string MapVote::PromptLine()
    {
        if (!_active)
        {
            return "";
        }
        const std::string proposer = OrEmpty(_proposer);
        return proposer + " PROPOSES " + ::MphRead::NativeRuntime::ToUpperInvariant(RequireReference(_roomKey));
    }

    std::string MapVote::TallyLine()
    {
        if (!_active)
        {
            return "";
        }
        const std::string answer = _answered ? "" : "  F1 YES / F2 NO";
        return std::to_string(_yes) + "/" + std::to_string(_needed)
            + " OF " + std::to_string(_eligible) + "   "
            + std::to_string(_seconds) + "s" + answer;
    }
}

namespace MphRead::Mods::Network::Detail
{
    void NetSessionMapVoteReset()
    {
        MapVote::Reset();
    }

    void NetSessionMapVoteApply(const VoteStatePacket& state)
    {
        MapVote::Apply(state);
    }
}
