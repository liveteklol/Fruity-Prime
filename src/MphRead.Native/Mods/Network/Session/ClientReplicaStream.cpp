#include "Mods/Network/Session/ClientReplicaStream.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <limits>

namespace MphRead::Mods::Network
{
    std::uint32_t ClientReplicaStream::SnapshotAge(std::uint32_t netFrame) const noexcept
    {
        return _snapshotArrived == 0
            ? std::numeric_limits<std::uint32_t>::max()
            : netFrame >= _snapshotArrived ? netFrame - _snapshotArrived : 0U;
    }

    bool ClientReplicaStream::AcceptSnapshot(std::uint32_t frame, std::uint32_t netFrame) noexcept
    {
        if (_hasSnapshot && !NetLifecycleTracker::Newer(frame, _lastSnapshotFrame))
        {
            ::MphRead::NativeRuntime::IncrementInPlace(_snapshotsOutOfOrder);
            return false;
        }
        _hasSnapshot = true;
        _lastSnapshotFrame = frame;
        _snapshotArrived = std::max(netFrame, 1U);
        return true;
    }

    bool ClientReplicaStream::AcceptRosterRevision(std::uint32_t revision) const noexcept
    {
        return !_hasRoster || NetLifecycleTracker::Newer(revision, _rosterRevision);
    }

    void ClientReplicaStream::CommitRosterRevision(std::uint32_t revision) noexcept
    {
        _hasRoster = true;
        _rosterRevision = revision;
    }

    std::uint32_t ClientReplicaStream::NextRosterRevision() noexcept
    {
        return ++_rosterRevision;
    }

    bool ClientReplicaStream::AcceptIntent(std::int32_t slot, std::uint32_t frame) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity))
        {
            return false;
        }
        const auto index = static_cast<std::size_t>(slot);
        const std::uint32_t previous = _lastIntentFrame[index];
        if (previous != 0 && !NetLifecycleTracker::Newer(frame, previous))
        {
            ::MphRead::NativeRuntime::IncrementInPlace(_intentsOutOfOrder);
            return false;
        }
        _lastIntentFrame[index] = frame;
        return true;
    }

    void ClientReplicaStream::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot >= 0 && slot < static_cast<std::int32_t>(SlotCapacity))
        {
            _lastIntentFrame[static_cast<std::size_t>(slot)] = 0;
        }
    }

    void ClientReplicaStream::ResetMatch() noexcept
    {
        _lastIntentFrame.fill(0);
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _hasSnapshot = false;
    }

    void ClientReplicaStream::ResetLobby() noexcept
    {
        _rosterRevision = 0;
        _hasRoster = false;
    }

    void ClientReplicaStream::BeginStreamTransition() noexcept
    {
        _hasSnapshot = false;
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _hasRoster = false;
        _lastIntentFrame.fill(0);
        ::MphRead::NativeRuntime::IncrementInPlace(_streamResets);
    }

    void ClientReplicaStream::RewindPlayback() noexcept
    {
        _hasRoster = false;
        _rosterRevision = 0;
        _hasSnapshot = false;
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _lastIntentFrame.fill(0);
        _snapshotsOutOfOrder = 0;
        _intentsOutOfOrder = 0;
    }

    void ClientReplicaStream::Stop() noexcept
    {
        _lastIntentFrame.fill(0);
        _lastSnapshotFrame = 0;
        _snapshotArrived = 0;
        _appliedSnapshotFrame = 0;
        _rosterRevision = 0;
        _streamResets = 0;
        _snapshotsOutOfOrder = 0;
        _intentsOutOfOrder = 0;
        _hasSnapshot = false;
        _hasRoster = false;
    }
}
