#include "Mods/Network/Gameplay/Player/ReplicaShadowVisibility.hpp"

#include "Formats/CollisionDetection.hpp"
#include "Formats/Types.hpp"
#include "Scene.hpp"

#include <limits>

namespace MphRead::Mods::Network
{
    using OpenTK::Mathematics::Vector3;

    ReplicaShadowVisibility::Projection ReplicaShadowVisibility::FindProjection(
        MphRead::Scene* scene, Vector3 bodyCenter)
    {
        Projection projection{};
        if (!scene) return projection;

        projection.scene = scene;
        projection.roomId = scene->RoomId();
        const auto room = scene->Room();
        projection.roomIdentity = room.get();

        const Vector3 shadowEnd(bodyCenter.X, bodyCenter.Y - 10.0F, bodyCenter.Z);
        Formats::CollisionResult collision{};
        if (!Formats::CollisionDetection::CheckBetweenPoints(bodyCenter, shadowEnd,
            Formats::TestFlags::None, scene, collision)
            || collision.Plane.Y < Fixed::ToFloat(4))
        {
            return projection;
        }

        projection.hasGround = true;
        projection.height = bodyCenter.Y - collision.Position.Y;
        return projection;
    }

    bool ReplicaShadowVisibility::MatchesScene(const Projection& projection,
        const MphRead::Scene* scene) noexcept
    {
        if (!scene || projection.scene != scene || projection.roomId != scene->RoomId()) return false;

        const auto room = scene->Room();
        return projection.roomIdentity == room.get();
    }

    bool ReplicaShadowVisibility::ShouldForceBody(bool remoteReplica,
        const Projection& projection, const FadeState& fade) noexcept
    {
        if (!remoteReplica || !projection.hasGround || projection.height >= 10.0F)
        {
            return false;
        }

        float alpha = fade.currentAlpha * (1.0F - projection.height / 10.0F);
        if (fade.health == 0)
        {
            const float respawnTime = fade.deathCountdown > 0.0F
                ? static_cast<float>(std::numeric_limits<std::uint16_t>::max())
                : static_cast<float>(fade.respawnTime);
            const float decrease = 2.0F * (respawnTime - fade.respawnTimer) / 2.0F;
            alpha -= decrease;
        }
        return alpha > 0.0F;
    }
}
