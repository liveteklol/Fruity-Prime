#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"

#include <utility>

namespace MphRead::Mods::Network
{
    void SessionSimulationCallbacks::Configure(
        SnapshotSink snapshotSink, std::function<void()> matchEnded)
    {
        _snapshotSink = std::move(snapshotSink);
        _matchEnded = std::move(matchEnded);
    }

    void SessionSimulationCallbacks::Reset() noexcept
    {
        _snapshotSink = {};
        _matchEnded = {};
    }

    bool SessionSimulationCallbacks::HasSnapshotSink() const noexcept
    {
        return static_cast<bool>(_snapshotSink);
    }

    void SessionSimulationCallbacks::PublishSnapshot(
        std::span<const std::uint8_t> payload) const
    {
        if (_snapshotSink)
        {
            _snapshotSink(payload);
        }
    }

    void SessionSimulationCallbacks::NotifyMatchEnded() const
    {
        if (_matchEnded)
        {
            _matchEnded();
        }
    }
}
