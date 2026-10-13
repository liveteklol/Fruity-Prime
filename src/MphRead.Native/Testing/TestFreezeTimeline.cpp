#include "Mods/Network/Testing/Harness/FreezeTimeline.hpp"

#include <cstdint>
#include <iostream>

namespace
{
    using MphRead::Mods::Network::FreezeTimeline;

    bool Check(bool condition, const char* name)
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
    FreezeTimeline timeline;
    bool passed = true;
    timeline.Observe(2, true, 10, true, true, 10, 1, 100, 10, 4);
    timeline.Observe(2, true, 9, true, true, 10, 1, 100, 11, 4);
    timeline.Observe(2, true, 2, true, true, 2, 2, 107, 17, 4);
    timeline.Observe(2, true, 1, true, true, 1, 3, 109, 19, 4);
    timeline.Observe(2, true, 0, true, true, 0, 4, 110, 20, 4);
    const FreezeTimeline::SlotStats& thawed = timeline.At(2);
    passed &= Check(thawed.AppliedFrozenFrames == 4
        && thawed.AppliedEpisodesStarted == 1
        && thawed.AppliedEpisodesCompleted == 1
        && thawed.AppliedLocalEpisodesCompleted == 1
        && thawed.AppliedDurationMin == 10 && thawed.AppliedDurationMax == 10
        && thawed.AppliedLocalDurationMin == 4 && thawed.AppliedLocalDurationMax == 4
        && thawed.AppliedLastRemaining == 0
        && thawed.AppliedEpisodeStartFrame == 10 && thawed.AppliedLastThawFrame == 20
        && thawed.AppliedAuthorityEpisodeStartFrame == 100 && thawed.AppliedAuthorityLastThawFrame == 110
        && thawed.AppliedEpisodeOpenFrames == 0,
        "applied duration separates local samples from the authority frame timeline");
    passed &= Check(thawed.AuthoritySnapshots == 4
        && thawed.AuthorityEpisodesStarted == 1 && thawed.AuthorityThaws == 1
        && thawed.AuthorityDurationMin == 10 && thawed.AuthorityDurationMax == 10
        && thawed.AuthorityFrozenSamples == 3 && thawed.AuthorityRemainingDeltaMax == 0
        && thawed.AuthorityEpisodeStartFrame == 100 && thawed.AuthorityLastThawFrame == 110
        && !thawed.AuthorityEpisodeOpen,
        "authority duration uses unique snapshots and server frames through thaw");

    timeline.Observe(2, true, 3, true, true, 3, 5, 120, 25, 4);
    timeline.Observe(2, true, 2, false, false, 0, 5, 120, 26, 4);
    const FreezeTimeline::SlotStats& open = timeline.At(2);
    passed &= Check(open.AppliedEpisodesStarted == 2 && open.AppliedEpisodesCompleted == 1
        && open.AppliedEpisodeOpenFrames == 2,
        "an unfinished applied freeze remains visible as an open interval");
    passed &= Check(open.AuthorityEpisodesStarted == 2 && open.AuthorityThaws == 1
        && open.AuthorityEpisodesInterrupted == 1 && !open.AuthorityEpisodeOpen
        && open.AuthorityLastRemaining == 0,
        "a missing authority snapshot interrupts an episode without fabricating a thaw or retaining stale time");
    passed &= Check(!timeline.HasObservations(3) && timeline.HasObservations(2),
        "timeline activity is isolated by player slot");

    timeline.Observe(3, true, 1, true, true, 1, 8, 0xFFFFFFFEU, 30, 1);
    timeline.Observe(3, true, 0, true, true, 0, 9, 2, 31, 1);
    const FreezeTimeline::SlotStats& wrapped = timeline.At(3);
    passed &= Check(wrapped.AuthorityThaws == 1 && wrapped.AuthorityDurationMin == 4
        && wrapped.AuthorityDurationMax == 4,
        "authority duration remains valid when the 60 Hz frame counter wraps");

    timeline.Observe(4, true, 4, true, true, 4, 20, 0xFFFFFFFEU, 20, 1);
    timeline.Observe(4, true, 1, true, true, 1, 21, 1, 23, 1);
    timeline.Observe(4, true, 0, true, true, 0, 22, 2, 24, 1);
    const FreezeTimeline::SlotStats& wrappedApplied = timeline.At(4);
    passed &= Check(wrappedApplied.AppliedEpisodesCompleted == 1
        && wrappedApplied.AppliedLocalEpisodesCompleted == 1
        && wrappedApplied.AppliedLocalDurationMin == 2
        && wrappedApplied.AppliedDurationMin == 4
        && wrappedApplied.AuthorityFrozenSamples == 2
        && wrappedApplied.AuthorityRemainingDeltaMax == 0
        && wrappedApplied.AppliedAuthorityLastThawFrame == 2,
        "applied authority-clock duration remains valid when both frame counters wrap");

    FreezeTimeline mismatch;
    mismatch.Observe(1, true, 4, true, true, 10, 1, 200, 80, 1);
    passed &= Check(mismatch.At(1).AuthorityFrozenSamples == 1
        && mismatch.At(1).AuthorityRemainingDeltaMax == 6,
        "a newly applied authority-frozen snapshot exposes the timer discrepancy");

    FreezeTimeline betweenSnapshots;
    betweenSnapshots.Observe(1, true, 10, true, true, 10, 30, 300, 900, 4);
    betweenSnapshots.Observe(1, true, 6, true, true, 10, 30, 300, 904, 4);
    passed &= Check(betweenSnapshots.At(1).AuthorityFrozenSamples == 1
        && betweenSnapshots.At(1).AuthorityRemainingDeltaMax == 0,
        "local countdown between snapshots is not reported as authority timer drift");
    betweenSnapshots.Observe(1, true, 5, true, true, 5, 31, 305, 905, 4);
    passed &= Check(betweenSnapshots.At(1).AuthorityFrozenSamples == 2
        && betweenSnapshots.At(1).AuthorityRemainingDeltaMax == 0,
        "the next snapshot compares the applied timer against its matching authority state");

    FreezeTimeline delayedSnapshot;
    delayedSnapshot.Observe(1, true, 112, true, true, 120, 31, 400, 910, 4, 8);
    passed &= Check(delayedSnapshot.At(1).AuthorityFrozenSamples == 1
        && delayedSnapshot.At(1).AuthorityRemainingDeltaMax == 0,
        "freeze timer acceptance compares the authority timer after snapshot age projection");

    timeline.Observe(5, true, 5, true, true, 5, 10, 50, 40, 8);
    timeline.Observe(5, true, 2, true, true, 2, 11, 53, 43, 8);
    timeline.Observe(5, true, 5, true, true, 5, 12, 54, 44, 9);
    const FreezeTimeline::SlotStats& rejoined = timeline.At(5);
    passed &= Check(rejoined.OccupantChanges == 1 && rejoined.HighestOccupantGeneration == 9
        && rejoined.AppliedEpisodesInterrupted == 1
        && rejoined.AppliedEpisodeOpenFrames == 1
        && rejoined.AppliedLastRemaining == 5,
        "a new occupant interrupts the prior local freeze and starts a fresh interval");
    passed &= Check(rejoined.AuthorityEpisodesInterrupted == 1
        && rejoined.AuthorityEpisodeOpen && rejoined.AuthorityEpisodesStarted == 2,
        "a new occupant cannot inherit the prior authority freeze or fabricate a thaw");
    timeline.Observe(5, true, 0, true, true, 0, 13, 60, 50, 9);
    const FreezeTimeline::SlotStats& rejoinedThawed = timeline.At(5);
    passed &= Check(rejoinedThawed.OccupantChanges == 1
        && rejoinedThawed.AppliedEpisodesInterrupted == 1
        && rejoinedThawed.AppliedEpisodesCompleted == 1
        && rejoinedThawed.AppliedLocalDurationMin == 1
        && rejoinedThawed.AppliedDurationMin == 6
        && rejoinedThawed.AppliedLastRemaining == 0,
        "the rejoined occupant's applied freeze closes on its own authority-timed thaw");
    passed &= Check(rejoinedThawed.AuthorityEpisodesInterrupted == 1
        && rejoinedThawed.AuthorityThaws == 1
        && rejoinedThawed.AuthorityDurationMin == 6
        && rejoinedThawed.AuthorityLastRemaining == 0
        && !rejoinedThawed.AuthorityEpisodeOpen,
        "the rejoined occupant's authority freeze closes independently");

    FreezeTimeline rejoinOnly;
    rejoinOnly.Observe(6, true, 0, false, false, 0, 0, 0, 1, 3);
    passed &= Check(rejoinOnly.HasObservations(6)
        && rejoinOnly.At(6).AppliedLastRemaining == 0,
        "an active occupant with no freeze remains visible to acceptance reports");
    rejoinOnly.Observe(6, true, 0, false, false, 0, 0, 0, 2, 4);
    passed &= Check(rejoinOnly.HasObservations(6)
        && rejoinOnly.At(6).OccupantChanges == 1
        && rejoinOnly.At(6).HighestOccupantGeneration == 4,
        "a rejoin remains reportable even when neither occupant froze");
    rejoinOnly.Observe(6, false, 0, false, false, 0, 0, 0, 3, 0);
    passed &= Check(rejoinOnly.At(6).OccupantChanges == 2
        && rejoinOnly.At(6).HighestOccupantGeneration == 4,
        "leaving the slot preserves the highest occupant generation observed");

    FreezeTimeline outOfPlay;
    outOfPlay.Observe(1, true, 8, true, true, 8, 1, 10, 1, 2);
    outOfPlay.Observe(1, false, 0, true, false, 35, 2, 11, 2, 2);
    const FreezeTimeline::SlotStats& dead = outOfPlay.At(1);
    passed &= Check(dead.AuthorityFrozenSamples == 1
        && dead.AuthorityFrozenExcludedSamples == 1
        && dead.AuthorityRemainingDeltaMax == 0
        && dead.AuthorityRemainingDeltaMaxExcluded == 35
        && dead.AuthorityEpisodesInterrupted == 1
        && dead.AuthorityThaws == 0 && !dead.AuthorityEpisodeOpen,
        "dead-state timer differences are diagnostic and death does not count as a thaw");
    passed &= Check(dead.AppliedEpisodesInterrupted == 1
        && dead.AppliedEpisodesCompleted == 0 && dead.AppliedLastRemaining == 0,
        "a locally dead player interrupts the applied freeze instead of completing it");
    passed &= Check(dead.AuthorityLastRemaining == 0,
        "a dead authority state clears its exposed remaining timer without counting a thaw");

    FreezeTimeline occupantReplacement;
    occupantReplacement.Observe(7, true, 5, true, true, 5, 1, 100, 1, 4);
    occupantReplacement.Observe(7, true, 90, true, true, 30, 2, 101, 2, 5);
    const FreezeTimeline::SlotStats& replaced = occupantReplacement.At(7);
    passed &= Check(replaced.AuthorityFrozenSamples == 1
        && replaced.AuthorityFrozenExcludedSamples == 1
        && replaced.AuthorityRemainingDeltaMax == 0
        && replaced.AuthorityRemainingDeltaMaxExcluded == 60
        && replaced.OccupantChanges == 1,
        "a slot replacement cannot compare the previous occupant's timer with the new local occupant");
    occupantReplacement.Observe(7, true, 29, true, true, 29, 3, 102, 3, 5);
    const FreezeTimeline::SlotStats& replacementSnapshot = occupantReplacement.At(7);
    passed &= Check(replacementSnapshot.AuthorityFrozenSamples == 2
        && replacementSnapshot.AuthorityRemainingDeltaMax == 0
        && replacementSnapshot.AuthorityFrozenExcludedSamples == 1,
        "timer acceptance resumes on the replacement occupant's next authority snapshot");

    timeline.Reset();
    passed &= Check(!timeline.HasObservations(2), "session reset clears every slot timeline");

    if (!passed)
    {
        return 1;
    }
    std::cout << "FreezeTimeline PASS\n";
    return 0;
}
