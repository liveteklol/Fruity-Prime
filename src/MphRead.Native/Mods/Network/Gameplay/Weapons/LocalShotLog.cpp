#include "Mods/Network/Gameplay/Weapons/LocalShotLog.hpp"

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

    void LocalShotLog::Fill(IntentPacket& intent, std::uint32_t now) const noexcept
    {
        intent.ShotHistory = {};
        intent.ShotHistoryLength = 0;
        for (std::size_t i = 0; i < _length; ++i)
        {
            if (now - _history[i].Frame <= RepeatFrames)
            {
                intent.ShotHistory[intent.ShotHistoryLength++] = _history[i];
            }
        }
    }

    void LocalShotLog::Reset() noexcept
    {
        _history = {};
        _length = 0;
    }
}
