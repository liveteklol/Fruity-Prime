#include "Animation.h"

#include "Enums.h"
#include "Model.h"

#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace fp {

namespace {

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("animation file truncated at " + std::to_string(offset));
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::string name32(const std::vector<uint8_t>& bytes, size_t offset)
{
    const char* p = reinterpret_cast<const char*>(bytes.data() + offset);
    return std::string(p, strnlen(p, 32));
}

std::vector<float> readFixed(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
{
    std::vector<float> out(count);
    for (size_t i = 0; i < count; i++) {
        out[i] = fxToFloat(readAt<int32_t>(bytes, offset + i * 4));
    }
    return out;
}

std::vector<float> readAngles(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
{
    std::vector<float> out(count);
    for (size_t i = 0; i < count; i++) {
        out[i] = readAt<uint16_t>(bytes, offset + i * 2) / 65536.0f * 2.0f * std::numbers::pi_v<float>;
    }
    return out;
}

// Blend bytes, then lengths, then indices, for n channels starting at `offset`.
template <size_t N>
std::array<Channel, N> readChannels(const std::vector<uint8_t>& b, size_t blendOffset, size_t lengthOffset, size_t indexOffset)
{
    std::array<Channel, N> out{};
    for (size_t i = 0; i < N; i++) {
        out[i].blend = readAt<uint8_t>(b, blendOffset + i);
        out[i].length = readAt<uint16_t>(b, lengthOffset + i * 2);
        out[i].index = readAt<uint16_t>(b, indexOffset + i * 2);
    }
    return out;
}

constexpr size_t kNodeAnimationSize = 48;
constexpr size_t kMaterialAnimationSize = 140;
constexpr size_t kTexcoordAnimationSize = 60;
constexpr size_t kTextureAnimationSize = 44;

} // namespace

int AnimationSet::frameCount(size_t index) const
{
    if (index < node.size() && !node[index].empty()) {
        return node[index].frameCount;
    }
    if (index < material.size() && !material[index].empty()) {
        return material[index].frameCount;
    }
    if (index < texture.size() && !texture[index].empty()) {
        return texture[index].frameCount;
    }
    if (index < texcoord.size() && !texcoord[index].empty()) {
        return texcoord[index].frameCount;
    }
    return 0;
}

void AnimationSet::append(AnimationSet&& other)
{
    for (auto& g : other.node) {
        node.push_back(std::move(g));
    }
    for (auto& g : other.material) {
        material.push_back(std::move(g));
    }
    for (auto& g : other.texcoord) {
        texcoord.push_back(std::move(g));
    }
    for (auto& g : other.texture) {
        texture.push_back(std::move(g));
    }
}

AnimationSet loadAnimations(const std::filesystem::path& file, const std::vector<Node>& nodes)
{
    const std::vector<uint8_t> b = readFile(file);
    const auto nodeGroupOffset = readAt<uint32_t>(b, 0);
    const auto materialGroupOffset = readAt<uint32_t>(b, 8);
    const auto texcoordGroupOffset = readAt<uint32_t>(b, 12);
    const auto textureGroupOffset = readAt<uint32_t>(b, 16);
    const auto count = readAt<uint16_t>(b, 20);
    AnimationSet set;

    for (uint16_t g = 0; g < count; g++) {
        const auto offset = readAt<uint32_t>(b, nodeGroupOffset + g * 4);
        NodeAnimationGroup group;
        if (offset != 0) {
            group.frameCount = static_cast<int>(readAt<uint32_t>(b, offset));
            const auto scaleLut = readAt<uint32_t>(b, offset + 4);
            const auto rotateLut = readAt<uint32_t>(b, offset + 8);
            const auto translateLut = readAt<uint32_t>(b, offset + 12);
            const auto animationOffset = readAt<uint32_t>(b, offset + 16);
            if (!nodes.empty() && offset > animationOffset) {
                const size_t n = (offset - animationOffset) / kNodeAnimationSize;
                for (size_t i = 0; i < n; i++) {
                    const size_t a = animationOffset + i * kNodeAnimationSize;
                    NodeAnimation anim;
                    anim.scale = readChannels<3>(b, a + 0, a + 4, a + 10);
                    anim.rotate = readChannels<3>(b, a + 16, a + 20, a + 26);
                    anim.translate = readChannels<3>(b, a + 32, a + 36, a + 42);
                    const std::string name = i < nodes.size() ? nodes[i].name : "__no_node_" + std::to_string(i);
                    group.animations.emplace(name, anim);
                }
                group.scales = readFixed(b, scaleLut, (rotateLut - scaleLut) / 4);
                group.rotations = readAngles(b, rotateLut, (translateLut - rotateLut) / 2);
                group.translations = readFixed(b, translateLut, (animationOffset - translateLut) / 4);
            }
        }
        set.node.push_back(std::move(group));
    }

    for (uint16_t g = 0; g < count; g++) {
        const auto offset = readAt<uint32_t>(b, materialGroupOffset + g * 4);
        MaterialAnimationGroup group;
        if (offset != 0) {
            group.frameCount = static_cast<int>(readAt<uint32_t>(b, offset));
            const auto colorLut = readAt<uint32_t>(b, offset + 4);
            const auto animationCount = readAt<uint32_t>(b, offset + 8);
            const auto animationOffset = readAt<uint32_t>(b, offset + 12);
            for (uint32_t i = 0; i < animationCount; i++) {
                const size_t a = animationOffset + i * kMaterialAnimationSize;
                MaterialAnimation anim;
                anim.diffuse = readChannels<3>(b, a + 0x44, a + 0x48, a + 0x4E);
                anim.ambient = readChannels<3>(b, a + 0x54, a + 0x58, a + 0x5E);
                anim.specular = readChannels<3>(b, a + 0x64, a + 0x68, a + 0x6E);
                anim.alpha.blend = readAt<uint8_t>(b, a + 0x84);
                anim.alpha.length = readAt<uint16_t>(b, a + 0x86);
                anim.alpha.index = readAt<uint16_t>(b, a + 0x88);
                group.animations.emplace(name32(b, a), anim);
            }
            if (animationCount > 0) {
                group.colors.resize(animationOffset - colorLut);
                for (size_t i = 0; i < group.colors.size(); i++) {
                    group.colors[i] = readAt<uint8_t>(b, colorLut + i);
                }
            }
        }
        set.material.push_back(std::move(group));
    }

    for (uint16_t g = 0; g < count; g++) {
        const auto offset = readAt<uint32_t>(b, texcoordGroupOffset + g * 4);
        TexcoordAnimationGroup group;
        if (offset != 0) {
            group.frameCount = static_cast<int>(readAt<uint32_t>(b, offset));
            const auto scaleLut = readAt<uint32_t>(b, offset + 4);
            const auto rotateLut = readAt<uint32_t>(b, offset + 8);
            const auto translateLut = readAt<uint32_t>(b, offset + 12);
            const auto animationCount = readAt<uint32_t>(b, offset + 16);
            const auto animationOffset = readAt<uint32_t>(b, offset + 20);
            for (uint32_t i = 0; i < animationCount; i++) {
                const size_t a = animationOffset + i * kTexcoordAnimationSize;
                TexcoordAnimation anim;
                anim.scale = readChannels<2>(b, a + 32, a + 34, a + 38);
                anim.rotate.blend = readAt<uint8_t>(b, a + 42);
                anim.rotate.length = readAt<uint16_t>(b, a + 44);
                anim.rotate.index = readAt<uint16_t>(b, a + 46);
                anim.translate = readChannels<2>(b, a + 48, a + 50, a + 54);
                group.animations.emplace(name32(b, a), anim);
            }
            if (animationCount > 0) {
                group.scales = readFixed(b, scaleLut, (rotateLut - scaleLut) / 4);
                group.rotations = readAngles(b, rotateLut, (translateLut - rotateLut) / 2);
                group.translations = readFixed(b, translateLut, (animationOffset - translateLut) / 4);
            }
        }
        set.texcoord.push_back(std::move(group));
    }

    for (uint16_t g = 0; g < count; g++) {
        const auto offset = readAt<uint32_t>(b, textureGroupOffset + g * 4);
        TextureAnimationGroup group;
        if (offset != 0) {
            group.frameCount = readAt<uint16_t>(b, offset);
            const auto frameIndexCount = readAt<uint16_t>(b, offset + 2);
            const auto textureIdCount = readAt<uint16_t>(b, offset + 4);
            const auto paletteIdCount = readAt<uint16_t>(b, offset + 6);
            const auto animationCount = readAt<uint16_t>(b, offset + 8);
            const auto frameIndexOffset = readAt<uint32_t>(b, offset + 12);
            const auto textureIdOffset = readAt<uint32_t>(b, offset + 16);
            const auto paletteIdOffset = readAt<uint32_t>(b, offset + 20);
            const auto animationOffset = readAt<uint32_t>(b, offset + 24);
            for (uint16_t i = 0; i < animationCount; i++) {
                const size_t a = animationOffset + i * kTextureAnimationSize;
                group.animations.emplace(name32(b, a), TextureAnimation{readAt<uint16_t>(b, a + 32), readAt<uint16_t>(b, a + 34)});
            }
            auto readU16 = [&](size_t at, size_t n) {
                std::vector<uint16_t> v(n);
                for (size_t i = 0; i < n; i++) {
                    v[i] = readAt<uint16_t>(b, at + i * 2);
                }
                return v;
            };
            if (animationCount > 0) {
                group.frameIndices = readU16(frameIndexOffset, frameIndexCount);
                group.textureIds = readU16(textureIdOffset, textureIdCount);
                group.paletteIds = readU16(paletteIdOffset, paletteIdCount);
            }
        }
        set.texture.push_back(std::move(group));
    }
    return set;
}

float interpolate(const std::vector<float>& values, const Channel& ch, int frame, int frameCount, bool isRotation)
{
    auto at = [&](long i) { return i >= 0 && i < static_cast<long>(values.size()) ? values[static_cast<size_t>(i)] : 0.0f; };
    const int start = ch.index;
    const int blend = ch.blend;
    const int lutLength = ch.length;
    if (lutLength == 1) {
        return at(start);
    }
    if (blend == 1) {
        return at(start + frame);
    }
    if (blend <= 0) {
        return at(start);
    }
    const int limit = (frameCount - 1) >> (blend >> 1) << (blend >> 1);
    if (frame >= limit) {
        return at(start + lutLength - (frameCount - limit - (frame - limit)));
    }
    const int index = frame / blend;
    const int remainder = frame % blend;
    if (remainder == 0) {
        return at(start + index);
    }
    float first = at(start + index);
    float second = at(start + index + 1);
    if (isRotation) {
        const float pi = std::numbers::pi_v<float>;
        if (first - second > pi) {
            second += pi * 2.0f;
        } else if (first - second < -pi) {
            first += pi * 2.0f;
        }
    }
    const float factor = 1.0f / blend * remainder;
    return first + (second - first) * factor;
}

Mat4 animateNode(const NodeAnimationGroup& group, const NodeAnimation& a, float modelScale, int frame)
{
    const int fc = group.frameCount;
    const float sx = interpolate(group.scales, a.scale[0], frame, fc);
    const float sy = interpolate(group.scales, a.scale[1], frame, fc);
    const float sz = interpolate(group.scales, a.scale[2], frame, fc);
    const float rx = interpolate(group.rotations, a.rotate[0], frame, fc, true);
    const float ry = interpolate(group.rotations, a.rotate[1], frame, fc, true);
    const float rz = interpolate(group.rotations, a.rotate[2], frame, fc, true);
    const float tx = interpolate(group.translations, a.translate[0], frame, fc);
    const float ty = interpolate(group.translations, a.translate[1], frame, fc);
    const float tz = interpolate(group.translations, a.translate[2], frame, fc);
    Mat4 m = Mat4::translation(tx / modelScale, ty / modelScale, tz / modelScale);
    m = Mat4::rotationX(rx) * Mat4::rotationY(ry) * Mat4::rotationZ(rz) * m;
    return Mat4::scale(sx, sy, sz) * m;
}

Mat4 animateTexcoords(const TexcoordAnimationGroup& group, const TexcoordAnimation& a, int frame)
{
    const int fc = group.frameCount;
    const float scaleS = interpolate(group.scales, a.scale[0], frame, fc);
    const float scaleT = interpolate(group.scales, a.scale[1], frame, fc);
    const float rotate = interpolate(group.rotations, a.rotate, frame, fc, true);
    const float translateS = interpolate(group.translations, a.translate[0], frame, fc);
    const float translateT = interpolate(group.translations, a.translate[1], frame, fc);
    Mat4 m = Mat4::translation(translateS, translateT, 0.0f);
    if (rotate != 0) {
        m = Mat4::translation(0.5f, 0.5f, 0.0f) * m;
        m = Mat4::rotationZ(rotate) * m;
        m = Mat4::translation(-0.5f, -0.5f, 0.0f) * m;
    }
    return Mat4::scale(scaleS, scaleT, 1.0f) * m;
}

void AnimationState::set(const AnimationSet& set, int animationIndex, uint16_t animFlags)
{
    if (set.count() == 0 || animationIndex < 0 || animationIndex >= static_cast<int>(set.count())) {
        index = -1;
        return;
    }
    index = animationIndex;
    step = 1;
    flags = animFlags;
    frameCount = set.frameCount(static_cast<size_t>(animationIndex));
    frame = (animFlags & AnimFlags::Reverse) ? frameCount - 1 : 0;
}

void AnimationState::advance()
{
    if (index < 0 || frameCount <= 0 || (flags & (AnimFlags::Paused | AnimFlags::Ended))) {
        return;
    }
    if (flags & AnimFlags::PingPong) {
        if (flags & AnimFlags::Reverse) {
            if (frame <= step) {
                frame = step - frame;
                flags ^= AnimFlags::Reverse;
            } else {
                frame -= step;
            }
        } else {
            frame += step;
            if (frame >= frameCount - 1) {
                frame = 2 * frameCount - frame - 2;
                flags ^= AnimFlags::Reverse;
            }
        }
    } else if (flags & AnimFlags::Reverse) {
        if (frame > step) {
            frame -= step;
        } else if (flags & AnimFlags::NoLoop) {
            frame = 0;
            flags |= AnimFlags::Ended;
        } else if (frame == step) {
            frame = 0;
        } else {
            frame = frameCount - (step - frame);
        }
    } else {
        frame += step;
        if (frame >= frameCount - 1) {
            if (flags & AnimFlags::NoLoop) {
                frame = frameCount - 1;
                flags |= AnimFlags::Ended;
            } else if (frame >= frameCount) {
                frame -= frameCount;
            }
        }
    }
}

} // namespace fp
