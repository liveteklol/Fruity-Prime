#if defined(__ANDROID__)
#include "OpenGlAndroidLauncherPhotoInternal.inc"
#else
#include "OpenGlLauncherPhoto.hpp"
#include "OpenGlWindowDraw.hpp"
#include "OpenGlDevice.hpp"

#include "../../../Mods/DebugLog.hpp"
#include "../../../Shaders.hpp"
#include "../../Avalonia/Media.hpp"
#include "../../Avalonia/Platform.hpp"
#include "../../OpenTK/GL.hpp"
#include "../../System/ExceptionText.hpp"
#include "../../System/Stopwatch.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    using ::MphRead::Mods::DebugLog;
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;
    namespace Avalonia = ::MphRead::NativeRuntime::Avalonia;
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::int64_t BackdropClock()
        {
            static const std::int64_t clock = Runtime::StopwatchGetTimestamp();
            return clock;
        }
    }

    bool OpenGlLauncherPhoto::_enabled = false;
    std::int32_t OpenGlLauncherPhoto::_texture = 0;
    std::int32_t OpenGlLauncherPhoto::_width = 0;
    std::int32_t OpenGlLauncherPhoto::_height = 0;
    bool OpenGlLauncherPhoto::_tried = false;
    std::unique_ptr<OpenGlWindowDraw> OpenGlLauncherPhoto::_draw;

    void OpenGlLauncherPhoto::Enabled(bool value) noexcept
    {
        _enabled = value;
    }

    bool OpenGlLauncherPhoto::Enabled() noexcept
    {
        return _enabled;
    }

    void OpenGlLauncherPhoto::Forget() noexcept
    {
        _texture = 0;
        _width = 0;
        _height = 0;
        _tried = false;
        _draw.reset();
    }

    void OpenGlLauncherPhoto::Draw(std::int32_t width, std::int32_t height)
    {
        if (!_enabled || width <= 0 || height <= 0 || !Ensure())
        {
            return;
        }

        const double window = static_cast<double>(width) / height;
        const double picture = static_cast<double>(_width) / _height;
        float u = 1;
        float v = 1;
        if (window > picture)
        {
            v = static_cast<float>(picture / window);
        }
        else
        {
            u = static_cast<float>(window / picture);
        }
        const float u0 = (1 - u) / 2;
        const float u1 = u0 + u;
        const float v0 = (1 - v) / 2;
        const float v1 = v0 + v;

        const std::array<WindowVertex, 4> vertices{{
            {{1, 1, 0}, {u1, v0}, {1, 0}}, {{-1, 1, 0}, {u0, v0}, {0, 0}},
            {{1, -1, 0}, {u1, v1}, {1, 1}}, {{-1, -1, 0}, {u0, v1}, {0, 1}}}};
        if (!_draw) _draw = std::make_unique<OpenGlWindowDraw>();
        const float seconds = static_cast<float>(
            Runtime::TimeSpanTotalMilliseconds(Runtime::StopwatchGetElapsedTicks(BackdropClock())) / 1000.0);
        _draw->Draw(_texture, vertices, width, height, false, true, Strength, seconds);
        GL::Color4(1, 1, 1, 1);
        GL::MultiTexCoord2(GL::TextureUnit::Texture0, u0, v1);
        GL::MultiTexCoord2(GL::TextureUnit::Texture1, 0, 1);
        GL::Enable(GL::EnableCap::Blend);
    }

    bool OpenGlLauncherPhoto::Ensure()
    {
        if (_tried)
        {
            return _texture != 0;
        }
        _tried = true;
        try
        {
            constexpr std::string_view resource = "avares://FruityPrime/Assets/Backgrounds/launcher-bg.jpg";
            if (!Avalonia::Platform::AssetLoader::Exists(resource))
            {
                DebugLog::Line("ui", "no backdrop resource in this build");
                return false;
            }
            const std::vector<std::uint8_t> bytes = Avalonia::Platform::AssetLoader::Open(resource);
            const std::shared_ptr<Avalonia::Media::Imaging::Bitmap> image
                = Avalonia::Media::Imaging::Bitmap::FromBytes(bytes);
            const ::MphRead::NativeRuntime::Skia::Bitmap* pixels = image->Pixels();
            if (pixels == nullptr || pixels->Pixels() == nullptr)
            {
                DebugLog::Line("ui", "the backdrop decoded to nothing");
                return false;
            }
            _width = pixels->Width();
            _height = pixels->Height();
            if (_width <= 0 || _height <= 0)
            {
                return false;
            }

            AdmitInteropTextureStorage(TextureFormat::RGBA8Unorm, _width, _height);
            GL::ActiveTexture(GL::TextureUnit::Texture0);
            _texture = Name;
            GL::BindTexture(GL::TextureTarget::Texture2D, _texture);
            GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, 4);
            GL::PixelStore(GL::PixelStoreParameter::UnpackRowLength, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipPixels, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipRows, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackImageHeight, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipImages, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSwapBytes, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackLsbFirst, 0);
            GL::TexImage2D(GL::TextureTarget::Texture2D, 0, GL::PixelInternalFormat::Rgba,
                _width, _height, 0, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, pixels->Pixels());
            CheckInteropStorageResult("OpenGL launcher backdrop allocation");
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureBaseLevel, 0);
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureMaxLevel, 0);
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureMinFilter,
                static_cast<std::int32_t>(GL::TextureMinFilter::Linear));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureMagFilter,
                static_cast<std::int32_t>(GL::TextureMagFilter::Linear));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureWrapS,
                static_cast<std::int32_t>(GL::TextureWrapMode::ClampToEdge));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureWrapT,
                static_cast<std::int32_t>(GL::TextureWrapMode::ClampToEdge));
            GL::BindTexture(GL::TextureTarget::Texture2D, 0);
            DebugLog::Line("ui", "launcher backdrop " + std::to_string(_width) + "x" + std::to_string(_height));
            return true;
        }
        catch (...)
        {
            _texture = 0;
            DebugLog::Line("ui", "no launcher backdrop: "
                + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            return false;
        }
    }
}

#endif
