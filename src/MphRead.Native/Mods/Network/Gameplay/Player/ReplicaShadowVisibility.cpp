#include "Mods/Network/Gameplay/Player/ReplicaShadowVisibility.hpp"

#include "Scene.hpp"

namespace MphRead::Mods::Network
{
    ReplicaShadowVisibility::DrawState ReplicaShadowVisibility::CaptureVisibleShadow(
        MphRead::Scene* scene)
    {
        DrawState state{};
        if (!scene) return state;

        state.scene = scene;
        state.roomId = scene->RoomId();
        const auto room = scene->Room();
        state.roomIdentity = room.get();
        state.shadowDrawn = true;
        return state;
    }

    bool ReplicaShadowVisibility::MatchesScene(const DrawState& state,
        const MphRead::Scene* scene) noexcept
    {
        if (!scene || state.scene != scene || state.roomId != scene->RoomId()) return false;

        const auto room = scene->Room();
        return state.roomIdentity == room.get();
    }

    bool ReplicaShadowVisibility::ShouldForceBody(bool remoteReplica,
        const DrawState& state) noexcept
    {
        return remoteReplica && state.shadowDrawn;
    }
}
