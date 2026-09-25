#pragma once

#include "Model.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fp {

// RGB15 to RGBA8 (R in the low byte), as ColorRgba(ushort) does.
uint32_t rgb15ToRgba(uint16_t value, uint8_t alpha = 255);

// A HUD sprite sheet (HudInfo.GetHudObject): 4-bit palette indices, one byte
// per pixel, 8x8 tiles row by row within each frame, frames one after another.
struct HudObject {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;
    std::vector<uint32_t> palette; // 16 colors per palette
    std::vector<int> animFrames;   // HudObjectInstance.SetAnimationFrames: image index per 1/30 s

    int frameCount() const { return width > 0 && height > 0 ? static_cast<int>(indices.size()) / (width * height) : 0; }
    int paletteCount() const { return static_cast<int>(palette.size()) / 16; }
    uint8_t index(int frame, int x, int y) const;
};

HudObject loadHudObject(const std::filesystem::path& root, const std::string& file);

// A tiled background layer (HudInfo.CharMapToTexture) as one image. Zero tile
// counts mean the whole map. `paletteOverride` replaces all but the first two
// colors, and `paletteOut` receives the palette that was used.
Image loadCharMap(const std::filesystem::path& root, const std::string& file, int startX = 0, int startY = 0,
    int tilesX = 0, int tilesY = 0, const std::vector<uint16_t>* paletteOverride = nullptr,
    std::vector<uint16_t>* paletteOut = nullptr);

// Font.Normal: the game's 8x8 font out of arm9.bin, at the offsets
// Extract.LoadRuntimeData has for each release.
struct HudFont {
    std::vector<int> widths;
    std::vector<int> offsets;
    std::vector<uint8_t> indices; // 64 per glyph, same layout as HudObject
    int minCharacter = 32;

    bool valid() const { return !widths.empty(); }
    int glyphCount() const { return static_cast<int>(indices.size()) / 64; }
    int glyph(int ch) const;
};

HudFont loadFont(const std::filesystem::path& root);

// Strings.GetHudMessage: the HUD's own text out of stringTables.
class HudStrings {
public:
    explicit HudStrings(std::filesystem::path root) : m_root(std::move(root)) {}
    std::string hudMessage(int id);
    // Strings.GetMessage: entry `type` + id of a string table ("WeaponNames.bin").
    std::string message(char type, int id, const std::string& table);

private:
    std::filesystem::path m_root;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> m_tables;
};

} // namespace fp
