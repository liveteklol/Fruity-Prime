#include "OpenGlLauncherOverlay.hpp"

#include "../../OpenTK/GL.hpp"
#include "../../OpenTK/GpuTrace.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;

    std::int32_t OpenGlLauncherOverlay::_texture = 0;
    std::int32_t OpenGlLauncherOverlay::_vertexBuffer = 0;
    std::int32_t OpenGlLauncherOverlay::_indexBuffer = 0;
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
            _width = width;
            _height = height;
            GL::TexImage2D(GL::TextureTarget::Texture2D, 0, GL::PixelInternalFormat::Rgba,
                width, height, 0, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, pixels);
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
        GL::UseProgram(0);
        GL::Disable(GL::EnableCap::DepthTest);
        GL::Disable(GL::EnableCap::CullFace);
        GL::Disable(GL::EnableCap::AlphaTest);
        GL::Disable(GL::EnableCap::StencilTest);
        GL::Enable(GL::EnableCap::Blend);
        GL::BlendFunc(GL::BlendingFactor::One, GL::BlendingFactor::OneMinusSrcAlpha);
        GL::ActiveTexture(GL::TextureUnit::Texture1);
        GL::BindTexture(GL::TextureTarget::Texture2D, 0);
        GL::Disable(GL::EnableCap::Texture2D);
        GL::ActiveTexture(GL::TextureUnit::Texture0);
        GL::Enable(GL::EnableCap::Texture2D);
        GL::BindTexture(GL::TextureTarget::Texture2D, _texture);
        GL::TexEnv(GL::TextureEnvTarget::TextureEnv, GL::TextureEnvParameter::TextureEnvMode,
            static_cast<std::int32_t>(GL::TextureEnvMode::Replace));
        GL::Color4(1, 1, 1, 1);
        GL::MatrixMode(GL::MatrixMode::Projection);
        GL::PushMatrix();
        GL::LoadIdentity();
        GL::MatrixMode(GL::MatrixMode::Modelview);
        GL::PushMatrix();
        GL::LoadIdentity();
        const float topT = topRowAtTextureZero ? 0.0F : 1.0F;
        const float bottomT = topRowAtTextureZero ? 1.0F : 0.0F;
        struct OverlayVertex final
        {
            float Position[3];
            float TexCoord[2];
        };
        const OverlayVertex vertices[]{
            {{ 1.0F,  1.0F, 0.0F}, {1.0F, topT}},
            {{-1.0F,  1.0F, 0.0F}, {0.0F, topT}},
            {{ 1.0F, -1.0F, 0.0F}, {1.0F, bottomT}},
            {{-1.0F, -1.0F, 0.0F}, {0.0F, bottomT}}
        };
        constexpr std::uint32_t indices[]{0U, 1U, 2U, 3U};
        if (_vertexBuffer == 0)
        {
            _vertexBuffer = GL::GenBuffer();
        }
        if (_indexBuffer == 0)
        {
            _indexBuffer = GL::GenBuffer();
        }
        GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
        GL::BufferData(GL::BufferTarget::ArrayBuffer, sizeof(vertices),
            vertices, GL::BufferUsageHint::StreamDraw);
        GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, _indexBuffer);
        GL::BufferData(GL::BufferTarget::ElementArrayBuffer, sizeof(indices),
            indices, GL::BufferUsageHint::StreamDraw);
        GL::EnableClientState(GL::ClientState::VertexArray);
        GL::VertexPointer(3, GL::PointerType::Float,
            static_cast<std::int32_t>(sizeof(OverlayVertex)),
            reinterpret_cast<const void*>(offsetof(OverlayVertex, Position)));
        GL::ClientActiveTexture(GL::TextureUnit::Texture0);
        GL::EnableClientState(GL::ClientState::TextureCoordArray);
        GL::TexCoordPointer(2, GL::PointerType::Float,
            static_cast<std::int32_t>(sizeof(OverlayVertex)),
            reinterpret_cast<const void*>(offsetof(OverlayVertex, TexCoord)));
        GL::DrawElements(GL::PrimitiveType::TriangleStrip, 4,
            GL::DrawElementsType::UnsignedInt, nullptr);
        GL::DisableClientState(GL::ClientState::TextureCoordArray);
        GL::DisableClientState(GL::ClientState::VertexArray);
        GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
        GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);
        GL::TexCoord2(0.0F, bottomT);
        GL::PopMatrix();
        GL::MatrixMode(GL::MatrixMode::Projection);
        GL::PopMatrix();
        GL::MatrixMode(GL::MatrixMode::Modelview);
        GL::TexEnv(GL::TextureEnvTarget::TextureEnv, GL::TextureEnvParameter::TextureEnvMode,
            static_cast<std::int32_t>(GL::TextureEnvMode::Modulate));
        GL::BindTexture(GL::TextureTarget::Texture2D, 0);
        GL::BlendFunc(GL::BlendingFactor::SrcAlpha, GL::BlendingFactor::OneMinusSrcAlpha);
        GL::Enable(GL::EnableCap::DepthTest);
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
        if (_indexBuffer != 0)
        {
            GL::DeleteBuffer(_indexBuffer);
            _indexBuffer = 0;
        }
        if (_vertexBuffer != 0)
        {
            GL::DeleteBuffer(_vertexBuffer);
            _vertexBuffer = 0;
        }
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
