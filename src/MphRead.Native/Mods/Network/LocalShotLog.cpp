#include "LocalShotLog.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    std::uint32_t LocalShotLog::Record(IntentPacket::ShotEvent shot) noexcept
    {
        _sequence = Advance(_sequence);
        shot.Sequence = _sequence;
        if (_length == _history.size())
        {
            std::rotate(_history.begin(), _history.begin() + 1, _history.end());
            --_length;
        }
        _history[_length++] = shot;
        return _sequence;
    }

    void LocalShotLog::Fill(IntentPacket& intent) const noexcept
    {
        intent.ShotHistory = _history;
        intent.ShotHistoryLength = _length;
    }

    void LocalShotLog::Reset() noexcept
    {
        _history = {};
        _length = 0;
    }
}
