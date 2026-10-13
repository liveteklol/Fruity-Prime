#pragma once

#include "Formats/Culling.hpp"
#include "Mods/Network/Gameplay/Player/ReplicaShadowVisibility.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>

namespace MphRead
{
    class Scene;
}

#define MPHREAD_PLAYER_ENTITY_NET_CULLING_MEMBERS \
public: \
    [[nodiscard]] static Formats::Culling::NodeRef ModWalkNodeRef( \
        MphRead::Scene& scene, \
        Formats::Culling::NodeRef current, \
        OpenTK::Mathematics::Vector3 previous, \
        OpenTK::Mathematics::Vector3 position); \
    void ModPlaceAt(OpenTK::Mathematics::Vector3 position); \
    void ModRefreshNodeRef(OpenTK::Mathematics::Vector3 previousPosition); \
    void ModResolveNetworkNodeRef(OpenTK::Mathematics::Vector3 previousPosition); \
    [[nodiscard]] bool ModNodeUnresolved() const noexcept; \
    /* Mods policy hook: only remote replicas with a drawn shadow can bypass Part culling. */ \
    [[nodiscard]] bool ModMayBypassReplicaPartCulling() const noexcept; \
    /* Called by DrawShadow to publish only the already-computed shadow result to Mods. */ \
    void ModSetReplicaShadowVisible(bool visible); \
    void ModNetSpawn(OpenTK::Mathematics::Vector3 position, \
        OpenTK::Mathematics::Vector3 facing); \
    [[nodiscard]] static Formats::Culling::NodeRef ModSpawnNodeRef( \
        MphRead::Scene& scene, OpenTK::Mathematics::Vector3 position); \
private: \
    [[nodiscard]] bool ModIsRemoteNetworkPlayer() const noexcept; \
    MphRead::Mods::Network::ReplicaShadowVisibility::DrawState _modShadowDrawState{}; \
public:
