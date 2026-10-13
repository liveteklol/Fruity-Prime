#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Testing/NetSessionFixture.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>

using namespace MphRead::Mods::Network;
using OpenTK::Mathematics::Vector3;

namespace MphRead::Mods::Network
{
    struct NetSmoothingTestAccess final
    {
        static void SetReadFrame(double frame) noexcept
        {
            NetSmoothing::_readFrame = frame;
        }
    };
}

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    PlayerState StateAt(std::uint32_t frame)
    {
        PlayerState state{};
        state.SlotIndex = 1;
        state.SlotGeneration = 1;
        state.LifeId = 1;
        state.Flags = PlayerState::FlagActive | PlayerState::FlagSpawned;
        state.Position = Vector3(static_cast<float>(frame), 0, 0);
        state.Health = 100;
        if (frame <= 29)
        {
            state.WeavelFlags = PlayerState::WeavelFlagMorphing;
            state.SetFormTransitionProgress(frame <= 28 ? 0 : 15);
        }
        else
        {
            state.Flags |= PlayerState::FlagAltForm;
        }
        return state;
    }

    PlayerState UnmorphStateAt(std::uint32_t frame)
    {
        PlayerState state = StateAt(frame);
        state.SlotIndex = 2;
        if (frame <= 29)
        {
            state.Flags |= PlayerState::FlagAltForm;
            state.WeavelFlags = PlayerState::WeavelFlagUnmorphing;
            state.SetFormTransitionProgress(frame <= 28 ? 0 : 15);
        }
        else
        {
            state.Flags = static_cast<std::uint8_t>(state.Flags & ~PlayerState::FlagAltForm);
            state.WeavelFlags = 0;
        }
        return state;
    }
}

int main()
{
    const NetSessionFixture::State previousSession = NetSessionFixture::Capture();
    const bool previousEnabled = NetSmoothing::Enabled();
    NetSessionFixture::SetRole(NetRole::Client);
    NetSessionFixture::SetLocalSlot(0);
    NetSessionFixture::RevokeAuthority();
    NetPlayerLifecycle::Reset();
    NetPlayerLifecycle::SetOccupant(1, 1);
    NetPlayerLifecycle::SetOccupant(2, 1);
    NetSmoothing::Reset();
    NetSmoothing::Enabled(true);

    bool passed = true;
    passed &= Check(NetSmoothing::PresentationOwnsRemoteReplicas(),
        "an interpolated client owns remote form presentation before its sample ring starts");
    NetSmoothing::Enabled(false);
    passed &= Check(!NetSmoothing::PresentationOwnsRemoteReplicas(),
        "disabling interpolation restores the immediate remote form-input path");
    NetSmoothing::Enabled(true);
    NetSessionFixture::SetRole(NetRole::Host);
    passed &= Check(!NetSmoothing::PresentationOwnsRemoteReplicas(),
        "the authoritative host retains remote form-input simulation");
    NetSessionFixture::SetRole(NetRole::Client);

    for (std::uint32_t frame = 1; frame <= 30; ++frame)
    {
        const std::array<PlayerState, 2> states{ StateAt(frame), UnmorphStateAt(frame) };
        for (const PlayerState& state : states)
        {
            passed &= Check(NetPlayerLifecycle::AcceptState(state, frame),
                "the test state belongs to the active slot generation and life");
        }
        NetSmoothing::Record(frame, std::span<const PlayerState>(states));

        NetSmoothing::TrackedPosition bomb{};
        bomb.Sequence = 7;
        bomb.Position = Vector3(static_cast<float>(frame * 10), 0, 0);
        bomb.Present = frame < 29;
        NetSmoothing::RecordTrackedPositions(frame, states[0].SlotIndex,
            std::span<const NetSmoothing::TrackedPosition>(&bomb, 1));
    }

    // Snap to newest-delay (frame 28), then let all visual tracks read the same fractional frame.
    NetSmoothing::Tick();
    NetSmoothing::Tick();
    Vector3 playerPosition{};
    bool altForm = true;
    ReplicatedFormState form{};
    Vector3 bombPosition{};
    bool bombPresent = false;
    passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
        && std::abs(playerPosition.X - 28.95F) < 0.02F && !altForm,
        "player presentation samples the shared fractional playout frame");
    passed &= Check(NetSmoothing::SampleForm(1, form)
        && form.Direction == ReplicatedFormState::Transition::Morph && form.Progress == 14
        && form.HeadingAlt(),
        "a remote form transition samples direction and progress at the shared presentation frame");
    ReplicatedFormState unmorph{};
    passed &= Check(NetSmoothing::SampleForm(2, unmorph)
        && unmorph.Direction == ReplicatedFormState::Transition::Unmorph
        && unmorph.Progress == 14 && !unmorph.HeadingAlt() && unmorph.AltForm,
        "an unmorph clip samples the same presentation clock while its authority alt-form bit remains set");
    passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && bombPresent && std::abs(bombPosition.X - 280.0F) < 0.02F,
        "a moving bomb holds its last visible pose until playout reaches its gone frame");

    NetSmoothing::Tick();
    passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && !bombPresent && std::abs(bombPosition.X - 290.0F) < 0.02F,
        "the bomb disappears when the shared playout reaches the gone frame");
    passed &= Check(NetSmoothing::SampleForm(1, form)
        && form.Direction == ReplicatedFormState::Transition::Morph && form.Progress == 15,
        "the remote animation reaches its final sampled phase before the stable-form snapshot");
    passed &= Check(NetSmoothing::SampleForm(2, unmorph)
        && unmorph.Direction == ReplicatedFormState::Transition::Unmorph && unmorph.Progress == 15,
        "the unmorph animation reaches its final sampled phase before the stable-form snapshot");

    NetSmoothing::Tick();
    passed &= Check(NetSmoothing::SampleForm(1, form)
        && form.Direction == ReplicatedFormState::Transition::None && form.AltForm,
        "morph commits to the stable alt form only when the shared read clock reaches it");
    passed &= Check(NetSmoothing::SampleForm(2, unmorph)
        && unmorph.Direction == ReplicatedFormState::Transition::None && !unmorph.AltForm,
        "unmorph commits to the stable biped form only when the shared read clock reaches it");

    NetSmoothing::Reset();
    PlayerState sparseBefore = StateAt(100);
    sparseBefore.Position = Vector3(0, 0, 0);
    sparseBefore.Flags = static_cast<std::uint8_t>(sparseBefore.Flags & ~PlayerState::FlagAltForm);
    sparseBefore.WeavelFlags = PlayerState::WeavelFlagMorphing;
    sparseBefore.SetFormTransitionProgress(0);
    PlayerState sparseAfter = sparseBefore;
    sparseAfter.Position = Vector3(3, 0, 0);
    sparseAfter.SetFormTransitionProgress(12);
    const std::array<PlayerState, 1> beforeStates{ sparseBefore };
    const std::array<PlayerState, 1> afterStates{ sparseAfter };
    passed &= Check(NetPlayerLifecycle::AcceptState(sparseBefore, 100)
        && NetPlayerLifecycle::AcceptState(sparseAfter, 103),
        "sparse timeline snapshots retain the active slot generation and life");
    NetSmoothing::Record(100, std::span<const PlayerState>(beforeStates));
    NetSmoothing::Record(103, std::span<const PlayerState>(afterStates));
    const std::array<NetSmoothing::TrackedPosition, 1> bombBefore{{
        { 7, Vector3(0, 0, 0), true }
    }};
    const std::array<NetSmoothing::TrackedPosition, 1> bombAfter{{
        { 7, Vector3(3, 0, 0), true }
    }};
    NetSmoothing::RecordTrackedPositions(100, 1, std::span<const NetSmoothing::TrackedPosition>(bombBefore));
    NetSmoothing::RecordTrackedPositions(103, 1, std::span<const NetSmoothing::TrackedPosition>(bombAfter));
    const std::array<float, 4> sparsePositions{ 0, 1, 2, 3 };
    const std::array<std::uint8_t, 4> sparseProgress{ 0, 4, 8, 12 };
    for (std::size_t i = 0; i < sparsePositions.size(); ++i)
    {
        NetSmoothingTestAccess::SetReadFrame(100.0 + static_cast<double>(i));
        passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
            && std::abs(playerPosition.X - sparsePositions[i]) < 0.01F && !altForm,
            "player position interpolates at every integer inside a sparse snapshot gap");
        passed &= Check(NetSmoothing::SampleForm(1, form)
            && form.Direction == ReplicatedFormState::Transition::Morph
            && form.Progress == sparseProgress[i],
            "form progress shares the sparse player-position presentation clock");
        passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
            && bombPresent && std::abs(bombPosition.X - sparsePositions[i]) < 0.01F,
            "tracked objects share the sparse player-position presentation clock");
    }
    NetSmoothingTestAccess::SetReadFrame(101.5);
    passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
        && std::abs(playerPosition.X - 1.5F) < 0.01F,
        "fractional sparse-frame position interpolation remains continuous");
    passed &= Check(NetSmoothing::SampleForm(1, form) && form.Progress == 6,
        "fractional form progress remains on the shared presentation clock");
    passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && bombPresent && std::abs(bombPosition.X - 1.5F) < 0.01F,
        "fractional tracked-object interpolation remains on the shared presentation clock");

    NetSmoothing::Reset();
    PlayerState formBoundaryBefore = sparseBefore;
    formBoundaryBefore.Flags = static_cast<std::uint8_t>(formBoundaryBefore.Flags & ~PlayerState::FlagAltForm);
    formBoundaryBefore.WeavelFlags = 0;
    formBoundaryBefore.SetFormTransitionProgress(0);
    formBoundaryBefore.Position = Vector3(0, 0, 0);
    PlayerState formBoundaryAfter = formBoundaryBefore;
    formBoundaryAfter.Flags |= PlayerState::FlagAltForm;
    formBoundaryAfter.Position = Vector3(3, 0, 0);
    const std::array<PlayerState, 1> formBoundaryBeforeStates{ formBoundaryBefore };
    const std::array<PlayerState, 1> formBoundaryAfterStates{ formBoundaryAfter };
    passed &= Check(NetPlayerLifecycle::AcceptState(formBoundaryBefore, 200)
        && NetPlayerLifecycle::AcceptState(formBoundaryAfter, 203),
        "form-boundary samples belong to the active slot generation and life");
    NetSmoothing::Record(200, std::span<const PlayerState>(formBoundaryBeforeStates));
    NetSmoothing::Record(203, std::span<const PlayerState>(formBoundaryAfterStates));
    const std::array<NetSmoothing::TrackedPosition, 1> bombGone{{
        { 7, Vector3(3, 0, 0), false }
    }};
    NetSmoothing::RecordTrackedPositions(200, 1, std::span<const NetSmoothing::TrackedPosition>(bombBefore));
    NetSmoothing::RecordTrackedPositions(203, 1, std::span<const NetSmoothing::TrackedPosition>(bombGone));
    NetSmoothingTestAccess::SetReadFrame(201.5);
    passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
        && playerPosition.X == 0 && !altForm,
        "position holds at the near side while the replicated form changes");
    passed &= Check(NetSmoothing::SampleForm(1, form)
        && !form.AltForm && form.Direction == ReplicatedFormState::Transition::None,
        "form state does not blend across a stable-form boundary");
    passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && bombPresent && bombPosition.X == 0,
        "a removed tracked object stays at its last present pose until its removal frame");

    NetSmoothing::Reset();
    PlayerState teleportBefore = formBoundaryBefore;
    teleportBefore.Position = Vector3(0, 0, 0);
    PlayerState teleportAfter = teleportBefore;
    teleportAfter.Position = Vector3(10, 0, 0);
    const std::array<PlayerState, 1> teleportBeforeStates{ teleportBefore };
    const std::array<PlayerState, 1> teleportAfterStates{ teleportAfter };
    passed &= Check(NetPlayerLifecycle::AcceptState(teleportBefore, 300)
        && NetPlayerLifecycle::AcceptState(teleportAfter, 303),
        "teleport-boundary samples belong to the active slot generation and life");
    NetSmoothing::Record(300, std::span<const PlayerState>(teleportBeforeStates));
    NetSmoothing::Record(303, std::span<const PlayerState>(teleportAfterStates));
    const std::array<NetSmoothing::TrackedPosition, 1> bombTeleported{{
        { 7, Vector3(10, 0, 0), true }
    }};
    NetSmoothing::RecordTrackedPositions(300, 1, std::span<const NetSmoothing::TrackedPosition>(bombBefore));
    NetSmoothing::RecordTrackedPositions(303, 1, std::span<const NetSmoothing::TrackedPosition>(bombTeleported));
    NetSmoothingTestAccess::SetReadFrame(301.5);
    passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
        && playerPosition.X == 0,
        "teleport-sized player movement keeps the near pose instead of interpolating");
    passed &= Check(NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && bombPresent && bombPosition.X == 0,
        "teleport-sized tracked movement keeps the near pose instead of interpolating");

    NetSmoothing::Reset();
    PlayerState generationBefore = sparseBefore;
    generationBefore.Position = Vector3(0, 0, 0);
    const std::array<PlayerState, 1> generationBeforeStates{ generationBefore };
    passed &= Check(NetPlayerLifecycle::AcceptState(generationBefore, 400),
        "pre-rejoin sample belongs to the original occupant");
    NetSmoothing::Record(400, std::span<const PlayerState>(generationBeforeStates));
    NetSmoothing::RecordTrackedPositions(400, 1, std::span<const NetSmoothing::TrackedPosition>(bombBefore));
    NetPlayerLifecycle::SetOccupant(1, 2);
    NetSmoothingTestAccess::SetReadFrame(400.5);
    passed &= Check(!NetSmoothing::Sample(1, playerPosition, altForm)
        && !NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent),
        "slot generation changes clear old sparse player and tracked samples");

    NetSmoothing::Reset();
    PlayerState lifeBefore = sparseBefore;
    lifeBefore.SlotGeneration = 2;
    lifeBefore.LifeId = 1;
    lifeBefore.Position = Vector3(0, 0, 0);
    const std::array<PlayerState, 1> lifeBeforeStates{ lifeBefore };
    passed &= Check(NetPlayerLifecycle::AcceptState(lifeBefore, 500),
        "pre-respawn sample belongs to the active slot life");
    NetSmoothing::Record(500, std::span<const PlayerState>(lifeBeforeStates));
    NetSmoothing::RecordTrackedPositions(500, 1, std::span<const NetSmoothing::TrackedPosition>(bombBefore));
    PlayerState lifeAfter = lifeBefore;
    lifeAfter.LifeId = 2;
    lifeAfter.Position = Vector3(3, 0, 0);
    const std::array<PlayerState, 1> lifeAfterStates{ lifeAfter };
    passed &= Check(NetPlayerLifecycle::AcceptState(lifeAfter, 503),
        "respawn advances life without changing the slot generation");
    NetSmoothing::Record(503, std::span<const PlayerState>(lifeAfterStates));
    NetSmoothing::RecordTrackedPositions(503, 1, std::span<const NetSmoothing::TrackedPosition>(bombAfter));
    NetSmoothingTestAccess::SetReadFrame(501.5);
    passed &= Check(!NetSmoothing::Sample(1, playerPosition, altForm)
        && !NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent),
        "life changes do not interpolate stale player or tracked samples across a respawn");
    NetSmoothingTestAccess::SetReadFrame(503.0);
    passed &= Check(NetSmoothing::Sample(1, playerPosition, altForm)
        && playerPosition.X == 3.0F
        && NetSmoothing::SampleTrackedPosition(1, 7, bombPosition, bombPresent)
        && bombPresent && bombPosition.X == 3.0F,
        "player and tracked samples resume at the new life's first frame");

    NetPlayerLifecycle::SetOccupant(1, 0);
    NetPlayerLifecycle::Reset();
    NetSmoothing::Reset();
    NetSmoothing::Enabled(previousEnabled);
    NetSessionFixture::Restore(previousSession);
    if (!passed)
    {
        return 1;
    }
    std::cout << "Network smoothing timeline checks passed.\n";
    return 0;
}
