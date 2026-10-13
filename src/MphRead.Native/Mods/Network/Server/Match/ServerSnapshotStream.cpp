#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    bool ServerSnapshotStream::CanAcceptIncoming(std::uint32_t frame) const noexcept
    {
        return !_incomingSeen || NetLifecycleTracker::Newer(frame, _incomingFrame);
    }

    bool ServerSnapshotStream::CacheLatest(std::span<const std::uint8_t> payload) noexcept
    {
        if (payload.size() > _latest.size())
        {
            return false;
        }
        std::copy(payload.begin(), payload.end(), _latest.begin());
        _latestSize = payload.size();
        return true;
    }

    bool ServerSnapshotStream::AcceptIncoming(std::uint32_t frame,
        std::span<const std::uint8_t> payload) noexcept
    {
        if (!CanAcceptIncoming(frame) || !CacheLatest(payload))
        {
            return false;
        }
        _incomingSeen = true;
        _incomingFrame = frame;
        return true;
    }

    void ServerSnapshotStream::ResetIncomingSequence() noexcept
    {
        _incomingFrame = 0;
        _incomingSeen = false;
    }
}
