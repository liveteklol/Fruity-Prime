#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// How a Quake 3 shader moves, as the texture pack carries it from the bake
// (MapTextureBake) to the import (Q3Import, which lays its layers over the
// faces) and the packer (MapPacker, which writes the room's animation): a
// stage's tcMods, its rgbGen or alphaGen wave and its animMap, for the base
// image of a surface and for each layer drawn over it.
namespace MphRead::Mods::MapGen::ShaderAnim
{
    // A room plays one loop: 100 seconds at the cartridge's 30 animation
    // frames a second. Every speed and frequency is rounded to whole cycles
    // of it (Looped), so the loop joins up where it starts again; Quake's
    // own values, to two decimals, already are.
    inline constexpr std::int32_t LoopFrames = 3000;
    inline constexpr float FramesPerSecond = 30.0F;
    inline constexpr float LoopSeconds = static_cast<float>(LoopFrames) / FramesPerSecond;

    struct Wave
    {
        std::string Function = "sin";
        float Base = 0.0F;
        float Amplitude = 0.0F;
        float Phase = 0.0F;
        float Frequency = 0.0F;
    };

    struct TcMod
    {
        // scroll, scale, rotate, stretch, turb, transform.
        std::string Kind;
        std::array<float, 6> Values{};
        std::optional<ShaderAnim::Wave> Wave;
    };

    // What a texture pack entry is, beyond its image.
    struct EntryMeta
    {
        // "base": the image a Quake texture's faces are drawn with (Of is the
        // texture); "layer": an image laid over them (Of the texture, Order
        // its place); "frame": one of an animMap's images (Of the source
        // index of the entry it animates, Frame its place).
        std::string Role = "base";
        std::int32_t Of = -1;
        std::int32_t Order = 0;
        std::int32_t Frame = 0;
        // "add" (Quake's GL_ONE GL_ONE, drawn here as see-through as it is
        // dark), "alpha" (by the image's alpha), or "" (opaque).
        std::string Blend;
        // Whether the level's light falls on it: a layer Quake draws before
        // its lightmap stage is lit, a glow drawn after it is not.
        bool Lit = true;
        bool Clamp = false;
        // cull none: seen from both sides, as a flame or a grate is.
        bool TwoSided = false;
        std::vector<TcMod> TcMods;
        // rgbGen wave or alphaGen wave: how strongly it shows, over time.
        std::optional<Wave> Fade;
        // animMap: frames a second and how many.
        float FrameRate = 0.0F;
        std::int32_t FrameCount = 0;
        // A sky's base drawn as Quake's cloud layers (skyparms' cloudheight):
        // over a dome that high, not over the level's sky surfaces.
        float CloudHeight = 0.0F;

        [[nodiscard]] bool Animated() const;
        [[nodiscard]] bool MovesTexcoords() const;
    };

    [[nodiscard]] std::string Encode(const EntryMeta& meta);
    [[nodiscard]] EntryMeta Decode(const std::string& text);

    // Quake's waveforms (tr_shade_calc.c's EvalWaveForm).
    [[nodiscard]] float WaveValue(const Wave& wave, float seconds);

    // A stage's tcMods at a time, in the terms a room's texcoord animation
    // has (Model::AnimateTexcoords): scaled, turned about the image's middle,
    // then moved.
    struct TexcoordFrame
    {
        float ScaleS = 1.0F;
        float ScaleT = 1.0F;
        float Rotate = 0.0F;
        float TranslateS = 0.0F;
        float TranslateT = 0.0F;
    };
    [[nodiscard]] TexcoordFrame Texcoords(const std::vector<TcMod>& mods, float seconds);

    // A rate rounded to whole cycles of the loop; cycle is how much of it
    // makes one (1 for a scroll or a frequency, 360 for degrees a second).
    [[nodiscard]] float Looped(float perSecond, float cycle = 1.0F);
}
