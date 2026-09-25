#pragma once

#include "Collision.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// cameraEditor/*.bin and their playback. Ported from Formats/CameraSequence.cs
// for the multiplayer intros, which move the camera on its own: keyframes
// placed relative to an entity, fades and messages are read but not applied.
namespace fp {

struct CameraKeyframe {
    Vec3 position{}, toTarget{};
    float roll = 0, fov = 0, moveTime = 0, holdTime = 0, fadeInTime = 0, fadeOutTime = 0;
    uint8_t fadeInType = 0, fadeOutType = 0;
    uint8_t prevFrameInfluence = 0, afterFrameInfluence = 0; // flag bits 0/1
    bool useEntityTransform = false;
    int16_t posEntityType = -1, posEntityId = -1, targetEntityType = -1, targetEntityId = -1;
    int16_t messageTargetType = -1, messageTargetId = -1;
    uint16_t messageId = 0, messageParam = 0;
    float easing = 0;
    std::string nodeName;
};

struct CameraView {
    Vec3 position{}, target{}, up{0, 1, 0};
    float fov = 78; // degrees, CameraInfo.Fov
};

class CameraSequence {
public:
    // CameraSequence.Load: `file` under cameraEditor.
    static std::optional<CameraSequence> load(const std::filesystem::path& root, const std::string& file, int id);
    // The room's intro, mp00_intro.bin to mp26_intro.bin (sequences 172-198).
    static std::optional<CameraSequence> loadIntro(const std::filesystem::path& root, int roomId);

    int id() const { return m_id; }
    const std::string& name() const { return m_name; }
    const std::vector<CameraKeyframe>& keyframes() const { return m_keyframes; }
    float length() const;
    bool loop = false;
    bool complete() const { return m_complete; }

    // SetUp with no transition: from the first keyframe.
    void start();
    // One tick of `frameTime` seconds (Process).
    void process(float frameTime);
    const CameraView& view() const { return m_view; }

private:
    void calculateFrameValues();

    int m_id = -1;
    std::string m_name;
    std::vector<CameraKeyframe> m_keyframes;
    size_t m_keyframeIndex = 0;
    float m_keyframeElapsed = 0;
    bool m_complete = false;
    CameraView m_view;
};

} // namespace fp
