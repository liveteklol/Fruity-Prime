#include "Mods/Network/Session/SessionMatchIdentity.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

namespace MphRead::Mods::Network
{
    std::uint16_t SessionMatchIdentity::MatchId(bool host) const noexcept
    {
        return _state.has_value() ? _state->MatchId : (host ? std::uint16_t{1} : std::uint16_t{0});
    }

    std::uint64_t SessionMatchIdentity::AuthorityEpoch(bool host) const noexcept
    {
        return _state.has_value() ? _state->AuthorityEpoch : (host ? 1ULL : 0ULL);
    }

    SessionMatchIdentity::Transition SessionMatchIdentity::Apply(const MatchStatePacket& state)
    {
        Transition result{};
        result.HadPrevious = _state.has_value();
        if (state.MatchId == 0 || state.AuthorityEpoch == 0)
        {
            result.Rejected = Rejection::InvalidIdentity;
            return result;
        }

        if (_state.has_value())
        {
            if (state.AuthorityEpoch != _state->AuthorityEpoch
                && !NetLifecycleTracker::Newer(state.AuthorityEpoch, _state->AuthorityEpoch))
            {
                result.Rejected = Rejection::StaleAuthority;
                return result;
            }
            if (state.AuthorityEpoch == _state->AuthorityEpoch && state.MatchId != _state->MatchId
                && !NetLifecycleTracker::Newer(state.MatchId, _state->MatchId))
            {
                result.Rejected = Rejection::StaleMatch;
                return result;
            }
            if (state.AuthorityEpoch == _state->AuthorityEpoch && state.MatchId == _state->MatchId
                && _state->Ending() && !state.Ending())
            {
                result.Rejected = Rejection::EndingRegression;
                return result;
            }
        }

        result.NewMatch = !_state.has_value() || state.MatchId != _state->MatchId;
        result.NewAuthority = !_state.has_value() || state.AuthorityEpoch != _state->AuthorityEpoch;
        result.RoomChanged = !_state.has_value() || state.RoomKey != _state->RoomKey;
        _state = state;
        return result;
    }
}
