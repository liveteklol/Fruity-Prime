#pragma once

#include <cstdint>

namespace fp {

enum class TextureFormat : uint8_t {
    Palette2Bit = 0,
    Palette4Bit = 1,
    Palette8Bit = 2,
    PaletteA5I3 = 4,
    DirectRgb = 5,
    PaletteA3I5 = 6,
};

enum class CullingMode : uint8_t { Neither = 0, Front = 1, Back = 2 };

enum class PolygonMode : uint32_t { Modulate = 0, Decal = 1, Toon = 2, Shadow = 3 };

enum class RenderMode : uint8_t { Normal = 0, Decal = 1, Translucent = 2, Unknown3 = 3, Unknown4 = 4 };

enum class RepeatMode : uint8_t { Clamp = 0, Repeat = 1, Mirror = 2 };

enum class TexgenMode : uint32_t { None = 0, Texcoord = 1, Normal = 2, Vertex = 3 };

enum class BillboardMode : uint8_t { None = 0, Sphere = 1, Cylinder = 2 };

namespace NodeLayer {
constexpr int MultiplayerLod0 = 0x8;
constexpr int MultiplayerLod1 = 0x10;
constexpr int MultiplayerU = 0x20;
constexpr int CaptureTheFlag = 0x4000;
} // namespace NodeLayer

inline float fxToFloat(int64_t value) { return static_cast<float>(value) / 4096.0f; }

} // namespace fp
