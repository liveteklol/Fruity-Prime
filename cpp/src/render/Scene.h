#pragma once

#include "formats/Animation.h"
#include "formats/Mat4.h"
#include "formats/Model.h"
#include "formats/Rooms.h"

#include <array>
#include <optional>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fp {

// One drawn copy of a model: EntityBase's model instance plus what
// GetModelTransform would return for it.
struct ModelInstance {
    const Model* model = nullptr;
    Mat4 transform = Mat4::identity(); // the entity transform, model scale not included
    int recolor = 0;
    bool room = false;                 // draw every enabled node, no node transforms (RoomEntity)
    // SpinningEntityBase: degrees per second about `spinAxis`, applied before `transform`.
    float spinDegreesPerSecond = 0;
    float spinStartDegrees = 0;
    std::array<float, 3> spinAxis{0, 1, 0};
    bool floats = false; // ItemInstanceEntity: bob by (sin(spin) + 1) / 8
    bool visible = true;
    float alpha = 1.0f;
    // EntityBase.UpdateTransforms(inst, transform, recolor): the parent transform as given,
    // without the model scale in it (player models).
    bool includeModelScale = true;
    bool useNodeTransform = true;
    float animationScale = 0; // AnimateNode's model scale; 0 means the model's own
    // Nodes whose final matrix is set directly (Kanden's stinglarva), by node index.
    std::vector<Mat4> nodeOverrides;
    AnimationState animation; // SetUpModel: animation 0, looping
    // Slot 1: material, texture and (unless cleared) texcoord animation over slot 0's.
    AnimationState secondary;
    bool secondaryTexcoord = true;
    long long animationTicks = 0; // 30 Hz steps applied so far
    // Material.Diffuse set by the entity before drawing (NodeDefenseEntity's
    // team colors): material id, color 0-1.
    std::vector<std::pair<int, std::array<float, 3>>> diffuseOverrides;
    // PlayerDraw.GetEmission: a glow added to lit materials (team suits), as a
    // DS BGR555 color; 0 for none.
    uint16_t emission = 0;
    // DynamicLightEntityBase.GetLightInfo: lights of its own instead of the
    // room's (the players and the Halfturret, lit by the light sources they are in).
    struct Lights {
        std::array<float, 3> light1Vector, light1Color, light2Vector, light2Color;
    };
    std::optional<Lights> lights;

    // The C# GetModelTransform at `seconds` into the scene.
    Mat4 modelTransform(double seconds) const;
};

// Geometry rebuilt every simulation tick (beam trails, single particles): the
// C# renderer's Trail and Particle items -- translucent, unlit, no culling,
// the vertex color multiplying one texture of `model`.
struct DynamicDraw {
    const Model* model = nullptr; // where the texture comes from
    int textureId = -1;
    int paletteId = -1;
    int recolor = 0;
    RepeatMode xRepeat = RepeatMode::Clamp;
    RepeatMode yRepeat = RepeatMode::Clamp;
    Mat4 transform = Mat4::identity();
    bool billboard = false; // faces the camera around the transform's origin
    float alpha = 1.0f;
    uint32_t firstVertex = 0; // into Scene::dynamicVertices
    uint32_t vertexCount = 0; // triangles
};

struct Scene {
    const RoomMetadata* room = nullptr;
    std::vector<Vertex> dynamicVertices;
    std::vector<DynamicDraw> dynamicDraws;
    std::vector<ModelInstance> instances; // instances[0] is the room
    std::vector<std::unique_ptr<Model>> models;
    // Set by World: it steps animations and the clock; otherwise the renderer does, from wall time.
    bool externalAnimation = false;
    double seconds = 0;

    // Loads a model from the metadata table once; nullptr when it cannot be loaded.
    const Model* model(const std::filesystem::path& root, const std::string& name);

private:
    std::map<std::string, const Model*> m_byName;
};

} // namespace fp
