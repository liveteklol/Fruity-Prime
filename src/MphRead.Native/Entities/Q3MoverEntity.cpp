#include "Q3MoverEntity.hpp"

#include "../Formats/Collision.hpp"
#include "../Formats/Types.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../Mods/MapGen/Q3Movers.hpp"
#include "../Renderer.hpp"
#include "../Scene.hpp"
#include "Players/PlayerEntity.hpp"

#include <utility>
#include <vector>

namespace MphRead::Entities
{
    Q3MoverEntity::Q3MoverEntity(Scene* scene, std::int32_t roomId, std::int32_t model, std::string collisionPath)
        : EntityBase(EntityType::Q3Mover, "q3model" + std::to_string(model), scene),
          _roomId(roomId),
          _model(model),
          _collisionPath(std::move(collisionPath))
    {
        // Out of the way of the level's own entities' ids, which the scene
        // keys them by.
        Id = 30000 + model;
    }

    void Q3MoverEntity::Initialize()
    {
        EntityBase::Initialize();
        SetCollision(Formats::Collision::Collision::GetEntityCollision(_collisionPath), 0, nullptr);
    }

    bool Q3MoverEntity::Process()
    {
        std::vector<OpenTK::Mathematics::Vector3> players;
        for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
        {
            if (player != nullptr && (player->LoadFlags() & LoadFlags::Active) == LoadFlags::Active
                && player->Health() > 0)
            {
                OpenTK::Mathematics::Vector3 position{};
                player->GetPosition(position);
                players.push_back(position);
            }
        }
        Scene& scene = *_scene;
        // Where it will be next frame: the hunters, processed before it,
        // then meet it where it is when they move, and one stood on it is
        // carried from where it was to there (PlayerInput).
        Mods::MapGen::Q3Movers::Update(_roomId, scene.FrameCount() + 1, players);
        const auto& entCol = EntityCollision[0];
        if (const auto motion = Mods::MapGen::Q3Movers::Motion(_roomId, _model); motion.has_value() && entCol != nullptr)
        {
            // Whoever stands on it is carried as it moves, here: PlayerInput
            // carries a hunter by the move its platform made the frame
            // before, a frame behind, which loses a mover sinking faster
            // than a hunter is pulled down (q3dm19's pillars). That carry is
            // left with nothing to do.
            const OpenTK::Mathematics::Matrix4 was = entCol->Transform;
            UpdateCollisionTransform(0, *motion);
            entCol->Inverse2 = entCol->Inverse1;
            const OpenTK::Mathematics::Matrix4 carry
                = OpenTK::Mathematics::Multiply(OpenTK::Mathematics::Inverted(was), *motion);
            for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
            {
                if (player != nullptr && player->StandingEntCol() == entCol)
                {
                    // Where it was too, or the carry reads as a fall.
                    // A hand's width into it as well: a hunter standing still
                    // is not pulled down, and the carry's rounding would
                    // otherwise lift it off; its collision sets it back on
                    // top.
                    const OpenTK::Mathematics::Vector3 at = player->Position;
                    const OpenTK::Mathematics::Vector3 moved = Matrix::Vec3MultMtx4(at, carry);
                    player->Position = OpenTK::Mathematics::Vector3(moved.X, moved.Y - 0.02F, moved.Z);
                    player->SetPrevPosition(Matrix::Vec3MultMtx4(player->PrevPosition(), carry));
                }
            }
        }
        return EntityBase::Process();
    }
}
