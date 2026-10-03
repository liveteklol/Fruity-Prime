#include "UiOverlay.hpp"

#include "LauncherHunter.hpp"
#include "LauncherPhoto.hpp"
#include "SceneWindowUi.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlLauncherOverlay.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"

#include "../../NativeRuntime/Rhi/WindowUi.hpp"

#include <array>
#include <memory>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Render
{
    std::int32_t UiOverlay::_width = 0;
    std::int32_t UiOverlay::_height = 0;
    bool UiOverlay::_hasFrame = false;
    bool UiOverlay::_visible = false;
    bool UiOverlay::_topRowAtTextureZero = true;

    namespace
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;

        // The scene-presented window's overlay: the texture shown, the one this owns
        // when the UI had to be uploaded, and how it is sampled.
        struct WindowOverlay final
        {
            const Rhi::Texture* Shown = nullptr;
            std::unique_ptr<Rhi::Texture> Owned;
            std::unique_ptr<Rhi::Sampler> Sampler;
        };

        WindowOverlay& Vk()
        {
            static WindowOverlay overlay;
            return overlay;
        }

        const Rhi::Sampler& LinearClamp()
        {
            auto& sampler = Vk().Sampler;
            if (!sampler)
            {
                Rhi::SamplerDesc desc{};
                desc.minFilter = Rhi::Filter::Linear;
                desc.magFilter = Rhi::Filter::Linear;
                desc.addressU = Rhi::SamplerAddressMode::ClampToEdge;
                desc.addressV = Rhi::SamplerAddressMode::ClampToEdge;
                sampler = Rhi::SceneDevice().CreateSampler(desc);
            }
            return *sampler;
        }
    }

    bool UiOverlay::Visible() noexcept
    {
        return _visible;
    }

    void UiOverlay::Visible(bool value) noexcept
    {
        _visible = value;
    }

    bool UiOverlay::HasFrame() noexcept
    {
        return _hasFrame;
    }

    void UiOverlay::Upload(const void* pixels, std::int32_t width, std::int32_t height)
    {
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            return;
        }
        if (SceneWindowUi::Active())
        {
            auto& gpu = Rhi::SceneDevice();
            auto& owned = Vk().Owned;
            if (!owned || owned->Desc().width != static_cast<std::uint32_t>(width)
                || owned->Desc().height != static_cast<std::uint32_t>(height))
            {
                owned = gpu.CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
                    Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDst});
            }
            gpu.WriteTexture(*owned, Rhi::TextureWrite{static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height), Rhi::TextureFormat::RGBA8Unorm, pixels});
            Vk().Shown = owned.get();
            _width = width;
            _height = height;
            _topRowAtTextureZero = true;
            _hasFrame = true;
            return;
        }
        Rhi::OpenGL::OpenGlLauncherOverlay::Upload(pixels, width, height);
        _width = width;
        _height = height;
        _topRowAtTextureZero = true;
        _hasFrame = true;
    }

    void UiOverlay::UseTexture(std::int32_t texture, std::int32_t width, std::int32_t height)
    {
        if (texture == 0 || width <= 0 || height <= 0)
        {
            return;
        }
        Rhi::OpenGL::OpenGlLauncherOverlay::Adopt(texture);
        _width = width;
        _height = height;
        _topRowAtTextureZero = false;
        _hasFrame = true;
    }

    void UiOverlay::UseTexture(const Rhi::Texture& texture, std::int32_t width, std::int32_t height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        Vk().Shown = &texture;
        // Composited by the GL overlay when the window is not presented
        // through the RHI: the same texture, by the GL name it has there.
        if (!SceneWindowUi::Active() && texture.Handle())
        {
            Rhi::OpenGL::OpenGlLauncherOverlay::Adopt(texture.Handle().value);
        }
        _width = width;
        _height = height;
        _topRowAtTextureZero = true;
        _hasFrame = true;
    }

    void UiOverlay::Draw(std::int32_t width, std::int32_t height)
    {
#if !defined(__ANDROID__)
        if (auto* ui = SceneWindowUi::Get())
        {
            if (!_visible || !_hasFrame || Vk().Shown == nullptr || width <= 0 || height <= 0)
            {
                return;
            }
            const float topT = _topRowAtTextureZero ? 0.0F : 1.0F;
            const float bottomT = _topRowAtTextureZero ? 1.0F : 0.0F;
            const std::array<Rhi::WindowQuadVertex, 4> strip{{
                {{ 1.0F,  1.0F}, {1.0F, topT}, {}},
                {{-1.0F,  1.0F}, {0.0F, topT}, {}},
                {{ 1.0F, -1.0F}, {1.0F, bottomT}, {}},
                {{-1.0F, -1.0F}, {0.0F, bottomT}, {}}}};
            ui->Begin(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), false);
            ui->DrawTexture(*Vk().Shown, LinearClamp(), strip, true);
            ui->End();
            return;
        }
#endif
        if (!_visible || !_hasFrame)
        {
            return;
        }
        Rhi::OpenGL::OpenGlLauncherOverlay::Draw(width, height, _topRowAtTextureZero);
    }

    void UiOverlay::DrawAlone(::MphRead::RenderWindow& window, std::int32_t width, std::int32_t height)
    {
#if !defined(__ANDROID__)
        if (auto* ui = SceneWindowUi::Get())
        {
            ui->Begin(static_cast<std::uint32_t>(std::max(width, 1)), static_cast<std::uint32_t>(std::max(height, 1)),
                true);
            ui->End();
            LauncherPhoto::Draw(width, height);
            Draw(width, height);
            LauncherHunter::Draw(window, width, height);
            return;
        }
#endif
        Rhi::OpenGL::OpenGlLauncherOverlay::Clear(width, height);
        LauncherPhoto::Draw(width, height);
        Draw(width, height);
        LauncherHunter::Draw(window, width, height);
    }

    void UiOverlay::Release()
    {
        if (SceneWindowUi::Active())
        {
            Vk().Shown = nullptr;
            Vk().Owned.reset();
            Vk().Sampler.reset();
            SceneWindowUi::Release();
            _width = 0;
            _height = 0;
            _hasFrame = false;
            _topRowAtTextureZero = true;
            return;
        }
        Rhi::OpenGL::OpenGlLauncherOverlay::Release();
        _width = 0;
        _height = 0;
        _hasFrame = false;
        _topRowAtTextureZero = true;
    }
}
