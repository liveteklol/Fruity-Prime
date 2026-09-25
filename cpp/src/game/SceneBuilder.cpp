#include "SceneBuilder.h"

#include "formats/Metadata.h"

#include <QtGlobal>

#include <cmath>

namespace fp {

namespace {

// Offsets into the entity records, from the C# structs (after the 40-byte header).
namespace Object {
constexpr size_t ModelId = 48;
} // namespace Object
namespace Platform {
constexpr size_t ModelId = 44;
} // namespace Platform


} // namespace

std::unique_ptr<Scene> buildScene(const std::filesystem::path& root, const RoomMetadata& room, Model roomModel,
    const std::vector<Entity>& entities)
{
    auto scene = std::make_unique<Scene>();
    scene->room = &room;
    scene->models.push_back(std::make_unique<Model>(std::move(roomModel)));
    ModelInstance roomInstance;
    roomInstance.model = scene->models.back().get();
    roomInstance.room = true;
    roomInstance.animation.set(roomInstance.model->animations(), 0);
    scene->instances.push_back(roomInstance);

    auto add = [&](const std::string& name, const Mat4& transform, int recolor = 0) -> ModelInstance* {
        const Model* model = scene->model(root, name);
        if (model == nullptr) {
            return nullptr;
        }
        ModelInstance inst;
        inst.model = model;
        inst.transform = transform;
        inst.recolor = recolor >= 0 && recolor < static_cast<int>(model->recolorCount()) ? recolor : 0;
        inst.animation.set(model->animations(), 0);
        scene->instances.push_back(inst);
        return &scene->instances.back();
    };

    for (const Entity& e : entities) {
        const Mat4 transform = Mat4::fromVectors(e.facing, e.up, e.position);
        switch (e.type) {
        case EntityType::Object: {
            const int32_t modelId = e.i32(Object::ModelId);
            if (modelId >= 0 && modelId < static_cast<int>(objectTable().size())) {
                const ObjectMetadata& meta = objectTable()[modelId];
                if (ModelInstance* inst = add(meta.name, transform, meta.recolorId)) {
                    inst->animation.set(inst->model->animations(), meta.animationIds[0]); // state 0
                }
            }
            break;
        }
        case EntityType::Platform: {
            uint32_t modelId = e.u32(Platform::ModelId);
            if (modelId == 1) {
                modelId = 0; // Metadata.GetPlatformById
            }
            if (modelId < platformTable().size() && platformTable()[modelId].name != nullptr) {
                add(platformTable()[modelId].name, transform);
            }
            break;
        }
        default:
            break;
        }
    }
    return scene;
}

} // namespace fp
