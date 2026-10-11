#pragma once

// stb_image / stb_image_write, as the managed build reaches them through
// ReFuel.Stb. Only PNG is implemented, because only PNG is asked for: the
// screenshots the game writes and the textures a converted map reads.
//
// The two `stbi_*` names keep stb's own signatures, because the call sites
// that were written against stb spell them that way.

#include <cstdint>
#include <string>
#include <vector>

extern "C"
{
    using StbiWriteFunc = void (*)(void* context, void* data, int size);

    // stbi_flip_vertically_on_write(flag).
    void stbi_flip_vertically_on_write(int flag);
    // stbi_write_png_to_func(func, context, w, h, comp, data, stride_bytes).
    int stbi_write_png_to_func(StbiWriteFunc func, void* context, int w, int h,
        int comp, const void* data, int stride_bytes);
}

namespace MphRead::NativeRuntime
{
    struct Image final
    {
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        // Components per pixel, as read.
        std::int32_t Components = 0;
        std::vector<std::uint8_t> Pixels;
    };

    // stbi_load_from_memory(..., desiredComponents): an empty image when the
    // bytes are not a PNG this decoder understands.
    [[nodiscard]] Image LoadPng(
        const std::vector<std::uint8_t>& bytes, std::int32_t desiredComponents);

    // PNG through LoadPng, JPEG and TGA through stb_image (ImageDecode.cpp):
    // what a Quake 3 level's textures are made of. An empty image for
    // anything else.
    [[nodiscard]] Image LoadImage(
        const std::vector<std::uint8_t>& bytes, std::int32_t desiredComponents);
}
