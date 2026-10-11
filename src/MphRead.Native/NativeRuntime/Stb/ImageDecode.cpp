#include "Image.hpp"

// stb_image itself, for the formats Image.cpp does not read: a Quake 3
// level's textures are JPEG and TGA, and the managed build decoded them with
// stb through ReFuel.Stb. Static, and limited to those two, so nothing of it
// is visible outside this file.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <climits>
#include <cstring>

namespace MphRead::NativeRuntime
{
    Image LoadImage(const std::vector<std::uint8_t>& bytes, std::int32_t desiredComponents)
    {
        static constexpr std::uint8_t PngSignature[] = {0x89, 'P', 'N', 'G'};
        if (bytes.size() >= sizeof(PngSignature)
            && std::memcmp(bytes.data(), PngSignature, sizeof(PngSignature)) == 0)
        {
            return LoadPng(bytes, desiredComponents);
        }
        if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX))
        {
            return Image();
        }

        int width = 0;
        int height = 0;
        int components = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()),
            &width, &height, &components, desiredComponents);
        if (pixels == nullptr)
        {
            return Image();
        }
        Image image;
        image.Width = width;
        image.Height = height;
        image.Components = desiredComponents != 0 ? desiredComponents : components;
        image.Pixels.assign(pixels,
            pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                * static_cast<std::size_t>(image.Components));
        stbi_image_free(pixels);
        return image;
    }
}
