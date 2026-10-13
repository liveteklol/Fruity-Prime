#include "Mods/Network/Server/Core/ServerPeerIntentState.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

namespace MphRead::Mods::Network
{
    ServerPeerIntentState::Disposition ServerPeerIntentState::Observe(
        std::uint32_t frame, bool hasShotHistory) noexcept
    {
        if (_lastFrame == 0 || NetLifecycleTracker::Newer(frame, _lastFrame))
        {
            _lastFrame = frame;
            return Disposition::Newest;
        }
        return hasShotHistory ? Disposition::StaleWithShots : Disposition::StaleWithoutShots;
    }
}
