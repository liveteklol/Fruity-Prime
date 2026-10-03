#if defined(__ANDROID__)
#include "OpenGlAndroidLauncherOverlayInternal.inc"
#else
#include "OpenGlLauncherOverlay.hpp"
#include "OpenGlWindowDraw.hpp"
#include "OpenGlDevice.hpp"
#include <memory>

#include "../../OpenTK/GL.hpp"
#include "../../OpenTK/GpuTrace.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;

    std::int32_t OpenGlLauncherOverlay::_texture = 0;
    std::unique_ptr<OpenGlWindowDraw> OpenGlLauncherOverlay::_draw;
    std::int32_t OpenGlLauncherOverlay::_width = 0;
    std::int32_t OpenGlLauncherOverlay::_height = 0;
    bool OpenGlLauncherOverlay::_ownsTexture = false;

    void OpenGlLauncherOverlay::Upload(const void* pixels, std::int32_t width, std::int32_t height)
    {
        GL::ActiveTexture(GL::TextureUnit::Texture0);
        if (_texture != 0 && !_ownsTexture)
        {
            _texture = 0;
            _width = 0;
            _height = 0;
        }
        if (_texture == 0)
        {
            _texture = Name;
            _ownsTexture = true;
            GL::BindTexture(GL::TextureTarget::Texture2D, _texture);
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
        }
        else
        {
            GL::BindTexture(GL::TextureTarget::Texture2D, _texture);
        }
        GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, 4);
        if (width != _width || height != _height)
        {
            AdmitInteropTextureStorage(TextureFormat::RGBA8Unorm, width, height);
            GL::TexImage2D(GL::TextureTarget::Texture2D, 0, GL::PixelInternalFormat::Rgba,
                width, height, 0, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, pixels);
            CheckInteropStorageResult("OpenGL launcher overlay allocation");
            _width = width;
            _height = height;
        }
        else
        {
            GL::TexSubImage2D(GL::TextureTarget::Texture2D, 0, 0, 0, width, height,
                GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, pixels);
        }
        GL::BindTexture(GL::TextureTarget::Texture2D, 0);
        ::MphRead::NativeRuntime::GpuTrace::Uploads++;
        ::MphRead::NativeRuntime::GpuTrace::UploadBytes += static_cast<std::int64_t>(width) * height * 4;
    }

    void OpenGlLauncherOverlay::Adopt(std::int32_t texture)
    {
        if (_ownsTexture && _texture != 0)
        {
            GL::DeleteTexture(_texture);
        }
        _texture = texture;
        _width = 0;
        _height = 0;
        _ownsTexture = false;
    }

    void OpenGlLauncherOverlay::Draw(std::int32_t width, std::int32_t height, bool topRowAtTextureZero)
    {
        if (_texture == 0)
        {
            return;
        }
        if (width > 0 && height > 0)
        {
            GL::Viewport(0, 0, width, height);
        }
        const float top = topRowAtTextureZero ? 0.0F : 1.0F;
        const float bottom = topRowAtTextureZero ? 1.0F : 0.0F;
        const std::array<WindowVertex, 4> vertices{{
            {{1, 1, 0}, {1, top}, {1, 0}}, {{-1, 1, 0}, {0, top}, {0, 0}},
            {{1, -1, 0}, {1, bottom}, {1, 1}}, {{-1, -1, 0}, {0, bottom}, {0, 1}}}};
        if (!_draw) _draw = std::make_unique<OpenGlWindowDraw>();
        _draw->Draw(_texture, vertices, width, height, true);
        GL::Color4(1, 1, 1, 1); GL::TexCoord2(0, bottom);
    }

    void OpenGlLauncherOverlay::Clear(std::int32_t width, std::int32_t height)
    {
        GL::Viewport(0, 0, std::max(width, 1), std::max(height, 1));
        GL::ClearColor(0, 0, 0, 1);
        GL::Clear(GL::ClearBufferMask::ColorBufferBit | GL::ClearBufferMask::DepthBufferBit
            | GL::ClearBufferMask::StencilBufferBit);
    }

    void OpenGlLauncherOverlay::Release()
    {
        _draw.reset();
        if (_texture != 0 && _ownsTexture)
        {
            GL::DeleteTexture(_texture);
        }
        _texture = 0;
        _width = 0;
        _height = 0;
        _ownsTexture = false;
    }
}

#endif
