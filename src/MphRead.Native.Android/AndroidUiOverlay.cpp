#include "AndroidUiOverlay.hpp"

#include "../MphRead.Native/NativeRuntime/System/Console.hpp"
#include "../MphRead.Native/NativeRuntime/System/ExceptionText.hpp"
#include "../MphRead.Native/NativeRuntime/System/Exceptions.hpp"

#include "../MphRead.Native/Mods/Render/SceneWindowUi.hpp"
#include "../MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp"
#include "../MphRead.Native/NativeRuntime/Rhi/WindowUi.hpp"
#include <GLES3/gl3.h>
#include <array>
#include <memory>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr char VertexSource[] = R"glsl(#version 300 es
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main()
{
    // Avalonia's first row is the top of the screen and GL's is the bottom.
    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);
    gl_Position = vec4(a_pos, 0.0, 1.0);
})glsl";

    constexpr char FragmentSource[] = R"glsl(#version 300 es
precision mediump float;
uniform sampler2D u_screen;
in vec2 v_uv;
out vec4 o_colour;
void main()
{
    o_colour = texture(u_screen, v_uv);
})glsl";

    [[nodiscard]] std::string ShaderLog(GLuint shader)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length <= 0)
        {
            return {};
        }
        std::vector<GLchar> buffer(static_cast<std::size_t>(length));
        GLsizei written = 0;
        glGetShaderInfoLog(
            shader,
            length,
            &written,
            buffer.data()
        );
        return std::string(
            buffer.data(),
            static_cast<std::size_t>(std::max<GLsizei>(written, 0))
        );
    }

    [[nodiscard]] std::string ProgramLog(GLuint program)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length <= 0)
        {
            return {};
        }
        std::vector<GLchar> buffer(static_cast<std::size_t>(length));
        GLsizei written = 0;
        glGetProgramInfoLog(
            program,
            length,
            &written,
            buffer.data()
        );
        return std::string(
            buffer.data(),
            static_cast<std::size_t>(std::max<GLsizei>(written, 0))
        );
    }
}

namespace MphRead::Droid
{
    namespace
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;

        // The scene-presented surface's overlay: the UI raster in an RHI texture,
        // composited by the scene device's WindowUi. No GLES context exists.
        struct WindowOverlay final
        {
            std::unique_ptr<Rhi::Texture> Texture;
            std::unique_ptr<Rhi::Sampler> Sampler;
        };

        WindowOverlay& Vk()
        {
            static WindowOverlay overlay;
            return overlay;
        }
    }

    std::int32_t AndroidUiOverlay::_program = 0;
    std::int32_t AndroidUiOverlay::_vao = 0;
    std::int32_t AndroidUiOverlay::_buffer = 0;
    std::int32_t AndroidUiOverlay::_texture = 0;
    std::int32_t AndroidUiOverlay::_width = 0;
    std::int32_t AndroidUiOverlay::_height = 0;
    bool AndroidUiOverlay::_hasFrame = false;
    bool AndroidUiOverlay::_failed = false;
    bool AndroidUiOverlay::_visible = false;

    void AndroidUiOverlay::Visible(bool value) noexcept
    {
        _visible = value;
    }

    bool AndroidUiOverlay::Visible() noexcept
    {
        return _visible;
    }

    void AndroidUiOverlay::Upload(
        std::span<const std::uint8_t> pixels,
        std::int32_t width,
        std::int32_t height
    )
    {
        if (_failed || width <= 0 || height <= 0)
        {
            return;
        }
        const std::size_t required =
            static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height)
            * 4u;
        if (pixels.size() < required)
        {
            return;
        }
        if (::MphRead::Mods::Render::SceneWindowUi::Active())
        {
            auto& gpu = Rhi::SceneDevice();
            auto& texture = Vk().Texture;
            if (!texture || texture->Desc().width != static_cast<std::uint32_t>(width)
                || texture->Desc().height != static_cast<std::uint32_t>(height))
            {
                texture = gpu.CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
                    Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDst});
            }
            gpu.WriteTexture(*texture, Rhi::TextureWrite{static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height), Rhi::TextureFormat::RGBA8Unorm, pixels.data()});
            _width = width;
            _height = height;
            _hasFrame = true;
            return;
        }
        if (!Ensure())
        {
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_texture));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (width != _width || height != _height)
        {
            _width = width;
            _height = height;
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                width,
                height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data()
            );
        }
        else
        {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                width,
                height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data()
            );
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        _hasFrame = true;
    }

    void AndroidUiOverlay::Draw(
        std::int32_t width,
        std::int32_t height
    )
    {
        if (auto* ui = ::MphRead::Mods::Render::SceneWindowUi::Get())
        {
            if (!_visible || !_hasFrame || !Vk().Texture || width <= 0 || height <= 0)
            {
                return;
            }
            if (!Vk().Sampler)
            {
                Rhi::SamplerDesc desc{};
                desc.minFilter = Rhi::Filter::Linear;
                desc.magFilter = Rhi::Filter::Linear;
                desc.addressU = Rhi::SamplerAddressMode::ClampToEdge;
                desc.addressV = Rhi::SamplerAddressMode::ClampToEdge;
                Vk().Sampler = Rhi::SceneDevice().CreateSampler(desc);
            }
            // Avalonia's first row is the top of the screen: t = 0 at the top,
            // as the GLES path's shader has it.
            const std::array<Rhi::WindowQuadVertex, 4> strip{{
                {{ 1.0F,  1.0F}, {1.0F, 0.0F}, {}},
                {{-1.0F,  1.0F}, {0.0F, 0.0F}, {}},
                {{ 1.0F, -1.0F}, {1.0F, 1.0F}, {}},
                {{-1.0F, -1.0F}, {0.0F, 1.0F}, {}}}};
            ui->Begin(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), false);
            ui->DrawTexture(*Vk().Texture, *Vk().Sampler, strip, true);
            ui->End();
            return;
        }
        if (_failed || !_visible || !_hasFrame || _program == 0)
        {
            return;
        }
        if (width > 0 && height > 0)
        {
            glViewport(0, 0, width, height);
        }

        glUseProgram(static_cast<GLuint>(_program));
        glDisable(GL_DEPTH_TEST);
        glDisable(static_cast<GLenum>(CullFace));
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_texture));
        glBindVertexArray(static_cast<GLuint>(_vao));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }

    void AndroidUiOverlay::Release()
    {
        if (::MphRead::Mods::Render::SceneWindowUi::Active())
        {
            Vk().Texture.reset();
            Vk().Sampler.reset();
            ::MphRead::Mods::Render::SceneWindowUi::Release();
            _width = 0;
            _height = 0;
            _hasFrame = false;
            return;
        }
        if (_texture != 0)
        {
            const GLuint texture = static_cast<GLuint>(_texture);
            glDeleteTextures(1, &texture);
        }
        if (_buffer != 0)
        {
            const GLuint buffer = static_cast<GLuint>(_buffer);
            glDeleteBuffers(1, &buffer);
        }
        if (_vao != 0)
        {
            const GLuint vao = static_cast<GLuint>(_vao);
            glDeleteVertexArrays(1, &vao);
        }
        if (_program != 0)
        {
            glDeleteProgram(static_cast<GLuint>(_program));
        }

        _texture = 0;
        _buffer = 0;
        _vao = 0;
        _program = 0;
        _width = 0;
        _height = 0;
        _hasFrame = false;
        _failed = false;
        _visible = false;
    }

    bool AndroidUiOverlay::Ensure()
    {
        if (_program != 0)
        {
            return true;
        }
        try
        {
            _program = Link();
            constexpr float quad[]{
                -1.0f, -1.0f,
                 1.0f, -1.0f,
                -1.0f,  1.0f,
                 1.0f,  1.0f
            };
            GLuint name = 0;
            glGenVertexArrays(1, &name);
            _vao = static_cast<std::int32_t>(name);
            glGenBuffers(1, &name);
            _buffer = static_cast<std::int32_t>(name);
            glBindVertexArray(static_cast<GLuint>(_vao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(_buffer));
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(sizeof(quad)),
                quad,
                GL_STATIC_DRAW
            );
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                0,
                2,
                GL_FLOAT,
                GL_FALSE,
                0,
                nullptr
            );
            glBindVertexArray(0);

            glGenTextures(1, &name);
            _texture = static_cast<std::int32_t>(name);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_texture));
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER,
                GL_LINEAR
            );
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                GL_LINEAR
            );
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_S,
                GL_CLAMP_TO_EDGE
            );
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_T,
                GL_CLAMP_TO_EDGE
            );
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(static_cast<GLuint>(_program));
            glUniform1i(
                glGetUniformLocation(
                    static_cast<GLuint>(_program),
                    "u_screen"
                ),
                0
            );
            glUseProgram(0);
            return true;
        }
        catch (const std::exception& ex)
        {
            _failed = true;
            ::MphRead::NativeRuntime::ConsoleWriteLine(
                "[ui] the overlay could not be set up: "
                + ::MphRead::NativeRuntime::ExceptionToString(ex));
            return false;
        }
    }

    std::int32_t AndroidUiOverlay::Link()
    {
        const std::int32_t vertex = Compile(
            GL_VERTEX_SHADER,
            VertexSource
        );
        const std::int32_t fragment = Compile(
            GL_FRAGMENT_SHADER,
            FragmentSource
        );
        const GLuint program = glCreateProgram();
        glAttachShader(program, static_cast<GLuint>(vertex));
        glAttachShader(program, static_cast<GLuint>(fragment));
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == 0)
        {
            throw ::System::InvalidOperationException(
                "the overlay program would not link: "
                + ProgramLog(program));
        }
        glDeleteShader(static_cast<GLuint>(vertex));
        glDeleteShader(static_cast<GLuint>(fragment));
        return static_cast<std::int32_t>(program);
    }

    std::int32_t AndroidUiOverlay::Compile(
        std::int32_t type,
        const char* source
    )
    {
        const GLuint shader = glCreateShader(static_cast<GLenum>(type));
        const GLchar* shaderSource = source;
        glShaderSource(shader, 1, &shaderSource, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == 0)
        {
            throw ::System::InvalidOperationException(
                "the overlay shader would not compile: "
                + ShaderLog(shader));
        }
        return static_cast<std::int32_t>(shader);
    }
}
