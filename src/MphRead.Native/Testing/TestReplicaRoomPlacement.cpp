#include "Mods/Network/Gameplay/Player/ReplicaRoomPlacementRules.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaShadowVisibility.hpp"

#include <iostream>
#include <limits>

namespace
{
    using Rules = MphRead::Mods::Network::ReplicaRoomPlacementRules;
    using NodeRef = MphRead::Formats::Culling::NodeRef;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    bool Check(bool condition, const char* message)
    {
        if (!condition) std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool Contains(Vector3 position, Vector3 minimum, Vector3 maximum, Vector3 offset = Vector3::Zero)
    {
        return Rules::CheckBounds(position, minimum, maximum, offset) == Rules::BoundsResult::Inside;
    }
}

int main()
{
    bool passed = true;

    // Two stacked parts share X/Z, but their body centers resolve to separate Y bounds.
    const NodeRef lowerPart(nullptr, 1, 10, 0);
    const NodeRef upperPart(nullptr, 2, 20, 0);
    const Vector3 lowerMinimum(-2.0F, 0.0F, -2.0F);
    const Vector3 lowerMaximum(2.0F, 2.0F, 2.0F);
    const Vector3 upperMinimum(-2.0F, 3.0F, -2.0F);
    const Vector3 upperMaximum(2.0F, 5.0F, 2.0F);
    const Vector3 lowerBody(0.0F, 1.0F, 0.0F);
    const Vector3 upperBody(0.0F, 4.0F, 0.0F);
    const Vector4 stackedPortalPlane(0.0F, 1.0F, 0.0F, 2.0F);

    Rules::Candidates lowerCandidates;
    if (Rules::IsInsidePortalSide(lowerBody, stackedPortalPlane, true)
        && Contains(lowerBody, lowerMinimum, lowerMaximum)) lowerCandidates.Consider(lowerPart);
    if (Rules::IsInsidePortalSide(lowerBody, stackedPortalPlane, false)
        && Contains(lowerBody, upperMinimum, upperMaximum)) lowerCandidates.Consider(upperPart);
    const auto lowerResolution = lowerCandidates.Get();
    passed &= Check(lowerResolution.state == Rules::State::Resolved
        && lowerResolution.node.PartIndex == lowerPart.PartIndex,
        "lower body center uniquely resolves the lower stacked part");

    Rules::Candidates upperCandidates;
    if (Rules::IsInsidePortalSide(upperBody, stackedPortalPlane, true)
        && Contains(upperBody, lowerMinimum, lowerMaximum)) upperCandidates.Consider(lowerPart);
    if (Rules::IsInsidePortalSide(upperBody, stackedPortalPlane, false)
        && Contains(upperBody, upperMinimum, upperMaximum)) upperCandidates.Consider(upperPart);
    const auto upperResolution = upperCandidates.Get();
    passed &= Check(upperResolution.state == Rules::State::Resolved
        && upperResolution.node.PartIndex == upperPart.PartIndex,
        "upper body center uniquely resolves the upper stacked part");

    // The Portal probe and current-form body center are independent samples.
    Rules::Candidates notInsidePortal;
    if (Rules::IsInsidePortalSide(upperBody, stackedPortalPlane, true)
        && Contains(upperBody, upperMinimum, upperMaximum)) notInsidePortal.Consider(upperPart);
    passed &= Check(notInsidePortal.Get().state == Rules::State::Unknown,
        "geometry overlap does not accept a part outside the probe's Portal half-space");

    Rules::Candidates bodyOutsideGeometry;
    if (Rules::IsInsidePortalSide(upperBody, stackedPortalPlane, false)
        && Contains(lowerBody, upperMinimum, upperMaximum)) bodyOutsideGeometry.Consider(upperPart);
    passed &= Check(bodyOutsideGeometry.Get().state == Rules::State::Unknown,
        "a Portal match does not accept a part when the current body center misses its bounds");

    // Sampling the current form's center chooses the corresponding stacked part without retained state.
    const Vector3 altFormBody(0.0F, 4.0F, 0.0F);
    Rules::Candidates currentForm;
    if (Rules::IsInsidePortalSide(altFormBody, stackedPortalPlane, true)
        && Contains(altFormBody, lowerMinimum, lowerMaximum)) currentForm.Consider(lowerPart);
    if (Rules::IsInsidePortalSide(altFormBody, stackedPortalPlane, false)
        && Contains(altFormBody, upperMinimum, upperMaximum)) currentForm.Consider(upperPart);
    passed &= Check(currentForm.Get().state == Rules::State::Resolved
        && currentForm.Get().node.PartIndex == upperPart.PartIndex,
        "a changed form body center is resolved from its current pose rather than stale part state");

    // The same Portal boundary belongs to either oriented side, as in the native half-space rule.
    const Vector4 horizontalPlane(0.0F, 1.0F, 0.0F, 2.0F);
    passed &= Check(Rules::IsInsidePortalSide(Vector3(0.0F, 2.0F, 0.0F), horizontalPlane, false)
        && Rules::IsInsidePortalSide(Vector3(0.0F, 2.0F, 0.0F), horizontalPlane, true),
        "a body on the portal plane is accepted from either side");
    passed &= Check(Rules::IsInsidePortalSide(upperBody, horizontalPlane, false)
        && !Rules::IsInsidePortalSide(upperBody, horizontalPlane, true),
        "portal side orientation rejects the opposite half-space");

    // Connector models store local bounds and use the collision instance's translation.
    passed &= Check(Contains(Vector3(0.0F, 11.0F, 0.0F), lowerMinimum, lowerMaximum,
        Vector3(0.0F, 10.0F, 0.0F)),
        "connector translation is applied before checking physical bounds");

    // Overlapping AABBs cannot identify one part. The caller must retain the distinct ambiguous state.
    Rules::Candidates overlapping;
    const Vector3 overlapBody(0.0F, 1.0F, 0.0F);
    if (Rules::IsInsidePortalSide(overlapBody, horizontalPlane, true)
        && Contains(overlapBody, lowerMinimum, lowerMaximum)) overlapping.Consider(lowerPart);
    if (Rules::IsInsidePortalSide(overlapBody, horizontalPlane, true)
        && Contains(overlapBody, Vector3(-1.0F, -1.0F, -1.0F),
            Vector3(1.0F, 2.0F, 1.0F)))
        overlapping.Consider(upperPart);
    const auto ambiguous = overlapping.Get();
    passed &= Check(ambiguous.state == Rules::State::Ambiguous
        && ambiguous.node.PartIndex == NodeRef::None.PartIndex && overlapping.IsAmbiguous(),
        "overlapping physical candidates are ambiguous instead of first-match placement");
    passed &= Check(Rules::StopsProbeFallback(ambiguous.state)
        && !Rules::StopsProbeFallback(Rules::State::Unknown),
        "ambiguity stops additional probes while unknown retains the existing fallback path");

    // No fitting AABB, missing candidates, malformed geometry and non-finite coordinates stay unknown.
    Rules::Candidates unknown;
    passed &= Check(unknown.Get().state == Rules::State::Unknown
        && unknown.Get().node.PartIndex == NodeRef::None.PartIndex,
        "an empty candidate set reports unknown without inventing a NodeRef");
    unknown.Consider(NodeRef::None);
    passed &= Check(unknown.Get().state == Rules::State::Unknown,
        "a None candidate does not become a resolved room part");
    passed &= Check(!Contains(Vector3(3.0F, 1.0F, 0.0F), lowerMinimum, lowerMaximum),
        "a legal portal probe outside every physical AABB produces no accepted candidate");
    passed &= Check(Rules::CheckBounds(Vector3::Zero, Vector3(1.0F, 0.0F, 0.0F),
            Vector3(-1.0F, 1.0F, 1.0F), Vector3::Zero) == Rules::BoundsResult::Invalid,
        "inverted bounds are rejected as invalid data");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    passed &= Check(!Rules::IsFinite(Vector3(nan, 0.0F, 0.0F)),
        "non-finite body centers remain unclassified");
    passed &= Check(Rules::CheckBounds(Vector3::Zero, Vector3(nan, 0.0F, 0.0F),
            lowerMaximum, Vector3::Zero) == Rules::BoundsResult::Invalid,
        "non-finite geometry bounds remain unclassified");

    passed &= Check(Rules::IsValidNodeRef(lowerPart, 8, 0, 1),
        "a base model NodeRef with in-range indexes is accepted");
    passed &= Check(Rules::IsValidNodeRef(NodeRef(nullptr, 1, 10, 2), 8, 2, 3),
        "a one-based connector model index is checked against both connector arrays");
    passed &= Check(!Rules::IsValidNodeRef(NodeRef(nullptr, -1, 0, 0), 8, 0, 1)
        && !Rules::IsValidNodeRef(NodeRef(nullptr, 8, 0, 0), 8, 0, 1)
        && !Rules::IsValidNodeRef(NodeRef(nullptr, 1, -1, 0), 8, 0, 1)
        && !Rules::IsValidNodeRef(NodeRef(nullptr, 1, 0, -1), 8, 0, 1),
        "negative and out-of-range part, node, or model indexes are rejected");
    passed &= Check(!Rules::IsValidNodeRef(NodeRef(nullptr, 1, 0, 3), 8, 2, 3)
        && !Rules::IsValidNodeRef(NodeRef(nullptr, 1, 0, 2), 8, 2, 2),
        "connector indexes outside either backing array are rejected");

    using ShadowVisibility = MphRead::Mods::Network::ReplicaShadowVisibility;
    const ShadowVisibility::Projection groundedShadow{ nullptr, nullptr, -1, true, 2.0F };
    const ShadowVisibility::FadeState visibleFade{ 1.0F, 100, 0.0F, 0, 180 };
    passed &= Check(ShadowVisibility::ShouldForceBody(true, groundedShadow, visibleFade),
        "a remote replica with a drawable shadow projection bypasses Part culling");
    passed &= Check(!ShadowVisibility::ShouldForceBody(false, groundedShadow, visibleFade),
        "local players and offline bots do not use remote shadow visibility policy");
    passed &= Check(!ShadowVisibility::ShouldForceBody(true,
            ShadowVisibility::Projection{ nullptr, nullptr, -1, false, 0.0F }, visibleFade),
        "a frame without a shadow surface keeps normal body culling");
    passed &= Check(!ShadowVisibility::ShouldForceBody(true,
            ShadowVisibility::Projection{ nullptr, nullptr, -1, true, 10.0F }, visibleFade),
        "a shadow at or beyond its ten-unit fade distance does not force the body");
    passed &= Check(!ShadowVisibility::ShouldForceBody(true, groundedShadow,
            ShadowVisibility::FadeState{ 0.0F, 100, 0.0F, 0, 180 }),
        "a fully transparent shadow does not force the body");
    passed &= Check(!ShadowVisibility::ShouldForceBody(true, groundedShadow,
            ShadowVisibility::FadeState{ 1.0F, 0, 0.0F, 0, 180 }),
        "a dead player's fully faded shadow does not force the body");

    return passed ? 0 : 1;
}
