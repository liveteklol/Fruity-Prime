#include "CameraSequence.h"

#include "formats/Model.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace fp {

namespace {

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 normalized(const Vec3& v)
{
    const float length = std::sqrt(dot(v, v));
    return length > 0 ? v * (1 / length) : v;
}

// GetVec4: the cubic Bezier weights at `t`.
std::array<float, 4> bezier(float t)
{
    const float inverse = 1 - t;
    return {inverse * inverse * inverse, 3 * t * inverse * inverse, 3 * t * t * inverse, t * t * t};
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a * (1 - t) + b * t; }

} // namespace

std::optional<CameraSequence> CameraSequence::load(const std::filesystem::path& root, const std::string& file, int id)
{
    const std::filesystem::path path = resolveCaseInsensitive(root, "cameraEditor/" + file);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    const std::vector<uint8_t> bytes = readFile(path);
    if (bytes.size() < 8) {
        return std::nullopt;
    }
    auto read = [&](size_t offset, auto& out) { std::memcpy(&out, bytes.data() + offset, sizeof(out)); };
    auto fixed = [&](size_t offset) {
        int32_t raw;
        read(offset, raw);
        return raw / 4096.0f;
    };
    // CameraSequenceHeader: count, version, padding; then 100-byte RawCameraSequenceKeyframes.
    uint16_t count;
    read(0, count);
    CameraSequence seq;
    seq.m_id = id;
    seq.m_name = file.substr(0, file.rfind(".bin"));
    for (size_t i = 0; i < count && 8 + (i + 1) * 100 <= bytes.size(); i++) {
        const size_t o = 8 + i * 100;
        CameraKeyframe k;
        k.position = {fixed(o), fixed(o + 4), fixed(o + 8)};
        k.toTarget = {fixed(o + 12), fixed(o + 16), fixed(o + 20)};
        k.roll = fixed(o + 24);
        k.fov = fixed(o + 28);
        k.moveTime = fixed(o + 32);
        k.holdTime = fixed(o + 36);
        k.fadeInTime = fixed(o + 40);
        k.fadeOutTime = fixed(o + 44);
        k.fadeInType = bytes[o + 48];
        k.fadeOutType = bytes[o + 49];
        k.prevFrameInfluence = bytes[o + 50];
        k.afterFrameInfluence = bytes[o + 51];
        k.useEntityTransform = bytes[o + 52] != 0;
        read(o + 56, k.posEntityType);
        read(o + 58, k.posEntityId);
        read(o + 60, k.targetEntityType);
        read(o + 62, k.targetEntityId);
        read(o + 64, k.messageTargetType);
        read(o + 66, k.messageTargetId);
        read(o + 68, k.messageId);
        read(o + 70, k.messageParam);
        k.easing = fixed(o + 72);
        const char* name = reinterpret_cast<const char*>(bytes.data() + o + 84);
        k.nodeName.assign(name, strnlen(name, 16));
        seq.m_keyframes.push_back(std::move(k));
    }
    // The game loops the multiplayer intros (scene_setup, then match states 1/2 in process_frame).
    seq.loop = id > 171;
    return seq;
}

std::optional<CameraSequence> CameraSequence::loadIntro(const std::filesystem::path& root, int roomId)
{
    // SceneSetup: sequence roomId - 93 + 172 for the arenas.
    const int index = roomId - 93;
    if (index < 0 || index > 26) {
        return std::nullopt;
    }
    char file[32];
    std::snprintf(file, sizeof(file), "mp%02d_intro.bin", index);
    std::optional<CameraSequence> seq = load(root, file, 172 + index);
    if (seq && seq->m_keyframes.empty()) {
        return std::nullopt;
    }
    return seq;
}

float CameraSequence::length() const
{
    float total = 0;
    for (const CameraKeyframe& k : m_keyframes) {
        total += k.holdTime + k.moveTime;
    }
    return total;
}

void CameraSequence::start()
{
    m_complete = false;
    m_keyframeElapsed = 0;
    m_keyframeIndex = 0;
    if (!m_keyframes.empty()) {
        calculateFrameValues();
    }
}

void CameraSequence::process(float frameTime)
{
    if (m_complete || m_keyframes.empty()) {
        return;
    }
    const CameraKeyframe& curFrame = m_keyframes[m_keyframeIndex];
    const float frameLength = curFrame.holdTime + curFrame.moveTime;
    calculateFrameValues();
    m_keyframeElapsed += frameTime;
    if (m_keyframeElapsed >= frameLength) {
        m_keyframeElapsed -= frameLength;
        if (m_keyframeElapsed >= 1 / 60.0f) {
            m_keyframeElapsed = 1 / 60.0f - 1 / 4096.0f;
        }
        m_keyframeIndex++;
        if (m_keyframeIndex >= m_keyframes.size()) {
            if (loop) {
                // Restart
                m_keyframeElapsed = 0;
                m_keyframeIndex = 0;
                calculateFrameValues();
            } else {
                m_complete = true;
                m_keyframeIndex--;
                m_keyframeElapsed = frameLength;
            }
        }
    }
}

void CameraSequence::calculateFrameValues()
{
    // CalculateFrameValues: a Bezier from the current keyframe to the next,
    // its handles set by the neighbours when the influence bits ask for it.
    const CameraKeyframe& cur = m_keyframes[m_keyframeIndex];
    float movePercent = 0;
    const float moveElapsed = m_keyframeElapsed - cur.holdTime;
    if (moveElapsed >= 0 && cur.moveTime > 0) {
        movePercent = moveElapsed / cur.moveTime;
    }
    const std::array<float, 4> moveVec = bezier(movePercent);
    // current/prev/after/next
    const std::array<float, 4> factorVec{0, (cur.prevFrameInfluence & 1) == 0 ? 1 / 3.0f : 0,
        (cur.afterFrameInfluence & 1) == 0 ? 2 / 3.0f : 1, 1};
    const float factor = factorVec[0] * moveVec[0] + factorVec[1] * moveVec[1] + factorVec[2] * moveVec[2] + factorVec[3] * moveVec[3];
    Vec3 position, toTarget;
    float roll, fov;
    const CameraKeyframe* next = m_keyframeIndex + 1 < m_keyframes.size() ? &m_keyframes[m_keyframeIndex + 1] : nullptr;
    if (next == nullptr) {
        position = cur.position;
        toTarget = cur.toTarget;
        roll = cur.roll;
        fov = cur.fov;
    } else if (((cur.prevFrameInfluence | cur.afterFrameInfluence) & 2) != 0 && cur.moveTime > 0) {
        const Vec3 curToNextPos = (next->position - cur.position) * (1 / cur.moveTime);
        const Vec3 curToNextTarget = (next->toTarget - cur.toTarget) * (1 / cur.moveTime);
        Vec3 prevPos, prevTarget;
        const CameraKeyframe* prev = m_keyframeIndex > 0 ? &m_keyframes[m_keyframeIndex - 1] : nullptr;
        if (prev != nullptr && (cur.prevFrameInfluence & 2) != 0 && prev->moveTime > 0) {
            const float easing = 1 / 6.0f * cur.moveTime * cur.easing;
            const Vec3 prevToCurPos = (cur.position - prev->position) * (1 / prev->moveTime);
            prevPos = (curToNextPos + prevToCurPos) * easing + cur.position;
            const Vec3 prevToCurTarget = (cur.toTarget - prev->toTarget) * (1 / prev->moveTime);
            prevTarget = (curToNextTarget + prevToCurTarget) * easing + cur.toTarget;
        } else {
            const float easing = 1 / 3.0f * cur.moveTime * cur.easing;
            prevPos = curToNextPos * easing + cur.position;
            prevTarget = curToNextTarget * easing + cur.toTarget;
        }
        Vec3 afterPos, afterTarget;
        const CameraKeyframe* after = m_keyframeIndex + 2 < m_keyframes.size() ? &m_keyframes[m_keyframeIndex + 2] : nullptr;
        if (after != nullptr && (cur.afterFrameInfluence & 2) != 0 && next->moveTime > 0) {
            const float easing = -1 / 6.0f * cur.moveTime * next->easing;
            const Vec3 nextToAfterPos = (after->position - next->position) * (1 / next->moveTime);
            afterPos = (curToNextPos + nextToAfterPos) * easing + next->position;
            const Vec3 nextToAfterTarget = (after->toTarget - next->toTarget) * (1 / next->moveTime);
            afterTarget = (curToNextTarget + nextToAfterTarget) * easing + next->toTarget;
        } else {
            const float easing = -1 / 3.0f * cur.moveTime * cur.easing;
            afterPos = curToNextPos * easing + next->position;
            afterTarget = curToNextTarget * easing + next->toTarget;
        }
        const std::array<float, 4> w = bezier(factor);
        position = cur.position * w[0] + prevPos * w[1] + afterPos * w[2] + next->position * w[3];
        toTarget = cur.toTarget * w[0] + prevTarget * w[1] + afterTarget * w[2] + next->toTarget * w[3];
        roll = cur.roll * (1 - factor) + next->roll * factor;
        fov = cur.fov * (1 - factor) + next->fov * factor;
    } else {
        position = lerp(cur.position, next->position, factor);
        toTarget = lerp(cur.toTarget, next->toTarget, factor);
        roll = cur.roll * (1 - factor) + next->roll * factor;
        fov = cur.fov * (1 - factor) + next->fov * factor;
    }
    m_view.fov = fov * 2;
    m_view.position = position;
    m_view.target = position + toTarget;
    Vec3 up{0, 1, 0};
    if (std::abs(roll) >= 1 / 4096.0f) {
        // As the C# has it: the roll's cosine on the sideways axis, its sine on the up vector's height.
        const float angle = (roll + 90) * std::numbers::pi_v<float> / 180;
        const Vec3 facing = m_view.target - m_view.position;
        const Vec3 side = normalized(cross(up, facing));
        const Vec3 camUp = normalized(cross(facing, side));
        up = {side[0] * std::cos(angle), camUp[1] * std::sin(angle), side[2] * std::cos(angle)};
    }
    m_view.up = up;
}

} // namespace fp
