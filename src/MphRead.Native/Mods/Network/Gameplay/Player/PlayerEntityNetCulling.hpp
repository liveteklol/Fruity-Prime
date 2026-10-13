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
    void ModResolveNetworkNodeRef(); \
    [[nodiscard]] bool ModNodeUnresolved() const noexcept; \
    void ModNetSpawn(OpenTK::Mathematics::Vector3 position, \
        OpenTK::Mathematics::Vector3 facing); \
    [[nodiscard]] static Formats::Culling::NodeRef ModSpawnNodeRef( \
        MphRead::Scene& scene, OpenTK::Mathematics::Vector3 position); \
private: \
    [[nodiscard]] bool ModIsRemoteNetworkPlayer() const noexcept; \
    void ModRefreshShadowProjection(); \
    MphRead::Mods::Network::ReplicaShadowVisibility::Projection _modShadowProjection{}; \
public:
