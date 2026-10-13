#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns ordering and reset state for snapshots, rosters, and remote intents.
    // Packet validation and the effects of accepted packets remain with NetSession.
    class ClientReplicaStream final
    {
    public:
        static constexpr std::size_t SlotCapacity = static_cast<std::size_t>(RosterPacket::MaxSlots);

        [[nodiscard]] std::uint32_t LastSnapshotFrame() const noexcept { return _lastSnapshotFrame; }
        [[nodiscard]] std::uint32_t SnapshotArrived() const noexcept { return _snapshotArrived; }
        [[nodiscard]] std::uint32_t AppliedSnapshotFrame() const noexcept { return _appliedSnapshotFrame; }
        [[nodiscard]] std::uint32_t RosterRevision() const noexcept { return _rosterRevision; }
        [[nodiscard]] std::int32_t StreamResets() const noexcept { return _streamResets; }
        [[nodiscard]] std::int64_t SnapshotsOutOfOrder() const noexcept { return _snapshotsOutOfOrder; }
        [[nodiscard]] std::int64_t IntentsOutOfOrder() const noexcept { return _intentsOutOfOrder; }

        [[nodiscard]] std::uint32_t SnapshotAge(std::uint32_t netFrame) const noexcept;
        [[nodiscard]] bool AcceptSnapshot(std::uint32_t frame, std::uint32_t netFrame) noexcept;
        [[nodiscard]] bool AcceptRosterRevision(std::uint32_t revision) const noexcept;
        void CommitRosterRevision(std::uint32_t revision) noexcept;
        [[nodiscard]] std::uint32_t NextRosterRevision() noexcept;
        [[nodiscard]] bool AcceptIntent(std::int32_t slot, std::uint32_t frame) noexcept;
        void NoteStatesApplied() noexcept { _appliedSnapshotFrame = _lastSnapshotFrame; }
        void ForgetSlot(std::int32_t slot) noexcept;

        // Match reset preserves roster revision and diagnostics, as the existing session does.
        void ResetMatch() noexcept;
        // Lobby reset invalidates only the replicated roster sequence.
        void ResetLobby() noexcept;
        // A new match/authority stream invalidates in-flight replica sequences and records the reset.
        void BeginStreamTransition() noexcept;
        // Demo rewind resets playback-visible ordering but retains the stream-reset diagnostic.
        void RewindPlayback() noexcept;
        // Stop clears all stream state and counters for the next session.
        void Stop() noexcept;

    private:
        std::array<std::uint32_t, SlotCapacity> _lastIntentFrame{};
        std::uint32_t _lastSnapshotFrame = 0;
        std::uint32_t _snapshotArrived = 0;
        std::uint32_t _appliedSnapshotFrame = 0;
        std::uint32_t _rosterRevision = 0;
        std::int32_t _streamResets = 0;
        std::int64_t _snapshotsOutOfOrder = 0;
        std::int64_t _intentsOutOfOrder = 0;
        bool _hasSnapshot = false;
        bool _hasRoster = false;
    };
}
