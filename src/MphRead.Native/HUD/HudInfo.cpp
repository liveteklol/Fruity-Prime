#include "HudInfo.hpp"

#include "../Export/Images.hpp"
#include "../Read.hpp"
#include "../Scene.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::FileReadAllBytes;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::RoundToEven;
using ::MphRead::NativeRuntime::StringReplace;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace
{
    using MphRead::ColorRgba;

    template <typename T>
    std::shared_ptr<const std::vector<T>> MakeReadOnlyList(std::vector<T> values)
    {
        return std::make_shared<const std::vector<T>>(std::move(values));
    }

    template <typename>
    inline constexpr bool UnsupportedReadType = false;

    template <typename T>
    [[nodiscard]] T ReadAt(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        if constexpr (std::is_same_v<T, std::uint8_t>)
        {
            if (offset >= bytes.size())
            {
                throw std::out_of_range("Index was outside the bounds of the array.");
            }
            return bytes[offset];
        }
        else if constexpr (std::is_same_v<T, std::uint16_t>)
        {
            if (offset > bytes.size() || 2 > bytes.size() - offset)
            {
                throw std::out_of_range("Index was outside the bounds of the array.");
            }
            return static_cast<std::uint16_t>(bytes[offset])
                | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        }
        else if constexpr (std::is_same_v<T, std::int32_t>)
        {
            if (offset > bytes.size() || 4 > bytes.size() - offset)
            {
                throw std::out_of_range("Index was outside the bounds of the array.");
            }
            const std::uint32_t value = static_cast<std::uint32_t>(bytes[offset])
                | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
                | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
            return std::bit_cast<std::int32_t>(value);
        }
        else if constexpr (std::is_same_v<T, MphRead::Hud::UiAnimParams>)
        {
            return T{ReadAt<std::uint8_t>(bytes, offset), ReadAt<std::uint8_t>(bytes, offset + 1),
                ReadAt<std::uint16_t>(bytes, offset + 2), ReadAt<std::int32_t>(bytes, offset + 4),
                ReadAt<std::uint16_t>(bytes, offset + 8), ReadAt<std::uint16_t>(bytes, offset + 10),
                ReadAt<std::uint16_t>(bytes, offset + 12), ReadAt<std::uint16_t>(bytes, offset + 14)};
        }
        else if constexpr (std::is_same_v<T, MphRead::Hud::HudInfo::UiPartHeader>)
        {
            return T{ReadAt<std::int32_t>(bytes, offset), ReadAt<std::int32_t>(bytes, offset + 4),
                ReadAt<std::int32_t>(bytes, offset + 8)};
        }
        else if constexpr (std::is_same_v<T, MphRead::Hud::HudInfo::ScrDatInfo>)
        {
            return T{ReadAt<std::uint16_t>(bytes, offset), ReadAt<std::uint16_t>(bytes, offset + 2),
                ReadAt<std::int32_t>(bytes, offset + 4)};
        }
        else if constexpr (std::is_same_v<T, MphRead::Hud::HudInfo::UiObjectHeader>)
        {
            return T{ReadAt<std::uint16_t>(bytes, offset), ReadAt<std::uint16_t>(bytes, offset + 2),
                ReadAt<std::uint16_t>(bytes, offset + 4), ReadAt<std::uint16_t>(bytes, offset + 6),
                ReadAt<std::int32_t>(bytes, offset + 8), ReadAt<std::int32_t>(bytes, offset + 12),
                ReadAt<std::int32_t>(bytes, offset + 16), ReadAt<std::int32_t>(bytes, offset + 20)};
        }
        else if constexpr (std::is_same_v<T, MphRead::Hud::HudInfo::RawUiOamAttrs>)
        {
            return T{ReadAt<std::uint16_t>(bytes, offset), ReadAt<std::uint16_t>(bytes, offset + 2),
                ReadAt<std::uint16_t>(bytes, offset + 4), ReadAt<std::uint16_t>(bytes, offset + 6)};
        }
        else
        {
            static_assert(UnsupportedReadType<T>, "Unsupported HUD binary read type.");
        }
    }

    template <typename T>
    [[nodiscard]] std::vector<T> ReadMany(
        const std::vector<std::uint8_t>& bytes, std::int32_t offset, std::int32_t count)
    {
        if (count < 0)
        {
            throw std::out_of_range("Index was outside the bounds of the array.");
        }
        std::vector<T> result;
        result.reserve(static_cast<std::size_t>(count));
        if (offset == 0)
        {
            return result;
        }
        std::int32_t current = offset;
        for (std::int32_t i = 0; i < count; ++i)
        {
            if (current < 0)
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }
            result.push_back(ReadAt<T>(bytes, static_cast<std::size_t>(current)));
            current = std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(current) + static_cast<std::uint32_t>(sizeof(T)));
        }
        return result;
    }

    template <typename T>
    [[nodiscard]] T ReadOffset(
        const std::vector<std::uint8_t>& bytes, std::int32_t offset)
    {
        std::vector<T> values = ReadMany<T>(bytes, offset, 1);
        if (values.empty())
        {
            throw std::out_of_range("Index was outside the bounds of the array.");
        }
        return values.front();
    }

    [[nodiscard]] std::size_t ArrayLength(std::int32_t length)
    {
        if (length < 0)
        {
            throw std::overflow_error("Arithmetic operation resulted in an overflow.");
        }
        return static_cast<std::size_t>(length);
    }

    [[nodiscard]] std::string LastPathPartWithoutExtension(const std::string& path)
    {
        const std::size_t slash = path.find_last_of('/');
        const std::size_t start = slash == std::string::npos ? 0 : slash + 1;
        const std::size_t dot = path.find('.', start);
        return path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    }

    [[nodiscard]] std::string PadLeft(std::int32_t value, std::size_t width)
    {
        std::string text = std::to_string(value);
        if (text.size() < width)
        {
            text.insert(text.begin(), width - text.size(), '0');
        }
        return text;
    }
}

namespace MphRead::Hud
{
    static_assert(sizeof(UiAnimParams) == 16);
    static_assert(sizeof(HudInfo::UiPartHeader) == 12);
    static_assert(sizeof(HudInfo::ScrDatInfo) == 8);
    static_assert(sizeof(HudInfo::UiObjectHeader) == 24);
    static_assert(sizeof(HudInfo::RawUiOamAttrs) == 8);

    const std::int32_t HudInfo::_layerHeaderSize = static_cast<std::int32_t>(sizeof(UiPartHeader));
    const std::int32_t HudInfo::_scrDatInfoSize = static_cast<std::int32_t>(sizeof(ScrDatInfo));
    const HudInfo::ObjectDimensions HudInfo::_objectDimensions{{
        {{{1, 1}, {2, 2}, {4, 4}, {8, 8}}},
        {{{2, 1}, {4, 1}, {4, 2}, {8, 4}}},
        {{{1, 2}, {1, 4}, {2, 4}, {4, 8}}}
    }};
    const std::int32_t HudInfo::_objHeaderSize = static_cast<std::int32_t>(sizeof(UiObjectHeader));
    const std::int32_t HudInfo::_animParamSize = static_cast<std::int32_t>(sizeof(UiAnimParams));
    const std::int32_t HudInfo::_oamAttrSize = static_cast<std::int32_t>(sizeof(RawUiOamAttrs));

    UiAnimParams& UiAnimParams::operator=(const UiAnimParams& other) noexcept
    {
        if (this != &other)
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    HudInfo::UiPartHeader& HudInfo::UiPartHeader::operator=(const UiPartHeader& other) noexcept
    {
        if (this != &other)
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    HudInfo::ScrDatInfo& HudInfo::ScrDatInfo::operator=(const ScrDatInfo& other) noexcept
    {
        if (this != &other)
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    HudInfo::UiObjectHeader& HudInfo::UiObjectHeader::operator=(const UiObjectHeader& other) noexcept
    {
        if (this != &other)
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    HudInfo::RawUiOamAttrs& HudInfo::RawUiOamAttrs::operator=(const RawUiOamAttrs& other) noexcept
    {
        if (this != &other)
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    HudObject::HudObject(std::int32_t width, std::int32_t height,
        ReadOnlyList<std::uint8_t> characterData,
        ReadOnlyList<ColorRgba> paletteData,
        ReadOnlyList<UiAnimParams> animParams)
        : Width(width), Height(height), CharacterData(std::move(characterData)),
          PaletteData(std::move(paletteData)), AnimParams(std::move(animParams))
    {
    }

    HudObjectInstance::HudObjectInstance(std::int32_t width, std::int32_t height)
        : Width(width), Height(height),
          Texture(std::make_shared<std::vector<ColorRgba>>(
              ArrayLength(UncheckedMultiply(width, height))))
    {
    }

    HudObjectInstance::HudObjectInstance(std::int32_t width, std::int32_t height,
        std::int32_t maxWidth, std::int32_t maxHeight)
        : Width(width), Height(height),
          Texture(std::make_shared<std::vector<ColorRgba>>(
              ArrayLength(UncheckedMultiply(maxWidth, maxHeight))))
    {
    }

    void HudObjectInstance::SetAnimationFrames(ReadOnlyList<UiAnimParams> frames)
    {
        std::vector<std::int32_t> list;
        if (!frames)
        {
            throw System::NullReferenceException();
        }
        for (std::size_t i = 0; i < frames->size(); ++i)
        {
            const UiAnimParams& frame = frames->at(i);
            for (std::int32_t j = 0; j < frame.Delay; ++j)
            {
                list.push_back(frame.ImageIndex);
            }
        }
        AnimFrames = MakeReadOnlyList(std::move(list));
    }

    void HudObjectInstance::SetCharacterData(ReadOnlyList<std::uint8_t> data,
        std::int32_t width, std::int32_t height, Scene& scene)
    {
        assert(static_cast<std::int64_t>(UncheckedMultiply(Width, Height))
            <= static_cast<std::int64_t>(Texture->size()));
        Width = width;
        Height = height;
        CharacterData.reset();
        SetCharacterData(std::move(data), scene);
    }

    void HudObjectInstance::SetCharacterData(ReadOnlyList<std::uint8_t> data, Scene& scene)
    {
        SetCharacterData(std::move(data), 0, scene);
    }

    void HudObjectInstance::SetCharacterData(ReadOnlyList<std::uint8_t> data,
        std::int32_t frame, Scene& scene)
    {
        if (CharacterData != data)
        {
            CharacterData = std::move(data);
            CurrentFrame = frame;
            Timer = 0.0F;
            if (PaletteData)
            {
                DoTexture(scene);
            }
        }
    }

    void HudObjectInstance::SetPaletteData(ReadOnlyList<ColorRgba> data, Scene& scene)
    {
        Color.reset();
        if (PaletteData != data)
        {
            PaletteData = std::move(data);
            PaletteIndex = 0;
            if (CharacterData)
            {
                DoTexture(scene);
            }
        }
    }

    void HudObjectInstance::SetPalette(std::int32_t index, Scene& scene)
    {
        Color.reset();
        const std::int32_t previous = PaletteIndex;
        PaletteIndex = index;
        if (CharacterData && index != previous)
        {
            DoTexture(scene);
        }
    }

    void HudObjectInstance::SetData(ReadOnlyList<std::uint8_t> charData,
        std::int32_t charFrame, ReadOnlyList<ColorRgba> palData,
        std::int32_t palIndex, Scene& scene)
    {
        Color.reset();
        Timer = 0.0F;
        CharacterData = std::move(charData);
        CurrentFrame = charFrame;
        PaletteData = std::move(palData);
        PaletteIndex = palIndex;
        DoTexture(scene);
    }

    void HudObjectInstance::SetData(std::int32_t charFrame,
        std::int32_t palIndex, Scene& scene)
    {
        Color.reset();
        Timer = 0.0F;
        const std::int32_t previousChar = CurrentFrame;
        const std::int32_t previousPal = PaletteIndex;
        CurrentFrame = charFrame;
        PaletteIndex = palIndex;
        if (CharacterData && PaletteData
            && (charFrame != previousChar || palIndex != previousPal))
        {
            DoTexture(scene);
        }
    }

    void HudObjectInstance::SetData(std::int32_t charFrame, ColorRgba color, Scene& scene)
    {
        PaletteIndex = -1;
        Timer = 0.0F;
        const std::int32_t previousChar = CurrentFrame;
        const std::optional<ColorRgba> previousColor = Color;
        CurrentFrame = charFrame;
        Color = color;
        if (CharacterData && PaletteData
            && (charFrame != previousChar || previousColor != Color))
        {
            DoTexture(scene);
        }
    }

    std::size_t HudObjectInstance::SourceKeyHash::operator()(const SourceKey& key) const noexcept
    {
        std::size_t hash = std::hash<const void*>{}(key.Characters);
        const auto mix = [&hash](std::size_t value) { hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2); };
        mix(std::hash<const void*>{}(key.Palette));
        mix(static_cast<std::size_t>(static_cast<std::uint32_t>(key.Frame)));
        mix(static_cast<std::size_t>(static_cast<std::uint32_t>(key.PaletteIndex)));
        mix(key.HasColor ? key.Color : 0x5A5A5A5AU);
        mix(static_cast<std::size_t>(static_cast<std::uint32_t>(key.Width) << 16 ^ static_cast<std::uint32_t>(key.Height)));
        return hash;
    }

    void HudObjectInstance::DoTexture(Scene& scene)
    {
        assert(CharacterData);
        assert(PaletteData);
        if (_pictureScene != &scene)
        {
            _sources.clear();
        }
        const SourceKey source{CharacterData.get(), Color.has_value() ? nullptr : PaletteData.get(), CurrentFrame,
            Color.has_value() ? -1 : PaletteIndex, Color.has_value() ? Color->ToUint() : 0U, Color.has_value(),
            Width, Height};
        if (const auto found = _sources.find(source); found != _sources.end())
        {
            BindingId = found->second.BindingId;
            return;
        }
        const std::int32_t paletteOffset = UncheckedMultiply(PaletteIndex, 16);
        const std::int32_t width = Width / 8;
        const std::int32_t height = Height / 8;
        const std::int32_t image = UncheckedMultiply(UncheckedMultiply(CurrentFrame, Width), Height);
        for (std::int32_t tileY = 0; tileY < height; ++tileY)
        {
            for (std::int32_t tileX = 0; tileX < width; ++tileX)
            {
                const std::int32_t start = UncheckedAdd(UncheckedMultiply(UncheckedMultiply(UncheckedMultiply(tileY, width), 8), 8), UncheckedMultiply(tileX, 8));
                for (std::int32_t pixelY = 0; pixelY < 8; ++pixelY)
                {
                    for (std::int32_t pixelX = 0; pixelX < 8; ++pixelX)
                    {
                        if (!CharacterData)
                        {
                            throw System::NullReferenceException();
                        }
                        const std::uint8_t paletteIndex = CharacterData->at(
                            static_cast<std::size_t>(UncheckedAdd(image, UncheckedAdd(
                                UncheckedMultiply(UncheckedMultiply(UncheckedMultiply(tileY, width), 8), 8),
                                UncheckedAdd(UncheckedMultiply(UncheckedMultiply(tileX, 8), 8),
                                    UncheckedAdd(UncheckedMultiply(pixelY, 8), pixelX))))));
                        const std::int32_t index = UncheckedAdd(start, UncheckedAdd(UncheckedMultiply(UncheckedMultiply(pixelY, width), 8), pixelX));
                        if (paletteIndex == 0)
                        {
                            Texture->at(static_cast<std::size_t>(index)) = ColorRgba();
                        }
                        else if (Color.has_value())
                        {
                            Texture->at(static_cast<std::size_t>(index)) = *Color;
                        }
                        else
                        {
                            if (!PaletteData)
                            {
                                throw System::NullReferenceException();
                            }
                            Texture->at(static_cast<std::size_t>(index)) = PaletteData->at(
                                static_cast<std::size_t>(UncheckedAdd(
                                    paletteOffset, static_cast<std::int32_t>(paletteIndex))));
                        }
                    }
                }
            }
        }
        BindPicture(scene, Width, Height);
        // A picture past the cache's cap shares the overflow binding, which
        // the next one rewrites: only a binding of its own can be reused.
        if (BindingId != _overflowBinding && _sources.size() < MaxCachedPictures)
        {
            _sources.emplace(source, SourceEntry{CharacterData, PaletteData, BindingId});
        }
    }

    void HudObjectInstance::BindPicture(Scene& scene, std::int32_t width, std::int32_t height)
    {
        if (_pictureScene != &scene)
        {
            _pictures.clear();
            _overflowBinding = -1;
            _pictureScene = &scene;
        }
        const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::string key(sizeof(std::int32_t) * 2 + pixels * sizeof(std::uint32_t), '\0');
        std::memcpy(key.data(), &width, sizeof(std::int32_t));
        std::memcpy(key.data() + sizeof(std::int32_t), &height, sizeof(std::int32_t));
        for (std::size_t i = 0; i < pixels && i < Texture->size(); ++i)
        {
            const std::uint32_t value = (*Texture)[i].ToUint();
            std::memcpy(key.data() + sizeof(std::int32_t) * 2 + i * sizeof(std::uint32_t), &value, sizeof(value));
        }
        if (const auto found = _pictures.find(key); found != _pictures.end())
        {
            BindingId = found->second;
            return;
        }
        if (_pictures.size() < MaxCachedPictures)
        {
            BindingId = scene.BindGetTexture(*Texture, width, height);
            _pictures.emplace(std::move(key), BindingId);
            return;
        }
        if (_overflowBinding == -1)
        {
            _overflowBinding = scene.BindGetTexture(*Texture, width, height);
        }
        else
        {
            scene.BindTexture(*Texture, width, height, _overflowBinding);
        }
        BindingId = _overflowBinding;
    }

    void HudObjectInstance::SetIndex(std::int32_t frame, Scene& scene)
    {
        const std::int32_t previous = CurrentFrame;
        CurrentFrame = frame;
        Timer = 0.0F;
        if (frame != previous)
        {
            DoTexture(scene);
        }
    }

    void HudObjectInstance::SetAnimation(std::int32_t start, std::int32_t target,
        std::int32_t frames, bool loop)
    {
        if (start == target)
        {
            CurrentFrame = start;
        }
        else
        {
            StartFrame = start;
            TargetFrame = target;
            Timer = Time = static_cast<float>(frames) * (1.0F / 30.0F);
            Loop = loop ? HudObjectLoopType::Start : HudObjectLoopType::None;
        }
    }

    void HudObjectInstance::SetAnimation(std::int32_t start, std::int32_t target,
        std::int32_t frames, std::int32_t afterAnim, bool loop)
    {
        SetAnimation(start, target, frames, afterAnim,
            loop ? HudObjectLoopType::Start : HudObjectLoopType::None);
    }

    void HudObjectInstance::SetAnimation(std::int32_t start, std::int32_t target,
        std::int32_t frames, std::int32_t afterAnim, HudObjectLoopType loopType)
    {
        assert(AnimFrames);
        if (start == target)
        {
            CurrentFrame = start;
        }
        else
        {
            StartFrame = start;
            TargetFrame = target;
            Timer = Time = static_cast<float>(frames) * (1.0F / 30.0F);
            if (loopType == HudObjectLoopType::Offset)
            {
                AfterAnimFrame = afterAnim;
            }
            else
            {
                if (!AnimFrames)
                {
                    throw System::NullReferenceException();
                }
                AfterAnimFrame = AnimFrames->at(static_cast<std::size_t>(afterAnim));
            }
            Loop = loopType;
        }
    }

    void HudObjectInstance::ProcessAnimation(Scene& scene)
    {
        if (Timer > 0.0F)
        {
            const std::int32_t previous = CurrentFrame;
            Timer -= scene.FrameTime();
            if (Timer <= 0.0F)
            {
                if (Loop == HudObjectLoopType::Start)
                {
                    CurrentFrame = StartFrame;
                    Timer = Time;
                }
                else if (Loop == HudObjectLoopType::Offset)
                {
                    StartFrame = AfterAnimFrame;
                    Timer = Time;
                }
                else
                {
                    CurrentFrame = !AnimFrames
                        ? TargetFrame : AfterAnimFrame;
                }
            }
            else if (Loop == HudObjectLoopType::Offset)
            {
                const float elapsedTime = Time - Timer;
                const std::int32_t elapsedFrames = ConvertToInt32Net9(RoundToEven(elapsedTime * 30.0F));
                const std::int32_t frame = UncheckedAdd(StartFrame, elapsedFrames);
                assert(AnimFrames);
                if (!AnimFrames)
                {
                    throw System::NullReferenceException();
                }
                CurrentFrame = frame >= static_cast<std::int32_t>(AnimFrames->size())
                    ? 0 : AnimFrames->at(static_cast<std::size_t>(frame));
            }
            else
            {
                const std::int32_t frame = UncheckedAdd(StartFrame, ConvertToInt32Net9(RoundToEven(
                    static_cast<float>(UncheckedSubtract(TargetFrame, StartFrame)) * (1.0F - Timer / Time))));
                CurrentFrame = !AnimFrames
                    ? frame : AnimFrames->at(static_cast<std::size_t>(frame));
            }
            if (CurrentFrame != previous)
            {
                DoTexture(scene);
            }
        }
    }

    HudInfo::ScreenData::ScreenData(std::uint16_t data)
        : CharacterId(data & 0x3FF),
          FlipHorizontal((data & 0x400) != 0),
          FlipVertical((data & 0x800) != 0),
          PaletteId((data & 0xF000) >> 12)
    {
    }

    std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> HudInfo::CharMapToTexture(
        const std::string& path, Scene& scene,
        ReadOnlyList<std::uint16_t> paletteOverride, std::int32_t paletteId)
    {
        const std::vector<std::uint8_t> bytes
            = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), path));
        return CharMapToTexture(bytes, 0, 0, 0, 0, scene,
            std::move(paletteOverride), paletteId);
    }

    std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> HudInfo::CharMapToTexture(
        const std::string& path, std::int32_t startX, std::int32_t startY,
        std::int32_t tilesX, std::int32_t tilesY, Scene& scene,
        ReadOnlyList<std::uint16_t> paletteOverride, std::int32_t paletteId)
    {
        const std::vector<std::uint8_t> bytes
            = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), path));
        return CharMapToTexture(bytes, startX, startY, tilesX, tilesY, scene,
            std::move(paletteOverride), paletteId);
    }

    std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> HudInfo::CharMapToTexture(
        const std::vector<std::uint8_t>& bytes, std::int32_t startX, std::int32_t startY,
        std::int32_t tilesX, std::int32_t tilesY, Scene& scene,
        ReadOnlyList<std::uint16_t> paletteOverride, std::int32_t paletteId)
    {
        const UiPartHeader header = ReadAt<UiPartHeader>(bytes, 0);
        assert(header.Magic == 0);
        std::int32_t offset = _layerHeaderSize;
        std::vector<std::uint8_t> characterData = ReadMany<std::uint8_t>(
            bytes, offset, header.CharDataSize);
        offset = UncheckedAdd(offset, header.CharDataSize);
        assert(header.PalDataSize % 2 == 0);
        std::vector<std::uint16_t> paletteData = ReadMany<std::uint16_t>(
            bytes, offset, header.PalDataSize / 2);
        offset = UncheckedAdd(offset, header.PalDataSize);
        const ScrDatInfo info = ReadOffset<ScrDatInfo>(bytes, offset);
        offset = UncheckedAdd(offset, _scrDatInfoSize);
        assert(info.ScrDataSize % 2 == 0);
        const std::vector<std::uint16_t> screenValues = ReadMany<std::uint16_t>(
            bytes, offset, info.ScrDataSize / 2);
        std::vector<ScreenData> screenData;
        screenData.reserve(screenValues.size());
        for (std::uint16_t value : screenValues)
        {
            screenData.emplace_back(value);
        }
        offset = UncheckedAdd(offset, info.ScrDataSize);
        const std::vector<std::uint8_t> trailingData = ReadMany<std::uint8_t>(
            bytes, offset, UncheckedSubtract(static_cast<std::int32_t>(bytes.size()), offset));
        assert(std::all_of(trailingData.begin(), trailingData.end(),
            [](std::uint8_t value) { return value == 0; }));

        if (paletteOverride)
        {
            std::vector<std::uint16_t> newPalette;
            newPalette.push_back(paletteData.at(0));
            newPalette.push_back(paletteData.at(1));
            for (std::size_t i = 1; i < paletteOverride->size(); ++i)
            {
                newPalette.push_back(paletteOverride->at(i));
            }
            paletteData = std::move(newPalette);
        }

        std::int32_t paletteOffset = 0;
        if (paletteId != -1)
        {
            paletteOffset = UncheckedMultiply(paletteId, 16);
        }

        std::vector<std::vector<ColorRgba>> characters;
        assert(characterData.size() % 32 == 0);
        for (std::size_t i = 0; i < characterData.size() / 32; ++i)
        {
            std::vector<ColorRgba> character;
            character.reserve(64);
            for (std::int32_t y = 0; y < 8; ++y)
            {
                for (std::int32_t x = 0; x < 4; ++x)
                {
                    const std::uint8_t data = characterData.at(i * 32
                        + static_cast<std::size_t>(y * 4 + x));
                    const std::int32_t index1 = data & 0xF;
                    const std::int32_t index2 = (data & 0xF0) >> 4;
                    const auto addColor = [&](std::int32_t index)
                    {
                        ColorRgba color;
                        if (index != 0 && (paletteId == -1 || index != 6))
                        {
                            color = ColorRgba(paletteData.at(
                                static_cast<std::size_t>(UncheckedAdd(index, paletteOffset))));
                        }
                        character.push_back(color);
                    };
                    addColor(index1);
                    addColor(index2);
                }
            }
            characters.push_back(std::move(character));
        }

        if (paletteId != -1)
        {
            std::vector<ScreenData> newScreenData;
            newScreenData.reserve(screenData.size() + 32 * 10);
            for (std::int32_t i = 0; i < 32 * 10; ++i)
            {
                newScreenData.emplace_back(0);
            }
            newScreenData.insert(newScreenData.end(), screenData.begin(), screenData.end());
            if (paletteId == 4)
            {
                for (std::int32_t i = 10 * 32; i < 17 * 32; ++i)
                {
                    newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(0);
                }
            }
            else
            {
                for (std::int32_t i = 10 * 32; i < 13 * 32; ++i)
                {
                    const std::int32_t characterId
                        = newScreenData.at(static_cast<std::size_t>(i)).CharacterId;
                    if (characterId == 1 || characterId == 2 || characterId == 5)
                    {
                        newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(0);
                    }
                }
            }
            for (std::int32_t i = 16 * 32; i < 17 * 32; ++i)
            {
                newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(13);
            }
            for (std::int32_t i = 17 * 32; i < 21 * 32; ++i)
            {
                newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(15);
            }
            for (std::int32_t i = 21 * 32; i < 21 * 32 + 4; ++i)
            {
                newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(21);
            }
            for (std::int32_t i = 21 * 32 + 28; i < 22 * 32; ++i)
            {
                newScreenData.at(static_cast<std::size_t>(i)) = ScreenData(21);
            }
            screenData = std::move(newScreenData);
        }

        if (tilesX == 0)
        {
            tilesX = info.CharsX;
        }
        if (tilesY == 0)
        {
            tilesY = info.CharsY;
        }
        const std::uint16_t width = static_cast<std::uint16_t>(UncheckedMultiply(tilesX, 8));
        const std::uint16_t height = static_cast<std::uint16_t>(UncheckedMultiply(tilesY, 8));
        std::vector<ColorRgba> texture(ArrayLength(UncheckedMultiply(width, height)));
        for (std::int32_t cy = 0; cy < tilesY; ++cy)
        {
            const std::int32_t icy = UncheckedAdd(cy, startY);
            for (std::int32_t cx = 0; cx < tilesX; ++cx)
            {
                const std::int32_t icx = UncheckedAdd(cx, startX);
                const std::int32_t split = (icx / 32) == 1
                    ? UncheckedAdd(0x400, UncheckedSubtract(icx, 32)) : icx;
                const std::int32_t index = UncheckedAdd(
                    UncheckedMultiply(icy, info.CharsX > 32 ? info.CharsX / 2 : info.CharsX), split);
                const ScreenData& screen = screenData.at(static_cast<std::size_t>(index));
                assert(screen.PaletteId == 0);
                const std::vector<ColorRgba>& character
                    = characters.at(static_cast<std::size_t>(screen.CharacterId));
                const std::int32_t start = UncheckedAdd(UncheckedMultiply(UncheckedMultiply(UncheckedMultiply(cy, tilesX), 8), 8), UncheckedMultiply(cx, 8));
                for (std::int32_t py = 0; py < 8; ++py)
                {
                    const std::int32_t iy = screen.FlipVertical ? 7 - py : py;
                    for (std::int32_t px = 0; px < 8; ++px)
                    {
                        const std::int32_t ix = screen.FlipHorizontal ? 7 - px : px;
                        const ColorRgba pixel = character.at(
                            static_cast<std::size_t>(iy * 8 + ix));
                        texture.at(static_cast<std::size_t>(
                            UncheckedAdd(start, UncheckedAdd(UncheckedMultiply(UncheckedMultiply(py, tilesX), 8), px)))) = pixel;
                    }
                }
            }
        }
        const std::int32_t binding = scene.BindGetTexture(texture, width, height);
        return {binding, MakeReadOnlyList(std::move(paletteData))};
    }

    HudInfo::UiOamAttrs::UiOamAttrs(RawUiOamAttrs raw)
    {
        assert(raw.Padding6 == 0);
        YPos = static_cast<std::uint16_t>(raw.Attr0 & 0xFF);
        AffineEnable = (raw.Attr0 & (1 << 8)) != 0;
        DoubleSize = (raw.Attr0 & (1 << 9)) != 0;
        Type = static_cast<ObjType>((raw.Attr0 & (3 << 10)) >> 10);
        Mosaic = (raw.Attr0 & (1 << 12)) != 0;
        Colors = (raw.Attr0 & (1 << 13)) == 0 ? ObjColors::Color16 : ObjColors::Color256;
        Shape = static_cast<ObjShape>(raw.Attr0 >> 14);
        assert(Shape != ObjShape::Unused);
        XPos = static_cast<std::uint16_t>(raw.Attr1 & 0x1FF);
        if (AffineEnable)
        {
            AffineIndex = (raw.Attr1 & (0x1F << 9)) >> 9;
            FlipHorizontal = false;
            FlipVertical = false;
        }
        else
        {
            AffineIndex = -1;
            FlipHorizontal = (raw.Attr1 & (1 << 12)) != 0;
            FlipVertical = (raw.Attr1 & (1 << 13)) != 0;
        }
        Size = static_cast<ObjSize>(raw.Attr1 >> 14);
        CharacterId = raw.Attr2 & 0x3FF;
        Priority = static_cast<std::uint8_t>((raw.Attr2 & (3 << 10)) >> 10);
        if (Type == ObjType::Bitmap)
        {
            PaletteId = -1;
            Alpha = static_cast<std::uint8_t>(raw.Attr2 >> 12);
        }
        else
        {
            PaletteId = raw.Attr2 >> 12;
            Alpha = 0;
        }
        assert(!AffineEnable);
        assert(Type == ObjType::Normal);
        assert(Colors == ObjColors::Color16);
        assert((raw.Attr0 & 0x3FFF) == 0);
        assert((raw.Attr1 & 0x3FFF) == 0);
        assert((raw.Attr2 & 0xFFF) == 0);
    }

    std::shared_ptr<HudObject> HudInfo::GetHudObject(const std::string& file)
    {
        const std::vector<std::uint8_t> bytes
            = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), file));
        const UiObjectHeader header = ReadAt<UiObjectHeader>(bytes, 0);
        std::int32_t offset = _objHeaderSize;
        assert(header.ParamDataSize % _animParamSize == 0);
        std::int32_t count = header.ParamDataSize / _animParamSize;
        std::vector<UiAnimParams> animParams = ReadMany<UiAnimParams>(bytes, offset, count);
        offset = UncheckedAdd(offset, header.ParamDataSize);
        assert(header.AttrDataSize % _oamAttrSize == 0);
        count = header.AttrDataSize / _oamAttrSize;
        const std::vector<RawUiOamAttrs> rawOamAttrs
            = ReadMany<RawUiOamAttrs>(bytes, offset, count);
        offset = UncheckedAdd(offset, header.AttrDataSize);
        const std::vector<std::uint8_t> characterData
            = ReadMany<std::uint8_t>(bytes, offset, header.CharDataSize);
        offset = UncheckedAdd(offset, header.CharDataSize);
        assert(header.PalDataSize % 2 == 0);
        const std::vector<std::uint16_t> paletteData
            = ReadMany<std::uint16_t>(bytes, offset, header.PalDataSize / 2);
        assert(UncheckedAdd(offset, header.PalDataSize)
            == static_cast<std::int32_t>(bytes.size()));

        std::vector<std::uint8_t> paletteIndexData;
        paletteIndexData.reserve(characterData.size() * 2);
        for (std::uint8_t data : characterData)
        {
            paletteIndexData.push_back(static_cast<std::uint8_t>(data & 0xF));
            paletteIndexData.push_back(static_cast<std::uint8_t>((data & 0xF0) >> 4));
        }
        std::vector<ColorRgba> paletteColorData;
        paletteColorData.reserve(paletteData.size());
        for (std::uint16_t color : paletteData)
        {
            paletteColorData.emplace_back(color);
        }
        std::vector<UiOamAttrs> oamAttrs;
        oamAttrs.reserve(rawOamAttrs.size());
        for (RawUiOamAttrs raw : rawOamAttrs)
        {
            oamAttrs.emplace_back(raw);
        }
        const UiOamAttrs& attrs = oamAttrs.at(0);
        assert(!attrs.FlipHorizontal);
        assert(!attrs.FlipVertical);
        const auto [width, height] = _objectDimensions.at(static_cast<std::size_t>(attrs.Shape))
            .at(static_cast<std::size_t>(attrs.Size));
        return std::make_shared<HudObject>(width * 8, height * 8,
            MakeReadOnlyList(std::move(paletteIndexData)),
            MakeReadOnlyList(std::move(paletteColorData)),
            MakeReadOnlyList(std::move(animParams)));
    }

    void HudInfo::TestAnimation(ReadOnlyList<UiAnimParams> animParams,
        std::int32_t pInitial, std::int32_t pStart, std::int32_t pTimer,
        std::int32_t pTarget)
    {
        if (!animParams)
        {
            throw System::NullReferenceException();
        }
        std::int32_t frame = pInitial;
        std::int32_t timer = 0;
        std::int32_t target = 0;
        bool updating = false;

        const auto setAnim = [&]()
        {
            frame = UncheckedSubtract(pStart, 1);
            timer = UncheckedSubtract(pTimer, 1);
            target = pTarget;
            updating = true;
        };
        const auto processAnim = [&]()
        {
            if (updating)
            {
                frame = UncheckedAdd(frame, 1);
                if (frame == timer)
                {
                    if (target >= 0)
                    {
                        updating = false;
                        frame = UncheckedSubtract(target, 1);
                    }
                    else
                    {
                        frame = UncheckedSubtract(-1, target);
                    }
                }
            }
        };
        const auto checkThing = [&](std::int32_t pFrame)
            -> std::pair<std::int32_t, std::int32_t>
        {
            for (std::size_t i = 0; i < animParams->size(); ++i)
            {
                const std::int32_t value = animParams->at(i).Delay;
                if (pFrame < value)
                {
                    return {static_cast<std::int32_t>(i), animParams->at(i).ImageIndex};
                }
                pFrame = UncheckedSubtract(pFrame, value);
            }
            return {0, animParams->at(0).ImageIndex};
        };

        const auto [startNumber, startIndex] = checkThing(frame);
        std::cout << "start: " << startNumber << " --> " << startIndex << '\n';
        setAnim();
        std::int32_t frameCount = -1;
        while (updating)
        {
            frameCount = UncheckedAdd(frameCount, 1);
            processAnim();
            const auto [number, index] = checkThing(frame);
            std::cout << "f" << frameCount << ": " << number << " --> " << index << '\n';
            Nop();
        }
        std::cout << '\n';
        Nop();
    }

    void HudInfo::TestObjects(const std::optional<std::string>& filename,
        std::int32_t pInitial, std::int32_t pStart, std::int32_t pTimer,
        std::int32_t pTarget, bool exportImages)
    {
        std::vector<std::string> files{
            "_archives/spSamus/hud_msgBox.bin",
            "_archives/spSamus/message_spacer.bin",
            "_archives/spSamus/message_pickupframe.bin",
            "_archives/spSamus/message_pickups.bin",
            "_archives/spSamus/message_crystalpickup.bin",
            "_archives/spSamus/map_quit.bin",
            "_archives/spSamus/scan_lore.bin",
            "_archives/spSamus/scan_lore_dim.bin",
            "_archives/spSamus/scan_enemy.bin",
            "_archives/spSamus/scan_enemy_dim.bin",
            "_archives/spSamus/scan_object.bin",
            "_archives/spSamus/scan_object_dim.bin",
            "_archives/spSamus/scan_equipment.bin",
            "_archives/spSamus/scan_equipment_dim.bin",
            "_archives/spSamus/scan_red.bin",
            "_archives/spSamus/scan_red_dim.bin",
            "_archives/spSamus/scan_arrow.bin",
            "_archives/spSamus/scan_ok.bin",
            "_archives/spSamus/scan_select.bin",
            "_archives/spSamus/scan_corner.bin",
            "_archives/spSamus/scan_cornerSm.bin",
            "_archives/spSamus/scan_horizline.bin",
            "_archives/spSamus/scan_vertline.bin",
            "_archives/spSamus/obj_wnd.bin",
            "_archives/spSamus/hud_etank.bin",
            "_archives/spSamus/map_portal.bin",
            "_archives/spSamus/map_crystalbig.bin",
            "_archives/spSamus/map_art_1.bin",
            "_archives/spSamus/map_art_2.bin",
            "_archives/spSamus/map_art_3.bin",
            "_archives/spSamus/map_art_4.bin",
            "_archives/spSamus/map_art_5.bin",
            "_archives/spSamus/map_art_6.bin",
            "_archives/spSamus/map_art_7.bin",
            "_archives/spSamus/map_art_8.bin",
            "_archives/spSamus/map_crystalred.bin",
            "_archives/spSamus/map_legendOthers.bin",
            "_archives/spSamus/map_legendDoors.bin",
            "hud/rad_NodesOG.bin",
            "hud/rad_NodesRB.bin",
            "_archives/commonMP/radar_octolithLARGE.bin",
            "_archives/commonMP/radar_octolithSMALL.bin",
            "_archives/commonMP/hud_systemload.bin",
            "_archives/commonMP/wifi_strength.bin",
            "_archives/commonMP/stars.bin",
            "_archives/common/rad_radplyred.bin",
            "_archives/common/rad_key.bin",
            "_archives/common/hud_boost.bin",
            "_archives/common/hud_bombs.bin",
            "_archives/common/enemy_samus.bin",
            "_archives/common/enemy_kanden.bin",
            "_archives/common/enemy_noxus.bin",
            "_archives/common/enemy_spyre.bin",
            "_archives/common/enemy_sylux.bin",
            "_archives/common/enemy_trace.bin",
            "_archives/common/enemy_weavel.bin"
        };
        static const std::array<std::string_view, 7> localDirs{
            "localSamus", "localKanden", "localNox", "localSpire",
            "localSylux", "localTrace", "localWeavel"
        };
        static const std::array<std::string_view, 16> localFiles{
            "rad_wepsel.bin", "wepsel_icon.bin", "wepsel_extra.bin", "wepsel_box.bin",
            "wepsel_hotdot.bin", "hud_targetcircle.bin", "hud_snipercircle.bin",
            "hud_primehunter.bin", "cloaking.bin", "hud_damage.bin", "hud_weaponicon.bin",
            "hud_ammobar.bin", "hud_energybar.bin", "hud_energybar2.bin",
            "rad_lights.bin", "rad_ammobar.bin"
        };
        for (std::string_view dir : localDirs)
        {
            for (std::string_view file : localFiles)
            {
                files.push_back("_archives/" + std::string(dir) + "/" + std::string(file));
            }
        }

        const std::unordered_map<std::string, std::vector<std::int32_t>> exportPalettes{
            {"hud_etank", {0, 1, 2}},
            {"hud_ammobar", {0, 1, 2}},
            {"hud_energybar", {0, 1, 2}},
            {"hud_energybar2", {0, 1, 2}},
            {"hud_msgBox", {0, 2, 3}},
            {"message_spacer", {0, 2, 3}},
            {"rad_wepsel", {0, 1}}
        };
        if (filename.has_value())
        {
            files.clear();
            files.push_back(*filename);
        }

        ReadOnlyList<std::uint16_t> targetCirclePaletteColors{};
        ReadOnlyList<std::uint16_t> messageBoxPaletteColors{};
        for (const std::string& file : files)
        {
            const std::string name = LastPathPartWithoutExtension(file);
            const std::vector<std::uint8_t> bytes
                = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), file));
            const UiObjectHeader header = ReadAt<UiObjectHeader>(bytes, 0);
            std::int32_t offset = _objHeaderSize;
            assert(header.ParamDataSize % _animParamSize == 0);
            std::int32_t count = header.ParamDataSize / _animParamSize;
            assert(count == header.FrameCount);
            std::vector<UiAnimParams> animParamsValues = ReadMany<UiAnimParams>(bytes, offset, count);
            ReadOnlyList<UiAnimParams> animParams = MakeReadOnlyList(std::move(animParamsValues));
            offset = UncheckedAdd(offset, header.ParamDataSize);
            assert(header.AttrDataSize % _oamAttrSize == 0);
            count = header.AttrDataSize / _oamAttrSize;
            const std::vector<RawUiOamAttrs> rawOamAttrs = ReadMany<RawUiOamAttrs>(bytes, offset, count);
            std::vector<UiOamAttrs> oamAttrs;
            oamAttrs.reserve(rawOamAttrs.size());
            for (RawUiOamAttrs raw : rawOamAttrs)
            {
                oamAttrs.emplace_back(raw);
            }
            offset = UncheckedAdd(offset, header.AttrDataSize);
            const std::vector<std::uint8_t> characterData
                = ReadMany<std::uint8_t>(bytes, offset, header.CharDataSize);
            offset = UncheckedAdd(offset, header.CharDataSize);
            assert(header.PalDataSize % 2 == 0);
            std::vector<std::uint16_t> paletteValues
                = ReadMany<std::uint16_t>(bytes, offset, header.PalDataSize / 2);
            offset = UncheckedAdd(offset, header.PalDataSize);
            assert(offset == static_cast<std::int32_t>(bytes.size()));
            ReadOnlyList<std::uint16_t> paletteData = MakeReadOnlyList(std::move(paletteValues));

            if (name == "hud_targetcircle")
            {
                targetCirclePaletteColors = paletteData;
            }
            else if (name == "hud_snipercircle")
            {
                assert(targetCirclePaletteColors);
                paletteData = targetCirclePaletteColors;
            }
            else if (name == "hud_msgBox")
            {
                messageBoxPaletteColors = paletteData;
            }
            else if (name == "message_spacer")
            {
                assert(messageBoxPaletteColors);
                paletteData = messageBoxPaletteColors;
            }

            if (filename.has_value())
            {
                TestAnimation(animParams, pInitial, pStart, pTimer, pTarget);
            }

            std::vector<std::uint8_t> paletteIndexData;
            paletteIndexData.reserve(characterData.size() * 2);
            for (std::uint8_t data : characterData)
            {
                paletteIndexData.push_back(static_cast<std::uint8_t>(data & 0xF));
                paletteIndexData.push_back(static_cast<std::uint8_t>((data & 0xF0) >> 4));
            }
            std::vector<ColorRgba> paletteColorData;
            if (!paletteData)
            {
                throw System::NullReferenceException();
            }
            paletteColorData.reserve(paletteData->size());
            for (std::uint16_t color : *paletteData)
            {
                paletteColorData.emplace_back(color);
            }

            std::vector<std::vector<std::vector<ColorRgba>>> characters;
            assert(characterData.size() % 32 == 0);
            assert(paletteData->size() % 16 == 0);
            for (std::size_t p = 0; p < paletteData->size() / 16; ++p)
            {
                std::vector<std::vector<ColorRgba>> paletteCharacters;
                for (std::size_t i = 0; i < characterData.size() / 32; ++i)
                {
                    std::vector<ColorRgba> character;
                    character.reserve(64);
                    for (std::int32_t y = 0; y < 8; ++y)
                    {
                        for (std::int32_t x = 0; x < 4; ++x)
                        {
                            const std::uint8_t data = characterData.at(i * 32
                                + static_cast<std::size_t>(y * 4 + x));
                            const std::int32_t index1 = data & 0xF;
                            character.push_back(index1 == 0 ? ColorRgba()
                                : ColorRgba(paletteData->at(p * 16 + static_cast<std::size_t>(index1))));
                            const std::int32_t index2 = (data & 0xF0) >> 4;
                            character.push_back(index2 == 0 ? ColorRgba()
                                : ColorRgba(paletteData->at(p * 16 + static_cast<std::size_t>(index2))));
                        }
                    }
                    paletteCharacters.push_back(std::move(character));
                }
                characters.push_back(std::move(paletteCharacters));
            }

            const UiOamAttrs& attrs = oamAttrs.at(0);
            assert(attrs.CharacterId == 0);
            assert(!attrs.FlipHorizontal);
            assert(!attrs.FlipVertical);
            const auto [width, height] = _objectDimensions.at(static_cast<std::size_t>(attrs.Shape))
                .at(static_cast<std::size_t>(attrs.Size));
            const std::int32_t tiles = width * height;
            const std::uint16_t textureWidth = static_cast<std::uint16_t>(width * 8);
            const std::uint16_t textureHeight = static_cast<std::uint16_t>(height * 8);
            const std::int32_t size = textureWidth * textureHeight;
            for (std::int32_t i = 0; i < header.ImageCount; ++i)
            {
                std::vector<std::vector<std::uint16_t>> distinctPalettes;
                for (std::size_t p = 0; p < paletteData->size() / 16; ++p)
                {
                    std::vector<std::uint16_t> palette;
                    palette.reserve(16);
                    for (std::size_t j = 0; j < 16; ++j)
                    {
                        palette.push_back(paletteData->at(p * 16 + j));
                    }
                    if (std::find(distinctPalettes.begin(), distinctPalettes.end(), palette)
                        != distinctPalettes.end())
                    {
                        continue;
                    }
                    distinctPalettes.push_back(std::move(palette));
                    std::vector<ColorRgba> texture(ArrayLength(size));
                    for (std::int32_t tileY = 0; tileY < height; ++tileY)
                    {
                        for (std::int32_t tileX = 0; tileX < width; ++tileX)
                        {
                            const std::vector<ColorRgba>& character = characters.at(p).at(
                                static_cast<std::size_t>(i * tiles + tileY * width + tileX));
                            const std::int32_t start = tileY * width * 8 * 8 + tileX * 8;
                            for (std::int32_t pixelY = 0; pixelY < 8; ++pixelY)
                            {
                                for (std::int32_t pixelX = 0; pixelX < 8; ++pixelX)
                                {
                                    const ColorRgba pixel = character.at(
                                        static_cast<std::size_t>(pixelY * 8 + pixelX));
                                    texture.at(static_cast<std::size_t>(
                                        start + pixelY * width * 8 + pixelX)) = pixel;
                                }
                            }
                        }
                    }
                    if (exportImages)
                    {
                        bool skip = p != 0;
                        const auto found = exportPalettes.find(name);
                        if (found != exportPalettes.end())
                        {
                            skip = std::find(found->second.begin(), found->second.end(),
                                static_cast<std::int32_t>(p)) == found->second.end();
                        }
                        if (!skip)
                        {
                            std::string directory = StringReplace(file, "_archives/", "");
                            directory = StringReplace(std::move(directory), ".bin", "");
                            directory = Paths::Combine(Paths::Export(), "_2D/Objects",
                                directory, "pal_" + PadLeft(static_cast<std::int32_t>(p), 2));
                            std::filesystem::create_directories(PathFromUtf8(directory));
                            const std::string number = PadLeft(i, 3);
                            Export::Images::SaveTexture(directory, number,
                                textureWidth, textureHeight, texture);
                        }
                    }
                }
            }
            Nop();
        }
        Nop();
    }

    void HudInfo::TestLayers(bool exportScreens, bool exportChars)
    {
        std::vector<std::string> files;
        static const std::array<std::pair<std::string_view, std::string_view>, 7> hunters{{
            {"samus", "localSamus"}, {"kanden", "localKanden"}, {"nox", "localNox"},
            {"spire", "localSpire"}, {"sylux", "localSylux"}, {"trace", "localTrace"},
            {"weavel", "localWeavel"}
        }};
        static const std::array<std::string_view, 6> hudFiles{
            "bg_bottom.bin", "bg_bottomL.bin", "bg_altform.bin",
            "bg_altformL.bin", "bg_wepsel.bin", "bg_wepselL.bin"
        };
        static const std::array<std::string_view, 4> localFiles{
            "map_grid.bin", "bg_top_ovl.bin", "bg_top.bin", "bg_top_drop.bin"
        };
        for (const auto& [hudDir, localDir] : hunters)
        {
            for (std::string_view file : hudFiles)
            {
                files.push_back("hud/" + std::string(hudDir) + "/" + std::string(file));
            }
            for (std::string_view file : localFiles)
            {
                files.push_back("_archives/" + std::string(localDir) + "/" + std::string(file));
            }
        }
        files.push_back("_archives/spSamus/bg_scanjewel.bin");
        files.push_back("_archives/spSamus/map_scan.bin");
        files.push_back("_archives/common/bg_ice.bin");

        for (const std::string& file : files)
        {
            const std::vector<std::uint8_t> bytes
                = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), file));
            const UiPartHeader header = ReadAt<UiPartHeader>(bytes, 0);
            assert(header.Magic == 0);
            std::int32_t offset = _layerHeaderSize;
            const std::vector<std::uint8_t> characterData
                = ReadMany<std::uint8_t>(bytes, offset, header.CharDataSize);
            offset = UncheckedAdd(offset, header.CharDataSize);
            assert(header.PalDataSize % 2 == 0);
            const std::vector<std::uint16_t> paletteData
                = ReadMany<std::uint16_t>(bytes, offset, header.PalDataSize / 2);
            offset = UncheckedAdd(offset, header.PalDataSize);
            const ScrDatInfo info = ReadOffset<ScrDatInfo>(bytes, offset);
            offset = UncheckedAdd(offset, _scrDatInfoSize);
            assert(info.ScrDataSize % 2 == 0);
            const std::vector<std::uint16_t> screenDataValues
                = ReadMany<std::uint16_t>(bytes, offset, info.ScrDataSize / 2);
            assert(UncheckedMultiply(info.CharsX, info.CharsY) >= 0
                && static_cast<std::size_t>(UncheckedMultiply(info.CharsX, info.CharsY)) == screenDataValues.size());

            std::vector<std::uint16_t> vramStuff;
            const std::int32_t charSlot = UncheckedAdd(header.CharDataSize, 32) / 32;
            const std::int32_t palSlot = 1;
            const std::int32_t size = info.ScrDataSize;
            const std::int32_t count = size / 2;
            std::uint32_t minCharId = 1024;
            std::uint32_t minPalId = 16;
            for (std::int32_t i = 0; i < count; ++i)
            {
                const std::uint32_t data = screenDataValues.at(static_cast<std::size_t>(i));
                const std::uint32_t charId = data & 0x3FF;
                if (charId < minCharId)
                {
                    minCharId = charId;
                }
                const std::uint32_t palId = (data & 0xF000) >> 12;
                if (palId < minPalId)
                {
                    minPalId = palId;
                }
            }
            for (std::int32_t j = 0; j < count;)
            {
                const std::uint32_t value = screenDataValues.at(static_cast<std::size_t>(j++));
                const std::uint32_t currentFlip = value & 0xC00;
                const std::uint32_t currentPaletteId = (value & 0xF000) >> 12;
                const std::uint32_t currentCharacterId = value & 0x3FF;
                const std::int64_t newPaletteId = static_cast<std::int64_t>(palSlot)
                    + static_cast<std::int64_t>(currentPaletteId)
                    - static_cast<std::int64_t>(minPalId);
                const std::int64_t newCharacterId = static_cast<std::int64_t>(charSlot)
                    + static_cast<std::int64_t>(currentCharacterId)
                    - static_cast<std::int64_t>(minCharId);
                (void)newPaletteId;
                (void)newCharacterId;
                vramStuff.push_back(static_cast<std::uint16_t>(
                    currentCharacterId | currentFlip | (currentPaletteId << 12)));
            }

            std::vector<ScreenData> screenData;
            screenData.reserve(screenDataValues.size());
            for (std::uint16_t value : screenDataValues)
            {
                screenData.emplace_back(value);
            }

            std::vector<std::vector<ColorRgba>> characters;
            assert(characterData.size() % 32 == 0);
            for (std::size_t i = 0; i < characterData.size() / 32; ++i)
            {
                std::vector<ColorRgba> character;
                character.reserve(64);
                for (std::int32_t y = 0; y < 8; ++y)
                {
                    for (std::int32_t x = 0; x < 4; ++x)
                    {
                        const std::uint8_t data = characterData.at(i * 32
                            + static_cast<std::size_t>(y * 4 + x));
                        const std::int32_t index1 = data & 0xF;
                        character.push_back(index1 == 0 ? ColorRgba()
                            : ColorRgba(paletteData.at(static_cast<std::size_t>(index1))));
                        const std::int32_t index2 = (data & 0xF0) >> 4;
                        character.push_back(index2 == 0 ? ColorRgba()
                            : ColorRgba(paletteData.at(static_cast<std::size_t>(index2))));
                    }
                }
                characters.push_back(std::move(character));
            }

            const std::uint16_t width = static_cast<std::uint16_t>(info.CharsX * 8);
            const std::uint16_t height = static_cast<std::uint16_t>(info.CharsY * 8);
            std::vector<ColorRgba> texture(ArrayLength(UncheckedMultiply(width, height)));
            for (std::int32_t cy = 0; cy < info.CharsY; ++cy)
            {
                for (std::int32_t cx = 0; cx < info.CharsX; ++cx)
                {
                    const std::int32_t split = (cx / 32) == 1
                        ? UncheckedAdd(0x400, UncheckedSubtract(cx, 32)) : cx;
                    const std::int32_t index = UncheckedAdd(
                        UncheckedMultiply(cy, info.CharsX > 32 ? info.CharsX / 2 : info.CharsX), split);
                    const ScreenData& screen = screenData.at(static_cast<std::size_t>(index));
                    assert(screen.PaletteId == 0);
                    const std::vector<ColorRgba>& character
                        = characters.at(static_cast<std::size_t>(screen.CharacterId));
                    const std::int32_t start = UncheckedAdd(UncheckedMultiply(UncheckedMultiply(UncheckedMultiply(cy, info.CharsX), 8), 8), UncheckedMultiply(cx, 8));
                    for (std::int32_t py = 0; py < 8; ++py)
                    {
                        const std::int32_t iy = screen.FlipVertical ? 7 - py : py;
                        for (std::int32_t px = 0; px < 8; ++px)
                        {
                            const std::int32_t ix = screen.FlipHorizontal ? 7 - px : px;
                            texture.at(static_cast<std::size_t>(UncheckedAdd(start,
                                UncheckedAdd(UncheckedMultiply(UncheckedMultiply(py, info.CharsX), 8), px))))
                                = character.at(static_cast<std::size_t>(iy * 8 + ix));
                        }
                    }
                }
            }

            const std::string name = StringReplace(file, "/", "--");
            if (exportChars)
            {
                std::string directory = Paths::Combine(Paths::Export(), "_2D\\Layers", name);
                std::filesystem::create_directories(PathFromUtf8(directory));
                Export::Images::SaveTexture(directory, name, width, height, texture);
                directory = Paths::Combine(directory, "Characters");
                std::filesystem::create_directories(PathFromUtf8(directory));
                for (std::size_t i = 0; i < characters.size(); ++i)
                {
                    const std::string characterName = PadLeft(static_cast<std::int32_t>(i), 3);
                    Export::Images::SaveTexture(directory, characterName, 8, 8, characters[i]);
                }
            }
            else if (exportScreens)
            {
                const std::string directory = Paths::Combine(Paths::Export(), "_2D\\Layers");
                std::filesystem::create_directories(PathFromUtf8(directory));
                Export::Images::SaveTexture(directory, name, width, height, texture);
            }
            offset = UncheckedAdd(offset, info.ScrDataSize);
            const std::vector<std::uint8_t> trailingData = ReadMany<std::uint8_t>(
                bytes, offset, UncheckedSubtract(static_cast<std::int32_t>(bytes.size()), offset));
            assert(std::all_of(trailingData.begin(), trailingData.end(),
                [](std::uint8_t value) { return value == 0; }));
            Nop();
        }
        Nop();
    }

    void HudInfo::Nop()
    {
    }

    RulesInfo::RulesInfo(std::int32_t count, IntArray messageIds, IntArray offsets)
        : _count(count), _messageIds(std::move(messageIds)), _offsets(std::move(offsets))
    {
    }

    std::int32_t RulesInfo::Count() const noexcept
    {
        return _count;
    }

    const RulesInfo::IntArray& RulesInfo::MessageIds() const noexcept
    {
        return _messageIds;
    }

    const RulesInfo::IntArray& RulesInfo::Offsets() const noexcept
    {
        return _offsets;
    }

    HudObjects::HudObjects(std::string helmet, std::string helmetDrop, std::string visor,
        std::string scanVisor, std::string healthBarA, std::string healthBarB,
        std::optional<std::string> energyTanks, std::string weaponIcon,
        std::string doubleDamage, std::string cloaking, std::string primeHunter,
        std::string ammoBar, std::string reticle, std::string sniperReticle,
        std::string weaponSelect, std::string selectIcon, std::string selectBox,
        std::string damageBar, std::int32_t healthMainPosX,
        std::int32_t healthMainPosY, std::int32_t healthSubPosX,
        std::int32_t healthSubPosY, std::int32_t healthOffsetY,
        std::int32_t healthOffsetYAlt, std::int32_t ammoBarPosX,
        std::int32_t ammoBarPosY, std::int32_t weaponIconPosX,
        std::int32_t weaponIconPosY, std::int32_t enemyHealthPosX,
        std::int32_t enemyHealthPosY, std::int32_t enemyHealthTextPosX,
        std::int32_t enemyHealthTextPosY, std::int32_t scorePosX,
        std::int32_t scorePosY, Align scoreAlign, std::int32_t octolithPosX,
        std::int32_t octolithPosY, std::int32_t primePosX,
        std::int32_t primePosY, std::int32_t primeTextPosX,
        std::int32_t primeTextPosY, Align primeAlign,
        std::int32_t nodeBonusPosX, std::int32_t nodeBonusPosY,
        std::int32_t enemyBonusPosX, std::int32_t enemyBonusPosY,
        std::int32_t nodeIconPosX, std::int32_t nodeIconPosY,
        std::int32_t nodeTextPosX, std::int32_t nodeTextPosY,
        std::int32_t dblDmgPosX, std::int32_t dblDmgPosY,
        std::int32_t dblDmgTextPosX, std::int32_t dblDmgTextPosY,
        Align dblDmgAlign, std::int32_t cloakPosX, std::int32_t cloakPosY,
        std::int32_t cloakTextPosX, std::int32_t cloakTextPosY, Align cloakAlign)
        : Helmet(std::move(helmet)), HelmetDrop(std::move(helmetDrop)), Visor(std::move(visor)),
          ScanVisor(std::move(scanVisor)), HealthBarA(std::move(healthBarA)),
          HealthBarB(std::move(healthBarB)), EnergyTanks(std::move(energyTanks)),
          WeaponIcon(std::move(weaponIcon)), DoubleDamage(std::move(doubleDamage)),
          Cloaking(std::move(cloaking)), PrimeHunter(std::move(primeHunter)),
          AmmoBar(std::move(ammoBar)), Reticle(std::move(reticle)),
          SniperReticle(std::move(sniperReticle)), WeaponSelect(std::move(weaponSelect)),
          SelectIcon(std::move(selectIcon)), SelectBox(std::move(selectBox)),
          DamageBar(std::move(damageBar)), HealthMainPosX(healthMainPosX),
          HealthSubPosX(healthSubPosX), HealthSubPosY(healthSubPosY),
          HealthOffsetY(healthOffsetY), HealthOffsetYAlt(healthOffsetYAlt),
          HealthMainPosY(healthMainPosY), AmmoBarPosX(ammoBarPosX), AmmoBarPosY(ammoBarPosY),
          WeaponIconPosX(weaponIconPosX), WeaponIconPosY(weaponIconPosY),
          EnemyHealthPosX(enemyHealthPosX), EnemyHealthPosY(enemyHealthPosY),
          EnemyHealthTextPosX(enemyHealthTextPosX), EnemyHealthTextPosY(enemyHealthTextPosY),
          ScorePosX(scorePosX), ScorePosY(scorePosY), ScoreAlign(scoreAlign),
          OctolithPosX(octolithPosX), OctolithPosY(octolithPosY), PrimePosX(primePosX),
          PrimePosY(primePosY), PrimeTextPosX(primeTextPosX), PrimeTextPosY(primeTextPosY),
          PrimeAlign(primeAlign), NodeBonusPosX(nodeBonusPosX), NodeBonusPosY(nodeBonusPosY),
          EnemyBonusPosX(enemyBonusPosX), EnemyBonusPosY(enemyBonusPosY),
          NodeIconPosX(nodeIconPosX), NodeIconPosY(nodeIconPosY),
          NodeTextPosX(nodeTextPosX), NodeTextPosY(nodeTextPosY), DblDmgPosX(dblDmgPosX),
          DblDmgPosY(dblDmgPosY), DblDmgTextPosX(dblDmgTextPosX),
          DblDmgTextPosY(dblDmgTextPosY), DblDmgAlign(dblDmgAlign),
          CloakPosX(cloakPosX), CloakPosY(cloakPosY), CloakTextPosX(cloakTextPosX),
          CloakTextPosY(cloakTextPosY), CloakAlign(cloakAlign)
    {
    }

    namespace
    {
        std::shared_ptr<HudMeter> MakeMeter(bool horizontal, std::int32_t tankAmount,
            std::int32_t tankCount, std::int32_t length, std::int32_t tankSpacing,
            std::int32_t tankOffsetX, std::int32_t tankOffsetY, std::int32_t barOffsetX,
            std::int32_t barOffsetY, Align align, std::int32_t textOffsetX,
            std::int32_t textOffsetY, std::int32_t messageId)
        {
            auto result = std::make_shared<HudMeter>();
            result->Horizontal = horizontal;
            result->TankAmount = tankAmount;
            result->TankCount = tankCount;
            result->Length = length;
            result->TankSpacing = tankSpacing;
            result->TankOffsetX = tankOffsetX;
            result->TankOffsetY = tankOffsetY;
            result->BarOffsetX = barOffsetX;
            result->BarOffsetY = barOffsetY;
            result->Align = align;
            result->TextOffsetX = textOffsetX;
            result->TextOffsetY = textOffsetY;
            result->MessageId = messageId;
            return result;
        }

        std::shared_ptr<HudObjects> MakeHunterObjects(const std::string& local,
            std::int32_t healthMainPosX, std::int32_t healthMainPosY,
            std::int32_t healthSubPosX, std::int32_t healthSubPosY,
            std::int32_t healthOffsetY, std::int32_t healthOffsetYAlt,
            std::int32_t ammoBarPosX, std::int32_t ammoBarPosY,
            std::int32_t weaponIconPosX, std::int32_t weaponIconPosY,
            std::int32_t scorePosX, std::int32_t scorePosY, Align scoreAlign,
            std::int32_t octolithPosX, std::int32_t octolithPosY,
            std::int32_t primePosX, std::int32_t primePosY,
            std::int32_t primeTextPosX, std::int32_t primeTextPosY, Align primeAlign,
            std::int32_t nodeBonusPosX, std::int32_t nodeBonusPosY,
            std::int32_t enemyBonusPosX, std::int32_t enemyBonusPosY,
            std::int32_t nodeIconPosX, std::int32_t nodeIconPosY,
            std::int32_t nodeTextPosX, std::int32_t nodeTextPosY,
            std::int32_t dblDmgPosX, std::int32_t dblDmgPosY,
            std::int32_t dblDmgTextPosX, std::int32_t dblDmgTextPosY, Align dblDmgAlign,
            std::int32_t cloakPosX, std::int32_t cloakPosY,
            std::int32_t cloakTextPosX, std::int32_t cloakTextPosY, Align cloakAlign)
        {
            const std::string base = "_archives\\" + local + "\\";
            return std::make_shared<HudObjects>(
                base + "bg_top.bin", base + "bg_top_drop.bin", base + "bg_top_ovl.bin",
                "_archives\\localSamus\\bg_top_ovl.bin", base + "hud_energybar.bin",
                base + "hud_energybar2.bin", std::optional<std::string>("_archives\\spSamus\\hud_etank.bin"),
                base + "hud_weaponicon.bin", base + "hud_damage.bin", base + "cloaking.bin",
                base + "hud_primehunter.bin", base + "hud_ammobar.bin", base + "hud_targetcircle.bin",
                base + "hud_snipercircle.bin", base + "rad_wepsel.bin", base + "wepsel_icon.bin",
                base + "wepsel_box.bin", base + "rad_ammobar.bin",
                healthMainPosX, healthMainPosY, healthSubPosX, healthSubPosY,
                healthOffsetY, healthOffsetYAlt, ammoBarPosX, ammoBarPosY,
                weaponIconPosX, weaponIconPosY, 93, 164, 128, 168,
                scorePosX, scorePosY, scoreAlign, octolithPosX, octolithPosY,
                primePosX, primePosY, primeTextPosX, primeTextPosY, primeAlign,
                nodeBonusPosX, nodeBonusPosY, enemyBonusPosX, enemyBonusPosY,
                nodeIconPosX, nodeIconPosY, nodeTextPosX, nodeTextPosY,
                dblDmgPosX, dblDmgPosY, dblDmgTextPosX, dblDmgTextPosY, dblDmgAlign,
                cloakPosX, cloakPosY, cloakTextPosX, cloakTextPosY, cloakAlign);
        }

        RulesInfo::IntArray IntArray(std::initializer_list<std::int32_t> values)
        {
            return std::make_shared<std::vector<std::int32_t>>(values);
        }
    }

    const std::string HudElements::IceLayer = "_archives\\common\\bg_ice.bin";
    const std::string HudElements::Boost = "_archives\\common\\hud_boost.bin";
    const std::string HudElements::Bombs = "_archives\\common\\hud_bombs.bin";
    const std::string HudElements::Stars = "_archives\\commonMP\\stars.bin";
    const std::string HudElements::Octolith = "_archives\\commonMP\\radar_octolithLARGE.bin";
    const std::string HudElements::NodesOG = "hud\\rad_NodesOG.bin";
    const std::string HudElements::NodesRB = "hud\\rad_NodesRB.bin";
    const std::string HudElements::SystemLoad = "_archives\\commonMP\\hud_systemload.bin";
    const std::string HudElements::MessageBox = "_archives\\spSamus\\hud_msgBox.bin";
    const std::string HudElements::MessageSpacer = "_archives\\spSamus\\message_spacer.bin";
    const std::string HudElements::MapScan = "_archives\\spSamus\\map_scan.bin";
    const std::string HudElements::DialogButton = "_archives\\spSamus\\scan_ok.bin";
    const std::string HudElements::DialogArrow = "_archives\\spSamus\\scan_arrow.bin";
    const std::string HudElements::DialogCrystal = "_archives\\spSamus\\message_crystalpickup.bin";
    const std::string HudElements::DialogPickup = "_archives\\spSamus\\message_pickups.bin";
    const std::string HudElements::DialogFrame = "_archives\\spSamus\\message_pickupframe.bin";
    const std::string HudElements::MapPortal = "_archives\\spSamus\\map_portal.bin";
    const std::string HudElements::MapOctolith = "_archives\\spSamus\\map_crystalbig.bin";
    const std::string HudElements::MapLostOctolith = "_archives\\spSamus\\map_crystalred.bin";
    const std::string HudElements::MapLegendDoors = "_archives\\spSamus\\map_legendDoors.bin";
    const std::string HudElements::MapLegendOther = "_archives\\spSamus\\map_legendOthers.bin";
    const std::string HudElements::MapQuit = "_archives\\spSamus\\map_quit.bin";

    const ReadOnlyList<std::string> HudElements::Hunters = MakeReadOnlyList<std::string>({
        "_archives\\common\\enemy_samus.bin",
        "_archives\\common\\enemy_kanden.bin",
        "_archives\\common\\enemy_trace.bin",
        "_archives\\common\\enemy_sylux.bin",
        "_archives\\common\\enemy_noxus.bin",
        "_archives\\common\\enemy_spyre.bin",
        "_archives\\common\\enemy_weavel.bin",
        "_archives\\common\\enemy_samus.bin"
    });

    const ReadOnlyList<std::string> HudElements::MapDots = MakeReadOnlyList<std::string>({
        "_archives\\spSamus\\map_art_1.bin", "_archives\\spSamus\\map_art_2.bin",
        "_archives\\spSamus\\map_art_3.bin", "_archives\\spSamus\\map_art_4.bin",
        "_archives\\spSamus\\map_art_5.bin", "_archives\\spSamus\\map_art_6.bin",
        "_archives\\spSamus\\map_art_7.bin", "_archives\\spSamus\\map_art_8.bin"
    });

    ReadOnlyList<std::shared_ptr<MphRead::Hud::RulesInfo>> HudElements::RulesInfo
        = MakeReadOnlyList<std::shared_ptr<MphRead::Hud::RulesInfo>>({
            std::make_shared<MphRead::Hud::RulesInfo>(4, IntArray({1, 2, 3, 4, 0, 0, 0, 0}), IntArray({0, 0, 0, 0, 0, 0, 0, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(4, IntArray({11, 12, 13, 14, 0, 0, 0, 0}), IntArray({0, 0, 0, 0, 0, 0, 0, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(7, IntArray({21, 22, 23, 24, 25, 26, 27, 0}), IntArray({0, 0, 0, 0, 12, 12, 12, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(5, IntArray({31, 32, 33, 34, 35, 0, 0, 0}), IntArray({0, 0, 0, 0, 0, 0, 0, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(6, IntArray({41, 42, 43, 44, 45, 46, 0, 0}), IntArray({0, 0, 0, 0, 0, 0, 0, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(4, IntArray({51, 52, 53, 54, 0, 0, 0, 0}), IntArray({0, 0, 0, 0, 0, 0, 0, 0})),
            std::make_shared<MphRead::Hud::RulesInfo>(8, IntArray({61, 62, 63, 64, 65, 66, 67, 68}), IntArray({0, 0, 0, 0, 0, 12, 12, 12}))
        });

    const std::string HudElements::ScanCorner = "_archives\\spSamus\\scan_corner.bin";
    const std::string HudElements::ScanCornerSmall = "_archives\\spSamus\\scan_cornerSm.bin";
    const std::string HudElements::ScanLineHoriz = "_archives\\spSamus\\scan_horizline.bin";
    const std::string HudElements::ScanLineVert = "_archives\\spSamus\\scan_vertline.bin";

    ReadOnlyList<std::string> HudElements::ScanIcons = MakeReadOnlyList<std::string>({
        "_archives\\spSamus\\scan_lore.bin", "_archives\\spSamus\\scan_lore_dim.bin",
        "_archives\\spSamus\\scan_enemy.bin", "_archives\\spSamus\\scan_enemy_dim.bin",
        "_archives\\spSamus\\scan_object.bin", "_archives\\spSamus\\scan_object_dim.bin",
        "_archives\\spSamus\\scan_equipment.bin", "_archives\\spSamus\\scan_equipment_dim.bin",
        "_archives\\spSamus\\scan_red.bin", "_archives\\spSamus\\scan_red_dim.bin"
    });

    ReadOnlyList<std::shared_ptr<HudObjects>> HudElements::HunterObjects
        = MakeReadOnlyList<std::shared_ptr<HudObjects>>({
            MakeHunterObjects("localSamus", 93, -5, 93, 1, 32, -10, 236, 137, 214, 150,
                12, 30, Align::Left, 228, 28, 232, 42, -16, -4, Align::Right,
                22, 56, 22, 80, 220, 41, 220, 45, 64, 174, 16, -8, Align::Left,
                192, 174, -16, 2, Align::Right),
            MakeHunterObjects("localKanden", 13, 0, 20, 0, 128, 0, 238, 128, 230, 138,
                20, 4, Align::Left, 212, 4, 222, 17, -16, -10, Align::Right,
                96, 4, 136, 4, 210, 12, 210, 14, 22, 156, 6, 14, Align::Left,
                224, 176, -16, 3, Align::Right),
            MakeHunterObjects("localTrace", 24, 135, 29, 135, 0, 0, 232, 135, 225, 148,
                128, 12, Align::Center, 176, 12, 226, 56, -16, -10, Align::Right,
                60, 24, 24, 38, 202, 32, 202, 36, 48, 172, 16, -7, Align::Left,
                208, 172, -16, 3, Align::Right),
            MakeHunterObjects("localSylux", 47, 165, 51, 165, 0, 0, 206, 165, 214, 131,
                56, 8, Align::Left, 186, 4, 190, 16, 14, 17, Align::Right,
                212, 38, 212, 62, 180, 15, 180, 17, 32, 164, 20, -3, Align::Left,
                186, 162, 16, 12, Align::Right),
            MakeHunterObjects("localNox", 29, 0, 34, 0, 117, 0, 221, 117, 196, 138,
                36, 12, Align::Left, 200, 8, 204, 16, 14, 17, Align::Right,
                40, 32, 190, 32, 200, 18, 200, 20, 56, 173, 16, -8, Align::Left,
                200, 173, -16, 2, Align::Right),
            MakeHunterObjects("localSpire", 12, 0, 21, 0, 128, 0, 233, 128, 227, 20,
                10, 16, Align::Left, 208, 13, 210, 20, 14, 17, Align::Right,
                37, 35, 193, 35, 196, 16, 196, 20, 68, 164, 16, -8, Align::Left,
                188, 164, -16, 2, Align::Right),
            MakeHunterObjects("localWeavel", 22, 118, 30, 118, 0, 0, 229, 118, 206, 104,
                128, 18, Align::Center, 216, 4, 214, 78, -16, -10, Align::Right,
                36, 4, 196, 4, 128, 9, 128, 11, 88, 178, 13, -8, Align::Left,
                168, 178, 0, -18, Align::Center),
            std::make_shared<HudObjects>(
                "_archives\\localWeavel\\bg_top.bin", "_archives\\localWeavel\\bg_top_drop.bin",
                "_archives\\localKanden\\bg_top_ovl.bin", "_archives\\localSamus\\bg_top_ovl.bin",
                "_archives\\localSamus\\hud_energybar.bin", "_archives\\localSamus\\hud_energybar2.bin",
                std::optional<std::string>("_archives\\spSamus\\hud_etank.bin"),
                "_archives\\localSamus\\hud_weaponicon.bin", "_archives\\localSamus\\hud_damage.bin",
                "_archives\\localSamus\\cloaking.bin", "_archives\\localSamus\\hud_primehunter.bin",
                "_archives\\localSamus\\hud_ammobar.bin", "_archives\\localSamus\\hud_targetcircle.bin",
                "_archives\\localSamus\\hud_snipercircle.bin", "_archives\\localSamus\\rad_wepsel.bin",
                "_archives\\localSamus\\wepsel_icon.bin", "_archives\\localSamus\\wepsel_box.bin",
                "_archives\\localSamus\\rad_ammobar.bin", 93, -5, 93, 1, 32, -10, 236, 137,
                214, 150, 93, 164, 128, 168, 12, 30, Align::Left, 228, 28, 232, 42,
                -16, -4, Align::Right, 22, 56, 22, 80, 220, 41, 220, 45, 64, 174,
                16, -8, Align::Left, 192, 174, -16, 2, Align::Right)
        });

    const std::shared_ptr<HudMeter> HudElements::EnemyHealthbar
        = MakeMeter(true, 0, 0, 0, 0, 0, 0, 15, 6, Align::Left, 30, 7, 0);
    const std::shared_ptr<HudMeter> HudElements::NodeProgressBar
        = MakeMeter(true, 100, 5, 40, 8, 1, -8, 15, 6, Align::Center, 0, -7, 0);

    const ReadOnlyList<std::shared_ptr<HudMeter>> HudElements::MainHealthbars
        = MakeReadOnlyList<std::shared_ptr<HudMeter>>({
            MakeMeter(true, 100, 0, 72, 6, 1, 8, 0, -8, Align::Left, 30, -8, 6),
            MakeMeter(false, 100, 5, 80, 8, 8, 3, 32, -35, Align::Right, 30, -7, 0),
            MakeMeter(false, 100, 0, 64, 8, -8, 3, 8, 0, Align::Left, 0, 0, 0),
            MakeMeter(false, 100, 5, 152, 8, 8, 1, -4, -66, Align::Right, 0, 0, 0),
            MakeMeter(false, 100, 5, 80, 8, 8, 3, -3, 1, Align::Right, 30, -7, 0),
            MakeMeter(false, 100, 5, 80, 8, 10, 3, 5, -82, Align::Center, 30, -7, 0),
            MakeMeter(false, 100, 5, 64, 8, 8, 3, 10, -68, Align::Left, 0, 0, 0),
            MakeMeter(true, 100, 0, 72, 6, 1, 8, 0, -8, Align::Left, 30, -8, 6)
        });

    const ReadOnlyList<std::shared_ptr<HudMeter>> HudElements::SubHealthbars
        = MakeReadOnlyList<std::shared_ptr<HudMeter>>({
            MakeMeter(true, 100, 5, 72, 8, 1, -8, 15, 6, Align::Left, 30, 7, 0),
            MakeMeter(false, 100, 5, 80, 8, 1, -8, 15, 6, Align::Left, 30, 7, 0),
            MakeMeter(false, 100, 0, 64, 0, 0, 0, 0, 0, Align::Left, 0, 0, 0),
            MakeMeter(false, 100, 5, 152, 8, 1, -8, 0, 0, Align::Left, 0, 0, 0),
            MakeMeter(false, 100, 5, 80, 8, 1, -8, 15, 6, Align::Left, 30, 7, 0),
            MakeMeter(false, 100, 5, 80, 8, 1, -8, 15, 6, Align::Left, 30, 7, 0),
            MakeMeter(false, 100, 5, 64, 8, 1, -8, 0, 0, Align::Left, 0, 0, 0),
            MakeMeter(true, 100, 5, 72, 8, 1, -8, 15, 6, Align::Left, 30, 7, 0)
        });

    const ReadOnlyList<std::shared_ptr<HudMeter>> HudElements::AmmoBars
        = MakeReadOnlyList<std::shared_ptr<HudMeter>>({
            MakeMeter(false, 100, 5, 72, 8, 8, 1, -3, 0, Align::Right, -2, -1, 0),
            MakeMeter(false, 100, 5, 80, 8, 8, 1, -22, -35, Align::Left, -2, -1, 0),
            MakeMeter(false, 100, 0, 64, 0, 0, 0, -2, 0, Align::Right, 0, 0, 0),
            MakeMeter(false, 100, 5, 152, 8, 8, 1, 6, -66, Align::Left, 0, 0, 0),
            MakeMeter(false, 100, 5, 80, 8, 8, 1, 9, 1, Align::Left, -2, -1, 0),
            MakeMeter(false, 100, 5, 80, 8, 8, 1, 3, -82, Align::Center, -2, -1, 0),
            MakeMeter(false, 100, 5, 64, 8, 8, 1, -10, -68, Align::Center, 0, 0, 0),
            MakeMeter(false, 100, 5, 72, 8, 8, 1, -3, 0, Align::Right, -2, -1, 0)
        });
}
