#include "Mods/Network/Gameplay/Player/PlayerCombatPolicy.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotEventEligibility.hpp"
#include "Mods/SpectatorCameraPolicy.hpp"
#include "Mods/SpectatorTargetPolicy.hpp"

#include <array>
#include <iostream>
#include <span>
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
    using namespace MphRead::Mods;
    using MphRead::Mods::Network::PlayerCombatPolicy;
    using MphRead::Mods::Network::ShotEventEligibility;
    using MphRead::Mods::Network::ShotEventEligibilityTimeline;
    bool passed = true;

    constexpr SpectatorSlotSnapshot alive{true, true, true, false, true, 100, 4, 8};
    passed &= Check(SpectatorTargetPolicy::IsEligible(alive, 1, 0, true),
        "an occupied, live network player can be followed");
    passed &= Check(!SpectatorTargetPolicy::IsEligible(alive, 0, 0, true),
        "the local player is never selected as a remote target");

    auto invalid = alive;
    invalid.Active = false;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "inactive slots cannot be followed");
    invalid = alive;
    invalid.Spawned = false;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "unspawned slots cannot be followed");
    invalid = alive;
    invalid.Health = 0;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "dead players cannot be followed");
    invalid = alive;
    invalid.Spectating = true;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "another spectator is not a combat target");
    invalid = alive;
    invalid.RosterOccupied = false;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "a stale entity outside the current roster cannot be followed");
    invalid = alive;
    invalid.Generation = 0;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "an unassigned network generation cannot be followed");
    invalid = alive;
    invalid.LifeId = 0;
    passed &= Check(!SpectatorTargetPolicy::IsEligible(invalid, 1, 0, true),
        "a network slot without a current life cannot be followed");
    invalid = alive;
    invalid.RosterOccupied = false;
    passed &= Check(SpectatorTargetPolicy::IsEligible(invalid, 1, 0, false),
        "offline multiplayer does not require a network roster");

    constexpr std::array<SpectatorSlotSnapshot, 4> slots{{
        {}, alive, {}, SpectatorSlotSnapshot{true, true, true, false, true, 100, 9, 2}}};
    constexpr std::array<SpectatorSlotSnapshot, 4> onlyCurrent{{{}, alive, {}, {}}};
    passed &= Check(SpectatorTargetPolicy::Find({}, 0, 0, true, true) == -1,
        "empty player lists are safe");
    passed &= Check(SpectatorTargetPolicy::Find(slots, 1, 0, true, true) == 3,
        "next-target search wraps past the current slot");
    passed &= Check(SpectatorTargetPolicy::Find(slots, 3, 0, true, false) == 1,
        "previous-target search wraps past the current slot");
    passed &= Check(SpectatorTargetPolicy::Find(onlyCurrent, 1, 0, true, false) == -1,
        "search does not reselect its only current target");
    passed &= Check(SpectatorTargetPolicy::Find(slots, -1, 0, true, true) == 1,
        "negative next-search origins normalize safely");
    passed &= Check(SpectatorTargetPolicy::Find(slots, 99, 0, true, false) == 3,
        "out-of-range previous-search origins normalize safely");

    constexpr SpectatorTargetIdentity identity{1, 4, 8};
    passed &= Check(SpectatorTargetPolicy::Matches(identity, alive, 1, 0, true),
        "the tracked target matches its current roster generation and life");
    auto reused = alive;
    reused.Generation = 5;
    passed &= Check(!SpectatorTargetPolicy::Matches(identity, reused, 1, 0, true),
        "a reused slot does not revive an old target generation");
    reused = alive;
    reused.LifeId = 9;
    passed &= Check(!SpectatorTargetPolicy::Matches(identity, reused, 1, 0, true),
        "a respawn does not revive an old target life");

    passed &= Check(PlayerCombatPolicy::IsInPlay(true, true, 100, false),
        "active spawned living players participate in combat");
    passed &= Check(!PlayerCombatPolicy::IsInPlay(false, true, 100, false)
        && !PlayerCombatPolicy::IsInPlay(true, false, 100, false)
        && !PlayerCombatPolicy::IsInPlay(true, true, 0, false)
        && !PlayerCombatPolicy::IsInPlay(true, true, 100, true),
        "inactive, unspawned, dead, and spectating players are outside combat");
    passed &= Check(!PlayerCombatPolicy::IntentIsInPlay(true, true)
        && !PlayerCombatPolicy::IntentIsInPlay(false, false)
        && PlayerCombatPolicy::IntentIsInPlay(true, false),
        "spectating input safely overrides a contradictory in-play bit");
    passed &= Check(!PlayerCombatPolicy::CanReceiveDamage(true)
        && PlayerCombatPolicy::CanReceiveDamage(false),
        "spectating players reject damage at the shared final gate");
    passed &= Check(PlayerCombatPolicy::IsAliveAndSpawned(true, true, 1)
        && !PlayerCombatPolicy::IsAliveAndSpawned(true, false, 1)
        && !PlayerCombatPolicy::IsAliveAndSpawned(true, true, 0),
        "a rejoin request requires an active spawned player with health");

    passed &= Check(!SpectatorCameraPolicy::CanFollowPlayer(true, true, false, false)
        && !SpectatorCameraPolicy::CanFollowPlayer(true, true, true, false)
        && SpectatorCameraPolicy::CanFollowPlayer(true, true, true, true)
        && SpectatorCameraPolicy::CanFollowPlayer(false, true, false, false),
        "player-camera follow requires a current target while spectating");
    passed &= Check(SpectatorCameraPolicy::CanBorrowLocalPose(true, false, true, true)
        && !SpectatorCameraPolicy::CanBorrowLocalPose(true, true, true, true)
        && !SpectatorCameraPolicy::CanBorrowLocalPose(false, false, true, true),
        "the local pose is available only to initialize targetless free camera");

    ShotEventEligibilityTimeline timeline;
    timeline.Record(3, 7, 100, true);
    timeline.Record(3, 7, 110, false);
    passed &= Check(timeline.EvaluateFrame(3, 99) == ShotEventEligibility::Allowed
        && timeline.Evaluate(3, 7, 99) == ShotEventEligibility::Allowed,
        "a delayed pre-sample shot remains eligible when the first accepted state is in play");
    passed &= Check(timeline.Evaluate(3, 7, 105) == ShotEventEligibility::Allowed
        && timeline.Evaluate(3, 7, 109) == ShotEventEligibility::Allowed,
        "a delayed shot fired before the accepted spectator boundary remains eligible");
    passed &= Check(timeline.Evaluate(3, 7, 110) == ShotEventEligibility::Denied
        && timeline.EvaluateFrame(3, 110) == ShotEventEligibility::Denied,
        "shots fired at the accepted spectator boundary are denied");
    timeline.Record(3, 7, 105, true);
    passed &= Check(timeline.Evaluate(3, 7, 110) == ShotEventEligibility::Denied
        && !timeline.LatestAllowsContinuousFire(3, 7),
        "an older spoofed in-play report cannot replace accepted spectator state");
    timeline.Record(3, 7, 120, false);
    timeline.Record(3, 7, 130, true);
    passed &= Check(timeline.Evaluate(3, 7, 125) == ShotEventEligibility::Denied
        && timeline.Evaluate(3, 7, 130) == ShotEventEligibility::Allowed
        && timeline.Evaluate(3, 7, 131) == ShotEventEligibility::Pending,
        "an accepted rejoin opens eligibility at its sender frame");
    timeline.Record(3, 8, 140, true);
    passed &= Check(timeline.EvaluateFrame(3, 105) == ShotEventEligibility::Allowed
        && timeline.EvaluateFrame(3, 110) == ShotEventEligibility::Denied
        && timeline.Evaluate(3, 7, 105) == ShotEventEligibility::Allowed
        && timeline.Evaluate(3, 8, 135) == ShotEventEligibility::Denied
        && timeline.Evaluate(3, 8, 140) == ShotEventEligibility::Allowed,
        "life changes preserve frame-accurate eligibility for delayed shots");
    passed &= Check(timeline.EvaluateFrame(3, 141) == ShotEventEligibility::Pending
        && timeline.Evaluate(3, 8, 141) == ShotEventEligibility::Pending,
        "unaccepted future shot frames wait for an authority-accepted intent");
    passed &= Check(timeline.Evaluate(4, 8, 140) == ShotEventEligibility::Pending,
        "eligibility history is isolated by roster generation");
    passed &= Check(!timeline.LatestAllowsContinuousFire(3, 7)
        && timeline.LatestAllowsContinuousFire(3, 8)
        && !timeline.LatestAllowsContinuousFire(4, 8),
        "continuous fire requires the latest accepted in-play state for the same identity");
    passed &= Check(timeline.EvaluateFrame(3, 105) == ShotEventEligibility::Allowed
        && timeline.EvaluateFrame(3, 125) == ShotEventEligibility::Denied
        && timeline.EvaluateFrame(3, 130) == ShotEventEligibility::Allowed,
        "shot eligibility remains tied to the correct life across a rejoin boundary");
    timeline.Record(3, 7, 140, false);
    passed &= Check(!timeline.LatestAllowsContinuousFire(3, 7),
        "continuous fire stops when the latest accepted intent is not in play");

    ShotEventEligibilityTimeline wrappedTimeline;
    wrappedTimeline.Record(1, 1, 0xFFFFFFFEU, true);
    wrappedTimeline.Record(1, 1, 1, false);
    passed &= Check(wrappedTimeline.Evaluate(1, 1, 0xFFFFFFFFU) == ShotEventEligibility::Allowed
        && wrappedTimeline.Evaluate(1, 1, 1) == ShotEventEligibility::Denied
        && wrappedTimeline.Evaluate(1, 1, 2) == ShotEventEligibility::Pending,
        "eligibility transitions remain ordered across sender-frame wraparound");

    ShotEventEligibilityTimeline conflictingFrame;
    conflictingFrame.Record(1, 1, 20, true);
    conflictingFrame.Record(1, 1, 20, false);
    passed &= Check(conflictingFrame.Evaluate(1, 1, 20) == ShotEventEligibility::Denied,
        "conflicting accepted state for one sender frame fails closed");

    ShotEventEligibilityTimeline initiallySpectating;
    initiallySpectating.Record(1, 4, 50, false);
    passed &= Check(initiallySpectating.EvaluateFrame(1, 49) == ShotEventEligibility::Denied
        && initiallySpectating.Evaluate(1, 4, 49) == ShotEventEligibility::Denied,
        "an initial spectator state cannot authorize a shot from an unknown earlier frame");

    ShotEventEligibilityTimeline claimWindow;
    claimWindow.Record(1, 1, 1, true);
    for (std::uint32_t frame = 2; frame <= 124; ++frame)
    {
        claimWindow.Record(1, 1, frame, (frame % 2) != 0);
    }
    passed &= Check(claimWindow.EvaluateFrame(1, 2) == ShotEventEligibility::Denied
        && claimWindow.EvaluateFrame(1, 124) == ShotEventEligibility::Denied,
        "spectator boundaries remain available throughout the full hit-claim rewind age");

    return passed ? 0 : 1;
}
