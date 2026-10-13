#include "Mods/Network/Gameplay/Player/PlayerEntityNetCulling.hpp"

#include "Entities/EntityBase.hpp"
#include "Entities/RoomEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaPlayerPolicy.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaRoomPlacement.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaShadowVisibility.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "Scene.hpp"

#include <cstdint>
#include <memory>
#include <string>

using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Multiply;

namespace MphRead::Entities
{
    using Vector3 = OpenTK::Mathematics::Vector3;

    namespace
    {
        Mods::Network::ReplicaPlayerPolicy::Context ReplicaRoleContext(const PlayerEntity& player)
        {
            return {
                Mods::Network::NetSession::IsClient(),
                Mods::Network::NetSession::IsHost(),
                Mods::Network::NetSession::IsAuthority(),
                player.SlotIndex(),
                Mods::Network::NetSession::LocalSlot(),
                player.IsBot()
            };
        }

        bool IsLocalNetworkPlayer(const PlayerEntity& player)
        {
            return Mods::Network::NetSession::Active()
                && Mods::Network::ReplicaPlayerPolicy::IsLocalSlot(
                    player.SlotIndex(), Mods::Network::NetSession::LocalSlot());
        }
    }

    Formats::Culling::NodeRef PlayerEntity::ModWalkNodeRef(
        MphRead::Scene& scene,
        Formats::Culling::NodeRef current,
        Vector3 previous,
        Vector3 position)
    {
        if (current == Formats::Culling::NodeRef::None
            || LengthSquared(position - previous) >= _nodeWalkStepMax * _nodeWalkStepMax)
        {
            return Formats::Culling::NodeRef::None;
        }

        Formats::Culling::NodeRef walked
            = scene.UpdateNodeRef(current, previous, position);
        if (walked.PartIndex == -1
            || !scene.PartCouldContain(walked.PartIndex, position))
        {
            return Formats::Culling::NodeRef::None;
        }
        return walked;
    }

    void PlayerEntity::ModPlaceAt(Vector3 position)
    {
        const Vector3 previous = static_cast<Vector3>((*this).Position);
        ((*this).Position = position);
        (*this).SetPrevPosition(position);
        ModRefreshNodeRef(previous);
    }

    void PlayerEntity::ModRefreshNodeRef(Vector3 previousPosition)
    {
        (*this)._volume = CollisionVolume::Move((*this)._volumeUnxf, (*this).Position);
        MphRead::Scene& scene = (*(*this)._scene);
        const bool isLocalNetworkPlayer = IsLocalNetworkPlayer(*this);
        const Formats::Culling::NodeRef currentForWalk = (*this).NodeRef;
        const Vector3 positionForWalk = static_cast<Vector3>((*this).Position);
        Formats::Culling::NodeRef walked = ModWalkNodeRef(
            scene, currentForWalk, previousPosition, positionForWalk);
        if (walked != Formats::Culling::NodeRef::None)
        {
            if (!scene.IsNodeRefVisible(walked) && !isLocalNetworkPlayer)
            {
                // Portal walking can retain a valid but stale part while a
                // smoothed replica crosses a culling boundary. The shadow is
                // drawn before the body, so that stale hidden part produces a
                // shadow-only player. Re-resolve this hidden replica by position.
                // The local owner keeps portal tracking: position-only lookup
                // is ambiguous where room layers overlap vertically.
                const Formats::Culling::NodeRef positionNode
                    = scene.GetNodeRefByPosition(positionForWalk);
                if (positionNode != Formats::Culling::NodeRef::None)
                {
                    if (positionNode.PartIndex != walked.PartIndex
                        && Mods::Network::NetLog::Enabled())
                    {
                        Mods::Network::NetLog::Event("[player-node] repaired hidden replica slot="
                            + std::to_string(SlotIndex())
                            + " stale=" + std::to_string(walked.PartIndex)
                            + "/" + std::to_string(walked.NodeIndex)
                            + " position=" + std::to_string(positionNode.PartIndex)
                            + "/" + std::to_string(positionNode.NodeIndex)
                            + " at=" + std::to_string(positionForWalk.X)
                            + "," + std::to_string(positionForWalk.Y)
                            + "," + std::to_string(positionForWalk.Z));
                    }
                    walked = positionNode;
                    _modNodeUnresolved = false;
                }
                else
                {
                    // Drawing the body is safer than rejecting it on a stale
                    // culling node when the current position has no room node.
                    _modNodeUnresolved = true;
                    Mods::Network::NetPlayerBridge::RecordUnresolvedNodeLookup();
                }
            }
            else
            {
                _modNodeUnresolved = false;
            }
            ((*this).NodeRef = walked);
            return;
        }

        if (isLocalNetworkPlayer)
        {
            // Never replace a local owner's missing portal walk with an
            // ambiguous point lookup into vertically overlapping room parts.
            _modNodeUnresolved = true;
            Mods::Network::NetPlayerBridge::RecordUnresolvedNodeLookup();
            return;
        }

        const Vector3 position = static_cast<Vector3>((*this).Position);
        Formats::Culling::NodeRef found = scene.GetNodeRefByPosition(position);
        if (found.PartIndex == -1)
        {
            found = scene.GetNodeRefByPosition((*this)._volume.SpherePosition);
        }
        if (found.PartIndex == -1)
        {
            found = scene.GetNodeRefByPosition(position
                + Multiply(Vector3::UnitY, 0.5F));
        }
        if (found.PartIndex == -1)
        {
            found = scene.GetNodeRefByPosition(position
                - Multiply(Vector3::UnitY, 0.5F));
        }
        if (found.PartIndex != -1)
        {
            ((*this).NodeRef = found);
            _modNodeUnresolved = false;
            return;
        }

        _modNodeUnresolved = true;
        Mods::Network::NetPlayerBridge::RecordUnresolvedNodeLookup();
        const Formats::Culling::NodeRef currentForCheck = (*this).NodeRef;
        if (currentForCheck != Formats::Culling::NodeRef::None)
        {
            ((*this).NodeRef = scene.UpdateNodeRef(currentForCheck,
                previousPosition, position));
        }
    }

    void PlayerEntity::ModResolveNetworkNodeRef(Vector3 previousPosition)
    {
        if (IsLocalNetworkPlayer(*this))
        {
            // A local position correction must keep the camera's established
            // portal path; direct point lookup can select an overlapping floor.
            ModRefreshNodeRef(previousPosition);
            return;
        }

        // A networked pose is sampled between authority snapshots and can
        // cross more than one portal in a single update. Rebuild its culling
        // location from the same displayed world position instead of walking
        // the previous NodeRef, whose portal may no longer be active here.
        (*this)._volume = CollisionVolume::Move((*this)._volumeUnxf, (*this).Position);
        MphRead::Scene& scene = (*(*this)._scene);
        const Vector3 position = static_cast<Vector3>((*this).Position);
        const Formats::Culling::NodeRef previous = (*this).NodeRef;
        const bool wasUnresolved = _modNodeUnresolved;
        const Vector3 bodyCenter = _volume.SpherePosition;
        // Direct point placement is reserved for remote replicas; local owners
        // returned above through their established Scene portal path.
        const bool useReplicaPlacement = Mods::Network::ReplicaPlayerPolicy::UsesReplicaRoomPlacement(
            ReplicaRoleContext(*this));
        const auto room = useReplicaPlacement ? scene.Room() : nullptr;

        bool ambiguous = false;
        const auto lookup = [&](Vector3 probe)
        {
            if (!room) return scene.GetNodeRefByPosition(probe);
            const auto resolution = Mods::Network::ReplicaRoomPlacement::Find(
                room.get(), probe, bodyCenter);
            ambiguous = Mods::Network::ReplicaRoomPlacementRules::StopsProbeFallback(resolution.state);
            return resolution.state == Mods::Network::ReplicaRoomPlacementRules::State::Resolved
                ? resolution.node : Formats::Culling::NodeRef::None;
        };
        // Every fallback probe must place the same current-form collision
        // center. No retained simulation/presentation node is trusted here.
        Formats::Culling::NodeRef found = lookup(bodyCenter);
        if (found.PartIndex == -1 && !ambiguous)
        {
            found = lookup(position);
        }
        if (found.PartIndex == -1 && !ambiguous)
        {
            found = lookup(position + Multiply(Vector3::UnitY, 0.5F));
        }
        if (found.PartIndex == -1 && !ambiguous)
        {
            found = lookup(position - Multiply(Vector3::UnitY, 0.5F));
        }

        if (found.PartIndex != -1)
        {
            ((*this).NodeRef = found);
            _modNodeUnresolved = false;
            if ((wasUnresolved || previous.PartIndex != found.PartIndex)
                && Mods::Network::NetLog::Enabled())
            {
                Mods::Network::NetLog::Event("[player-node] resolved presentation position slot="
                    + std::to_string(SlotIndex())
                    + " from=" + std::to_string(previous.PartIndex)
                    + "/" + std::to_string(previous.NodeIndex)
                    + " to=" + std::to_string(found.PartIndex)
                    + "/" + std::to_string(found.NodeIndex)
                    + " visible=" + (scene.IsNodeRefVisible(found) ? std::string("true") : std::string("false"))
                    + " at=" + std::to_string(position.X)
                    + "," + std::to_string(position.Y)
                    + "," + std::to_string(position.Z));
            }

            return;
        }

        _modNodeUnresolved = true;
        Mods::Network::NetPlayerBridge::RecordUnresolvedNodeLookup();
        if (!wasUnresolved && Mods::Network::NetLog::Enabled())
        {
            Mods::Network::NetLog::Event("[player-node] unresolved presentation position slot="
                + std::to_string(SlotIndex())
                + " retained=" + std::to_string(previous.PartIndex)
                + "/" + std::to_string(previous.NodeIndex)
                + " at=" + std::to_string(position.X)
                + "," + std::to_string(position.Y)
                + "," + std::to_string(position.Z));
        }
    }

    void PlayerEntity::ModSetReplicaShadowVisible(bool visible)
    {
        _modShadowDrawState = {};
        if (!visible || !ModIsRemoteNetworkPlayer())
        {
            return;
        }

        _modShadowDrawState = Mods::Network::ReplicaShadowVisibility::CaptureVisibleShadow(
            (*this)._scene);
    }

    bool PlayerEntity::ModNodeUnresolved() const noexcept
    {
        return _modNodeUnresolved;
    }

    bool PlayerEntity::ModMayBypassReplicaPartCulling() const noexcept
    {
        if (!ModIsRemoteNetworkPlayer())
        {
            return false;
        }

        const MphRead::Scene* scene = (*this)._scene;
        if (!Mods::Network::ReplicaShadowVisibility::MatchesScene(
            _modShadowDrawState, scene))
        {
            return false;
        }
        return Mods::Network::ReplicaShadowVisibility::ShouldForceBody(
            true, _modShadowDrawState);
    }

    bool PlayerEntity::ModIsRemoteNetworkPlayer() const noexcept
    {
        return Mods::Network::ReplicaPlayerPolicy::UsesReplicaShadowVisibility(
            ReplicaRoleContext(*this));
    }

    void PlayerEntity::ModNetSpawn(Vector3 position, Vector3 facing)
    {
        const Vector3 forward = LengthSquared(facing) > 0.0001F
            ? facing.Normalized()
            : Vector3(0.0F, 0.0F, -1.0F);
        const Vector3 up = Vector3::UnitY;
        MphRead::Scene& scene = (*(*this)._scene);
        const Formats::Culling::NodeRef nodeRef = ModSpawnNodeRef(scene, position);
        (*this).Spawn(position, forward, up, nodeRef, true);
    }

    Formats::Culling::NodeRef PlayerEntity::ModSpawnNodeRef(
        MphRead::Scene& scene, Vector3 position)
    {
        std::shared_ptr<EntityBase> closest{};
        float closestDist = 2.0F * 2.0F;
        auto enumerator = scene.Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<EntityBase> entity = enumerator.Current();
            if (entity->Type != MphRead::EntityType::PlayerSpawn
                || entity->NodeRef == Formats::Culling::NodeRef::None)
            {
                continue;
            }
            const float dist = LengthSquared(static_cast<Vector3>(entity->Position) - position);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = entity;
            }
        }
        if (closest != nullptr)
        {
            return closest->NodeRef;
        }
        return scene.GetNodeRefByPosition(position);
    }
}
