#include "OpenGlLauncherPhoto.hpp"
#include "OpenGlDevice.hpp"
#include "../BackdropNoise.hpp"

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
        BackdropNoiseResources backdropNoise;
        std::unique_ptr<CommandList> backdropNoiseCommands;
        std::int32_t noiseUniform = -1;
        struct BufferCleanup final
        {
            std::int32_t Vertex = 0;
            std::int32_t Index = 0;

            ~BufferCleanup()
            {
                try
                {
                    if (Index != 0)
                    {
                        GL::DeleteBuffer(Index);
                    }
                    if (Vertex != 0)
                    {
                        GL::DeleteBuffer(Vertex);
                    }
                }
                catch (...)
                {
                }
            }
        };

        struct ShaderCleanup final
        {
            std::int32_t Vertex = 0;
            std::int32_t Fragment = 0;

            ~ShaderCleanup()
            {
                if (Fragment != 0)
                {
                    GL::DeleteShader(Fragment);
                }
                if (Vertex != 0)
                {
                    GL::DeleteShader(Vertex);
                }
            }
        };

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
    std::int32_t OpenGlLauncherPhoto::_program = 0;
    bool OpenGlLauncherPhoto::_programTried = false;
    std::int32_t OpenGlLauncherPhoto::_photoUniform = -1;
    std::int32_t OpenGlLauncherPhoto::_timeUniform = -1;
    std::int32_t OpenGlLauncherPhoto::_viewWidthUniform = -1;
    std::int32_t OpenGlLauncherPhoto::_viewHeightUniform = -1;
    std::int32_t OpenGlLauncherPhoto::_strengthUniform = -1;

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
        backdropNoiseCommands.reset();
        backdropNoise = {};
        noiseUniform = -1;
        _texture = 0;
        _width = 0;
        _height = 0;
        _tried = false;
        _program = 0;
        _programTried = false;
        _photoUniform = -1;
        _timeUniform = -1;
        _viewWidthUniform = -1;
        _viewHeightUniform = -1;
        _strengthUniform = -1;
    }

    bool OpenGlLauncherPhoto::EnsureProgram()
    {
        if (_programTried)
        {
            return _program != 0;
        }
        _programTried = true;
        ShaderCleanup shaders;
        try
        {
            shaders.Vertex = GL::CreateShader(GL::ShaderType::VertexShader);
            GL::ShaderSource(shaders.Vertex, ::MphRead::Shaders::BackdropVertexShader);
            GL::CompileShader(shaders.Vertex);
            std::int32_t vertexOk = 0;
            GL::GetShader(shaders.Vertex, GL::ShaderParameter::CompileStatus, vertexOk);

            shaders.Fragment = GL::CreateShader(GL::ShaderType::FragmentShader);
            GL::ShaderSource(shaders.Fragment, ::MphRead::Shaders::BackdropFragmentShader);
            GL::CompileShader(shaders.Fragment);
            std::int32_t fragmentOk = 0;
            GL::GetShader(shaders.Fragment, GL::ShaderParameter::CompileStatus, fragmentOk);
            if (vertexOk == 0 || fragmentOk == 0)
            {
                DebugLog::Line("ui", "the moving backdrop's shaders would not compile: "
                    + GL::GetShaderInfoLog(shaders.Vertex) + " " + GL::GetShaderInfoLog(shaders.Fragment));
                return false;
            }

            const std::int32_t program = GL::CreateProgram();
            GL::AttachShader(program, shaders.Vertex);
            GL::AttachShader(program, shaders.Fragment);
            GL::LinkProgram(program);
            GL::DetachShader(program, shaders.Vertex);
            GL::DetachShader(program, shaders.Fragment);
            std::int32_t linked = 0;
            GL::GetProgram(program, GL::GetProgramParameterName::LinkStatus, linked);
            if (linked == 0)
            {
                DebugLog::Line("ui", "the moving backdrop would not link: " + GL::GetProgramInfoLog(program));
                GL::DeleteProgram(program);
                return false;
            }

            _program = program;
            _photoUniform = GL::GetUniformLocation(program, "photo");
            _timeUniform = GL::GetUniformLocation(program, "time");
            _viewWidthUniform = GL::GetUniformLocation(program, "view_width");
            _viewHeightUniform = GL::GetUniformLocation(program, "view_height");
            _strengthUniform = GL::GetUniformLocation(program, "strength");
            noiseUniform = GL::GetUniformLocation(program, "noise_tex");
            backdropNoise = CreateBackdropNoiseResources(ContextDevice());
            backdropNoiseCommands = ContextDevice().CreateCommandList();
            DebugLog::Line("ui", "the moving backdrop is on");
            return true;
        }
        catch (...)
        {
            _program = 0;
            DebugLog::Line("ui", "the moving backdrop could not be set up: "
                + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            return false;
        }
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

        // Generate the moving layer in the fragment shader. CPU work here is
        // scalar uniform setup only: there is no pixel fill or noise upload.
        const bool moving = EnsureProgram();
        GL::UseProgram(moving ? _program : 0);
        GL::Disable(GL::EnableCap::DepthTest);
        GL::Disable(GL::EnableCap::CullFace);
        GL::Disable(GL::EnableCap::AlphaTest);
        GL::Disable(GL::EnableCap::StencilTest);
        GL::Disable(GL::EnableCap::Blend);

        GL::ActiveTexture(GL::TextureUnit::Texture0);
        GL::Enable(GL::EnableCap::Texture2D);
        GL::BindTexture(GL::TextureTarget::Texture2D, _texture);
        GL::TexEnv(GL::TextureEnvTarget::TextureEnv, GL::TextureEnvParameter::TextureEnvMode,
            static_cast<std::int32_t>(GL::TextureEnvMode::Replace));
        GL::Color4(1, 1, 1, 1);
        if (moving)
        {
            const float seconds = static_cast<float>(
                Runtime::TimeSpanTotalMilliseconds(Runtime::StopwatchGetElapsedTicks(BackdropClock())) / 1000.0);
            GL::Uniform1(_photoUniform, 0);
            GL::Uniform1(noiseUniform, 1);
            backdropNoiseCommands->Begin();
            backdropNoiseCommands->BindSampledTexture(1, backdropNoise.Texture.get(), backdropNoise.Sampler.get());
            GL::Uniform1(_strengthUniform, Strength);
            GL::Uniform1(_timeUniform, seconds);
            GL::Uniform1(_viewWidthUniform, static_cast<float>(width));
            GL::Uniform1(_viewHeightUniform, static_cast<float>(height));
        }

        GL::MatrixMode(GL::MatrixMode::Projection);
        GL::PushMatrix();
        GL::LoadIdentity();
        GL::MatrixMode(GL::MatrixMode::Modelview);
        GL::PushMatrix();
        GL::LoadIdentity();

        struct BackdropVertex final
        {
            float Position[3];
            float PhotoCoord[2];
            float NoiseCoord[2];
        };
        const BackdropVertex vertices[]{
            {{ 1.0F,  1.0F, 0.0F}, {u1, v0}, {1.0F, 0.0F}},
            {{-1.0F,  1.0F, 0.0F}, {u0, v0}, {0.0F, 0.0F}},
            {{ 1.0F, -1.0F, 0.0F}, {u1, v1}, {1.0F, 1.0F}},
            {{-1.0F, -1.0F, 0.0F}, {u0, v1}, {0.0F, 1.0F}}
        };
        constexpr std::uint32_t indices[]{0U, 1U, 2U, 3U};
        BufferCleanup buffers{};
        buffers.Vertex = GL::GenBuffer();
        buffers.Index = GL::GenBuffer();
        GL::BindBuffer(GL::BufferTarget::ArrayBuffer, buffers.Vertex);
        GL::BufferData(GL::BufferTarget::ArrayBuffer, sizeof(vertices),
            vertices, GL::BufferUsageHint::StreamDraw);
        GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, buffers.Index);
        GL::BufferData(GL::BufferTarget::ElementArrayBuffer, sizeof(indices),
            indices, GL::BufferUsageHint::StreamDraw);

        // Semantic inputs: the backdrop shader reads them as generic
        // attributes, and GL mirrors them into the conventional arrays for the
        // fixed-function fallback (a photograph with no moving layer).
        namespace VI = GL::VertexInput;
        GL::EnableVertexAttribArray(VI::Position);
        GL::VertexAttribPointer(VI::Position, 3, GL::PointerType::Float, false,
            static_cast<std::int32_t>(sizeof(BackdropVertex)),
            reinterpret_cast<const void*>(offsetof(BackdropVertex, Position)));
        GL::EnableVertexAttribArray(VI::TexCoord);
        GL::VertexAttribPointer(VI::TexCoord, 2, GL::PointerType::Float, false,
            static_cast<std::int32_t>(sizeof(BackdropVertex)),
            reinterpret_cast<const void*>(offsetof(BackdropVertex, PhotoCoord)));
        GL::EnableVertexAttribArray(VI::TexCoord1);
        GL::VertexAttribPointer(VI::TexCoord1, 2, GL::PointerType::Float, false,
            static_cast<std::int32_t>(sizeof(BackdropVertex)),
            reinterpret_cast<const void*>(offsetof(BackdropVertex, NoiseCoord)));

        GL::DrawElements(GL::PrimitiveType::TriangleStrip, 4,
            GL::DrawElementsType::UnsignedInt, nullptr);

        GL::DisableVertexAttribArray(VI::TexCoord1);
        GL::DisableVertexAttribArray(VI::TexCoord);
        GL::DisableVertexAttribArray(VI::Position);
        GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
        GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);
        // Client arrays do not update fixed-function current texture
        // coordinates. Preserve the terminal state of the former immediate path.
        GL::MultiTexCoord2(GL::TextureUnit::Texture0, u0, v1);
        GL::MultiTexCoord2(GL::TextureUnit::Texture1, 0.0F, 1.0F);

        GL::PopMatrix();
        GL::MatrixMode(GL::MatrixMode::Projection);
        GL::PopMatrix();
        GL::MatrixMode(GL::MatrixMode::Modelview);
        GL::TexEnv(GL::TextureEnvTarget::TextureEnv, GL::TextureEnvParameter::TextureEnvMode,
            static_cast<std::int32_t>(GL::TextureEnvMode::Modulate));
        GL::BindTexture(GL::TextureTarget::Texture2D, 0);
        if (moving)
        {
            backdropNoiseCommands->BindSampledTexture(1, nullptr, nullptr);
            backdropNoiseCommands->End();
            GL::UseProgram(0);
        }
        GL::Enable(GL::EnableCap::Blend);
        GL::Enable(GL::EnableCap::DepthTest);
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
            const ::OpenTK::Graphics::OpenGL::ErrorCode uploaded = GL::GetError();
            if (uploaded != ::OpenTK::Graphics::OpenGL::ErrorCode::NoError)
            {
                DebugLog::Line("ui", "backdrop upload said "
                    + ::OpenTK::Graphics::OpenGL::ToString(uploaded));
            }
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
