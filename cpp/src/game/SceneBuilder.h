#pragma once

#include "formats/Entities.h"
#include "render/Scene.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace fp {

// The room plus the models of its static entities (ObjectEntity,
// PlatformEntity); World adds the ones that change while playing.
std::unique_ptr<Scene> buildScene(const std::filesystem::path& root, const RoomMetadata& room, Model roomModel,
    const std::vector<Entity>& entities);

} // namespace fp
