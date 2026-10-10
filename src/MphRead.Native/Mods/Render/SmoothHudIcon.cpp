#include "SmoothHudIcon.hpp"

#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;

namespace
{
    [[nodiscard]] MphRead::ColorRgba& TextureAt(
        std::vector<MphRead::ColorRgba>& texture, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= texture.size())
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return texture[static_cast<std::size_t>(index)];
    }
}

namespace MphRead::Mods::Render
{
    std::shared_ptr<Hud::HudObjectInstance> SmoothHudIcon::Create(
        const std::shared_ptr<Hud::HudObject>& sheet)
    {
        if (!sheet)
        {
            throw System::NullReferenceException();
        }
        auto inst = std::make_shared<Hud::HudObjectInstance>(
            sheet->Width, sheet->Height,
            UncheckedMultiply(sheet->Width, Factor), UncheckedMultiply(sheet->Height, Factor));
        inst->Smooth = true;
        inst->Enabled = true;
        // Kept for Build's shading only; nothing is bound from it.
        inst->PaletteData = sheet->PaletteData;
        return inst;
    }

    void SmoothHudIcon::Tint(const std::shared_ptr<Hud::HudObjectInstance>& inst,
        Hud::ReadOnlyList<std::uint8_t> data, std::int32_t frame,
        ColorRgba color, Scene& scene)
    {
        if (!inst)
        {
            throw System::NullReferenceException();
        }
        if (inst->CharacterData == data && inst->CurrentFrame == frame
            && inst->Color.has_value() && inst->Color.value() == color
            && inst->BindingId != -1)
        {
            return;
        }
        inst->CharacterData = data;
        inst->CurrentFrame = frame;
        inst->Color = color;
        inst->PaletteIndex = -1;
        Build(*inst, data, frame, color, scene);
    }

    void SmoothHudIcon::Build(Hud::HudObjectInstance& inst,
        const Hud::ReadOnlyList<std::uint8_t>& data, std::int32_t frame,
        ColorRgba color, Scene& scene)
    {
        const std::int32_t width = inst.Width;
        const std::int32_t height = inst.Height;
        const std::int32_t outWidth = UncheckedMultiply(width, Factor);
        const std::int32_t outHeight = UncheckedMultiply(height, Factor);
        std::vector<ColorRgba>& texture = *inst.Texture;
        const std::int32_t requiredLength = UncheckedMultiply(outWidth, outHeight);
        if (static_cast<std::int32_t>(texture.size()) < requiredLength)
        {
            return;
        }
        const std::int32_t tilesX = width / 8;
        const std::int32_t image = UncheckedMultiply(UncheckedMultiply(frame, width), height);
        const ColorRgba transparent{};
        // Each pixel keeps the brightness its own palette colour has against
        // the brightest one in the picture. The DS draws these icons as a lit
        // outline round a dark body; one flat tint for every non-transparent
        // pixel filled the body in, and the icons read as fat blobs.
        float brightest = 0.0F;
        for (std::int32_t y = 0; y < height; ++y)
        {
            for (std::int32_t x = 0; x < width; ++x)
            {
                brightest = std::max(brightest, Shade(inst, data, image, tilesX, width, height, x, y));
            }
        }
        for (std::int32_t y = 0; y < outHeight; ++y)
        {
            const std::int32_t sourceY = y / Factor;
            for (std::int32_t x = 0; x < outWidth; ++x)
            {
                ColorRgba& target = TextureAt(
                    texture, UncheckedAdd(UncheckedMultiply(y, outWidth), x));
                const float shade = Shade(inst, data, image, tilesX, width, height, x / Factor, sourceY);
                if (shade <= 0.0F)
                {
                    target = transparent;
                    continue;
                }
                const float level = brightest > 0.0F ? std::min(1.0F, shade / brightest) : 1.0F;
                const auto channel = [level](std::uint8_t value)
                { return static_cast<std::uint8_t>(static_cast<float>(value) * level + 0.5F); };
                target = ColorRgba(channel(color.Red), channel(color.Green), channel(color.Blue), 255);
            }
        }
        inst.BindPicture(scene, outWidth, outHeight);
    }

    float SmoothHudIcon::Shade(const Hud::HudObjectInstance& inst,
        const Hud::ReadOnlyList<std::uint8_t>& data, std::int32_t image,
        std::int32_t tilesX, std::int32_t width, std::int32_t height,
        std::int32_t x, std::int32_t y)
    {
        const std::int32_t index = Index(data, image, tilesX, width, height, x, y);
        if (index <= 0)
        {
            return 0.0F;
        }
        // Without a palette every inked pixel is full strength, as before.
        if (!inst.PaletteData || static_cast<std::size_t>(index) >= inst.PaletteData->size())
        {
            return 1.0F;
        }
        const ColorRgba& colour = inst.PaletteData->at(static_cast<std::size_t>(index));
        // Never fully dark: a black pixel inside the icon is still the icon.
        return static_cast<float>(std::max<int>({colour.Red, colour.Green, colour.Blue, 24}));
    }

    std::int32_t SmoothHudIcon::Index(const Hud::ReadOnlyList<std::uint8_t>& data,
        std::int32_t image, std::int32_t tilesX, std::int32_t width,
        std::int32_t height, std::int32_t x, std::int32_t y)
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
        {
            return 0;
        }
        std::int32_t index = image;
        index = UncheckedAdd(index, UncheckedMultiply(UncheckedMultiply(y / 8, tilesX), 64));
        index = UncheckedAdd(index, UncheckedMultiply(x / 8, 64));
        index = UncheckedAdd(index, UncheckedMultiply(y % 8, 8));
        index = UncheckedAdd(index, x % 8);
        if (index < 0)
        {
            return 0;
        }
        if (!data)
        {
            throw System::NullReferenceException();
        }
        if (static_cast<std::size_t>(index) >= data->size())
        {
            return 0;
        }
        return data->at(static_cast<std::size_t>(index));
    }
}
