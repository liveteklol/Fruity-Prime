#include "LauncherPhoto.hpp"
#include "SceneWindowUi.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlLauncherPhoto.hpp"

#if !defined(__ANDROID__)
#include "../DebugLog.hpp"
#include "../../NativeRuntime/Avalonia/Media.hpp"
#include "../../NativeRuntime/Avalonia/Platform.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/Rhi/WindowUi.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Stopwatch.hpp"

#include <array>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#endif

namespace MphRead::Mods::Render
{
    using Gpu = ::MphRead::NativeRuntime::Rhi::OpenGL::OpenGlLauncherPhoto;

#if !defined(__ANDROID__)
    namespace
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        namespace Avalonia = ::MphRead::NativeRuntime::Avalonia;

        // The same photograph and the same moving layer OpenGlLauncherPhoto
        // draws, through the window's Rhi::WindowUi: what differs is only
        // where the texture lives and who issues the draw.
        struct WindowPhoto final
        {
            bool Tried = false;
            std::unique_ptr<Rhi::Texture> Texture;
            std::unique_ptr<Rhi::Sampler> Sampler;
            std::int32_t Width = 0;
            std::int32_t Height = 0;
        };

        WindowPhoto& Photo()
        {
            static WindowPhoto photo;
            return photo;
        }

        bool EnsurePhoto()
        {
            auto& photo = Photo();
            if (photo.Tried) return photo.Texture != nullptr;
            photo.Tried = true;
            try
            {
                constexpr std::string_view resource = "avares://FruityPrime/Assets/Backgrounds/launcher-bg.jpg";
                if (!Avalonia::Platform::AssetLoader::Exists(resource)) return false;
                const std::vector<std::uint8_t> bytes = Avalonia::Platform::AssetLoader::Open(resource);
                const auto image = Avalonia::Media::Imaging::Bitmap::FromBytes(bytes);
                const auto* pixels = image->Pixels();
                if (pixels == nullptr || pixels->Pixels() == nullptr || pixels->Width() <= 0 || pixels->Height() <= 0)
                    return false;
                photo.Width = pixels->Width();
                photo.Height = pixels->Height();
                auto& gpu = Rhi::SceneDevice();
                photo.Texture = gpu.CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(photo.Width),
                    static_cast<std::uint32_t>(photo.Height), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
                    Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDst});
                gpu.WriteTexture(*photo.Texture, Rhi::TextureWrite{static_cast<std::uint32_t>(photo.Width),
                    static_cast<std::uint32_t>(photo.Height), Rhi::TextureFormat::RGBA8Unorm, pixels->Pixels()});
                Rhi::SamplerDesc sampler{};
                sampler.minFilter = Rhi::Filter::Linear;
                sampler.magFilter = Rhi::Filter::Linear;
                sampler.addressU = Rhi::SamplerAddressMode::ClampToEdge;
                sampler.addressV = Rhi::SamplerAddressMode::ClampToEdge;
                photo.Sampler = gpu.CreateSampler(sampler);
                ::MphRead::Mods::DebugLog::Line("ui", "launcher backdrop " + std::to_string(photo.Width) + "x"
                    + std::to_string(photo.Height) + " (vulkan)");
                return true;
            }
            catch (...)
            {
                photo.Texture.reset();
                ::MphRead::Mods::DebugLog::Line("ui", "no launcher backdrop: "
                    + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
                return false;
            }
        }

        [[nodiscard]] std::int64_t BackdropClock()
        {
            static const std::int64_t clock = ::MphRead::NativeRuntime::StopwatchGetTimestamp();
            return clock;
        }
    }
#endif

    void LauncherPhoto::Release() noexcept
    {
#if !defined(__ANDROID__)
        auto& photo = Photo();
        photo.Texture.reset();
        photo.Sampler.reset();
        photo.Tried = false;
#endif
        Gpu::Forget();
    }

    void LauncherPhoto::Enabled(bool value) noexcept { Gpu::Enabled(value); }
    bool LauncherPhoto::Enabled() noexcept { return Gpu::Enabled(); }

    void LauncherPhoto::Draw(std::int32_t width, std::int32_t height)
    {
#if !defined(__ANDROID__)
        if (auto* ui = SceneWindowUi::Get())
        {
            if (!Gpu::Enabled() || width <= 0 || height <= 0 || !EnsurePhoto()) return;
            const auto& photo = Photo();
            // OpenGlLauncherPhoto::Draw's crop, to the same four numbers.
            const double window = static_cast<double>(width) / height;
            const double picture = static_cast<double>(photo.Width) / photo.Height;
            float u = 1;
            float v = 1;
            if (window > picture) v = static_cast<float>(picture / window);
            else u = static_cast<float>(window / picture);
            const float u0 = (1 - u) / 2;
            const float u1 = u0 + u;
            const float v0 = (1 - v) / 2;
            const float v1 = v0 + v;
            const std::array<Rhi::WindowQuadVertex, 4> strip{{
                {{ 1.0F,  1.0F}, {u1, v0}, {1.0F, 0.0F}},
                {{-1.0F,  1.0F}, {u0, v0}, {0.0F, 0.0F}},
                {{ 1.0F, -1.0F}, {u1, v1}, {1.0F, 1.0F}},
                {{-1.0F, -1.0F}, {u0, v1}, {0.0F, 1.0F}}}};
            const float seconds = static_cast<float>(::MphRead::NativeRuntime::TimeSpanTotalMilliseconds(
                ::MphRead::NativeRuntime::StopwatchGetElapsedTicks(BackdropClock())) / 1000.0);
            ui->Begin(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), false);
            ui->DrawBackdrop(*photo.Texture, *photo.Sampler, strip, Gpu::Strength, seconds);
            ui->End();
            return;
        }
#endif
        Gpu::Draw(width, height);
    }
}
