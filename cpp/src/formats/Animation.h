#pragma once

#include "Mat4.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// *_Anim.bin files: node, material, texcoord and texture animation groups.
// Ported from Read.LoadAnimation and the Animate* functions in Model.cs.
namespace fp {

struct Node;

struct Channel {
    uint8_t blend = 1;
    uint16_t length = 1;
    uint16_t index = 0;
};

struct NodeAnimation {
    std::array<Channel, 3> scale, rotate, translate;
};

struct NodeAnimationGroup {
    int frameCount = 0;
    std::vector<float> scales, rotations, translations;
    std::map<std::string, NodeAnimation> animations; // by node name
    bool empty() const { return animations.empty(); }
};

struct MaterialAnimation {
    std::array<Channel, 3> diffuse, ambient, specular;
    Channel alpha;
};

struct MaterialAnimationGroup {
    int frameCount = 0;
    std::vector<float> colors;
    std::map<std::string, MaterialAnimation> animations; // by material name
    bool empty() const { return animations.empty(); }
};

struct TexcoordAnimation {
    std::array<Channel, 2> scale, translate;
    Channel rotate;
};

struct TexcoordAnimationGroup {
    int frameCount = 0;
    std::vector<float> scales, rotations, translations;
    std::map<std::string, TexcoordAnimation> animations; // by material name
    bool empty() const { return animations.empty(); }
};

struct TextureAnimation {
    uint16_t count = 0;
    uint16_t startIndex = 0;
};

struct TextureAnimationGroup {
    int frameCount = 0;
    std::vector<uint16_t> frameIndices, textureIds, paletteIds;
    std::map<std::string, TextureAnimation> animations; // by material name
    bool empty() const { return animations.empty(); }
};

struct AnimationSet {
    std::vector<NodeAnimationGroup> node;
    std::vector<MaterialAnimationGroup> material;
    std::vector<TexcoordAnimationGroup> texcoord;
    std::vector<TextureAnimationGroup> texture;

    size_t count() const { return node.size(); }
    // The frame count SetAnimation takes for slot 0: node > material > texture > texcoord.
    int frameCount(size_t index) const;
    void append(AnimationSet&& other);
};

AnimationSet loadAnimations(const std::filesystem::path& file, const std::vector<Node>& nodes);

// Model.InterpolateAnimation
float interpolate(const std::vector<float>& values, const Channel& channel, int frame, int frameCount, bool isRotation = false);

// Model.AnimateNode
Mat4 animateNode(const NodeAnimationGroup& group, const NodeAnimation& animation, float modelScale, int frame);

// Model.AnimateTexcoords
Mat4 animateTexcoords(const TexcoordAnimationGroup& group, const TexcoordAnimation& animation, int frame);

// AnimFlags, and the frame stepping of ModelInstance.UpdateAnimFrames.
namespace AnimFlags {
constexpr uint16_t PingPong = 0x1;
constexpr uint16_t Reverse = 0x2;
constexpr uint16_t Paused = 0x4;
constexpr uint16_t NoLoop = 0x8;
constexpr uint16_t Ended = 0x10;
} // namespace AnimFlags

struct AnimationState {
    int index = -1;
    int frame = 0;
    int frameCount = 0;
    int step = 1;
    uint16_t flags = 0;

    void set(const AnimationSet& set, int animationIndex, uint16_t animFlags = 0);
    void advance();
};

} // namespace fp
