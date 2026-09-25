#pragma once

#include "Animation.h"
#include "Enums.h"
#include "Mat4.h"
#include "Metadata.h"
#include "Raw.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fp {

// One vertex as the NDS geometry engine leaves it: color.a == 0 marks a
// DIF_AMB diffuse (lit by the shader), color.a < 0 marks "no color command
// yet", which the shader replaces with the material diffuse.
struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
    uint32_t matrixId; // MTX_RESTORE: index into the model's node matrix stack
};

struct Node {
    std::string name;
    int parentIndex;
    int childIndex;
    int nextIndex;
    bool enabled;
    int meshCount;
    int meshId;
    std::array<float, 3> scale;
    std::array<float, 3> angle;
    std::array<float, 3> position;
    BillboardMode billboardMode;
    Mat4 transform = Mat4::identity(); // bind pose, parent chain included
};

struct Material {
    std::string name;
    bool lighting;
    CullingMode culling;
    float alpha;
    int paletteId;
    int textureId;
    RepeatMode xRepeat;
    RepeatMode yRepeat;
    std::array<float, 3> diffuse;
    std::array<float, 3> ambient;
    std::array<float, 3> specular;
    PolygonMode polygonMode;
    RenderMode renderMode;
    TexgenMode texgenMode;
    float scaleS, scaleT, translateS, translateT, rotateZ;
    uint8_t animationFlags; // MatAnimFlags: 1 disables color, 2 disables alpha
};

struct Mesh {
    int materialId;
    int dlistId;
};

struct DrawRange {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> rgba; // R in the low byte
    bool opaque = true;
};

struct Recolor {
    std::string name;
    std::vector<raw::Texture> textures;
    std::shared_ptr<const std::vector<uint8_t>> texelBytes;
    std::vector<std::vector<uint16_t>> palettes;
};

class Model {
public:
    // A model from the metadata table, with every recolor. Ported from Read.ReadModel.
    static Model load(const std::filesystem::path& root, const ModelMetadata& meta);
    // A room: one recolor, textures and palettes in `textureFile`.
    static Model loadRoom(const std::filesystem::path& modelFile, const std::filesystem::path& textureFile,
        const std::filesystem::path& animationFile = {});

    const std::string& name() const { return m_name; }
    float scale() const { return m_scale; }
    const std::vector<Node>& nodes() const { return m_nodes; }
    std::vector<Node>& nodes() { return m_nodes; }
    const std::vector<Mesh>& meshes() const { return m_meshes; }
    const std::vector<Material>& materials() const { return m_materials; }
    const std::vector<Vertex>& vertices() const { return m_vertices; }
    const DrawRange& dlistRange(int dlistId) const { return m_dlistRanges.at(dlistId); }
    size_t textureCount() const { return m_recolors.empty() ? 0 : m_recolors[0].textures.size(); }
    size_t recolorCount() const { return m_recolors.size(); }
    const std::vector<int>& nodeMatrixIds() const { return m_nodeWeights; }
    bool isRoom() const { return m_isRoom; }
    const AnimationSet& animations() const { return m_animations; }

    Image decodeTexture(int textureId, int paletteId, int recolor = 0) const;

    // Model.ComputeNodeMatrices: each node's bind-pose transform, parents applied.
    void computeNodeMatrices();

    // Hides the "_"-prefixed layer nodes the mask excludes. Ported from Model.FilterNodes.
    void filterNodes(int layerMask);

    // Mesh indices a node draws. The file stores MeshId doubled.
    int firstMesh(const Node& node) const { return node.meshId / 2; }

private:
    static Model loadInternal(const std::filesystem::path& root, const ModelMetadata& meta, bool isRoom);
    void buildGeometry(const std::vector<uint8_t>& bytes, const std::vector<raw::DisplayList>& dlists);

    std::string m_name;
    bool m_isRoom = false;
    float m_scale = 1.0f;
    std::vector<Node> m_nodes;
    std::vector<Mesh> m_meshes;
    std::vector<Material> m_materials;
    std::vector<Recolor> m_recolors;
    std::vector<int> m_nodeWeights;
    AnimationSet m_animations;
    std::vector<Vertex> m_vertices;
    std::vector<DrawRange> m_dlistRanges;
};

std::vector<uint8_t> readFile(const std::filesystem::path& path);

// The game's file names do not always match the case on disk.
std::filesystem::path resolveCaseInsensitive(const std::filesystem::path& root, const std::string& relative);

} // namespace fp
