#pragma once

#include "EntityBase.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    // A translated Quake 3 level's mover as something to stand on and run
    // into: its brushes' collision (MapPacker writes it beside the room's),
    // moved each frame to where the mover is (Mods::MapGen::Q3Movers), which
    // carries whoever stands on it the way a platform does. Its look is the
    // room's own node, moved the same way.
    class Q3MoverEntity final : public EntityBase
    {
    public:
        Q3MoverEntity(Scene* scene, std::int32_t roomId, std::int32_t model, std::string collisionPath);

        void Initialize() override;
        [[nodiscard]] bool Process() override;

    private:
        std::int32_t _roomId = 0;
        std::int32_t _model = 0;
        std::string _collisionPath;
    };
}
