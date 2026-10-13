#include "Mods/Network/Session/ClientReplicaStream.hpp"

#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::ClientReplicaStream;

    bool passed = true;
    ClientReplicaStream stream;
    passed &= Check(stream.SnapshotAge(0) == std::numeric_limits<std::uint32_t>::max()
        && stream.LastSnapshotFrame() == 0 && stream.SnapshotArrived() == 0
        && stream.AppliedSnapshotFrame() == 0, "initial snapshot stream state");

    passed &= Check(stream.AcceptSnapshot(40, 0) && stream.LastSnapshotFrame() == 40
        && stream.SnapshotArrived() == 1 && stream.SnapshotAge(0) == 0,
        "first snapshot uses a nonzero arrival frame");
    stream.NoteStatesApplied();
    passed &= Check(stream.AppliedSnapshotFrame() == 40, "applied marker follows newest accepted snapshot");
    passed &= Check(!stream.AcceptSnapshot(40, 10) && !stream.AcceptSnapshot(39, 10)
        && stream.SnapshotsOutOfOrder() == 2 && stream.LastSnapshotFrame() == 40,
        "duplicate and stale snapshots are rejected and counted");
    ClientReplicaStream wrapping;
    passed &= Check(wrapping.AcceptSnapshot(std::numeric_limits<std::uint32_t>::max() - 1, 40)
        && wrapping.AcceptSnapshot(0, 41) && wrapping.LastSnapshotFrame() == 0,
        "snapshot frame serial arithmetic accepts wraparound");

    passed &= Check(stream.AcceptRosterRevision(12), "first roster revision is accepted");
    stream.CommitRosterRevision(12);
    passed &= Check(!stream.AcceptRosterRevision(12) && !stream.AcceptRosterRevision(11),
        "roster revision ordering rejects duplicates and stale values");
    ClientReplicaStream wrappingRoster;
    wrappingRoster.CommitRosterRevision(std::numeric_limits<std::uint32_t>::max());
    passed &= Check(wrappingRoster.AcceptRosterRevision(0),
        "roster revision serial arithmetic accepts wraparound");

    passed &= Check(stream.AcceptIntent(1, 30) && !stream.AcceptIntent(1, 30)
        && stream.AcceptIntent(2, 1) && !stream.AcceptIntent(-1, 99)
        && stream.IntentsOutOfOrder() == 1, "intent ordering is per-slot and rejects invalid slots");

    stream.BeginStreamTransition();
    passed &= Check(stream.StreamResets() == 1 && stream.LastSnapshotFrame() == 0
        && stream.SnapshotArrived() == 0 && stream.AppliedSnapshotFrame() == 0
        && stream.SnapshotsOutOfOrder() == 2 && stream.IntentsOutOfOrder() == 1,
        "authority transition clears stream frames and retains session diagnostics");
    passed &= Check(stream.AcceptRosterRevision(12),
        "authority transition invalidates roster acceptance without changing its revision value");
    passed &= Check(stream.AcceptIntent(1, 1), "authority transition clears per-slot intent frames");

    stream.CommitRosterRevision(12);
    passed &= Check(stream.AcceptSnapshot(70, 75), "new match accepts a lower snapshot frame");
    stream.NoteStatesApplied();
    stream.ResetMatch();
    passed &= Check(stream.LastSnapshotFrame() == 0 && stream.SnapshotArrived() == 0
        && stream.AppliedSnapshotFrame() == 0 && !stream.AcceptRosterRevision(12)
        && stream.SnapshotsOutOfOrder() == 2 && stream.StreamResets() == 1,
        "match reset clears snapshots while preserving roster and diagnostics");
    passed &= Check(stream.AcceptIntent(1, 1), "match reset clears intent sequence state");

    stream.ResetLobby();
    passed &= Check(stream.AcceptRosterRevision(1), "lobby reset clears the roster revision gate");
    stream.RewindPlayback();
    passed &= Check(stream.StreamResets() == 1 && stream.SnapshotsOutOfOrder() == 0
        && stream.IntentsOutOfOrder() == 0 && stream.RosterRevision() == 0
        && stream.AcceptRosterRevision(1), "playback rewind resets replay diagnostics and roster state");

    stream.BeginStreamTransition();
    passed &= Check(stream.AcceptSnapshot(90, 100) && stream.AcceptIntent(3, 17),
        "new stream accepts initial snapshot and intent");
    stream.CommitRosterRevision(8);
    stream.Stop();
    passed &= Check(stream.StreamResets() == 0 && stream.SnapshotsOutOfOrder() == 0
        && stream.IntentsOutOfOrder() == 0 && stream.LastSnapshotFrame() == 0
        && stream.SnapshotArrived() == 0 && stream.AppliedSnapshotFrame() == 0
        && stream.RosterRevision() == 0 && stream.AcceptRosterRevision(8)
        && stream.AcceptIntent(3, 1), "stop resets all owned stream state for the next session");
    return passed ? 0 : 1;
}
