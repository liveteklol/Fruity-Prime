#include "ThumbnailWindowCheck.hpp"

#if defined(MPHREAD_SHELL)

#include "../ScreenCapture.hpp"
#include "../ThumbnailCapture.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../../NativeRuntime/Rhi/BackendSession.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlWindowDraw.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "../../Renderer.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;

#if !defined(__ANDROID__)
    namespace
    {
        void CheckWindowComposite()
        {
            namespace Rhi = NativeRuntime::Rhi;
            auto& device = Rhi::OpenGL::ContextDevice();
            {
                Rhi::TextureDesc desc{};
                desc.width = desc.height = 2;
                desc.format = Rhi::TextureFormat::RGBA8Unorm;
                desc.usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDst;
                auto texture = device.CreateTexture(desc);
                const std::array<std::uint8_t, 16> pixels{
                    128, 0, 0, 128, 128, 0, 0, 128,
                    0, 128, 0, 128, 0, 128, 0, 128};
                device.WriteTexture(*texture, {2, 2, desc.format, pixels.data()});
                const std::array<Rhi::OpenGL::WindowVertex, 4> vertices{{
                    {{1, 1, 0}, {1, 0}, {1, 0}}, {{-1, 1, 0}, {0, 0}, {0, 0}},
                    {{1, -1, 0}, {1, 1}, {1, 1}}, {{-1, -1, 0}, {0, 1}, {0, 1}}}};
                Rhi::OpenGL::OpenGlWindowDraw draw;
                GL::Color4(0.125F, 0.25F, 0.5F, 0.75F);
                for (const bool premultiplied : {true, false})
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, 0);
                    GL::DrawBuffer(GL::DrawBufferMode::Back);
                    GL::ClearColor(0, 0, 1, 1);
                    GL::Clear(GL::ClearBufferMask::ColorBufferBit);
                    draw.Draw(texture->Handle().value, vertices, 64, 64, premultiplied);
                    GL::ReadBuffer(GL::ReadBufferMode::Back);
                    std::array<std::uint8_t, 8> result{};
                    GL::ReadPixels(32, 49, 1, 1, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, result.data());
                    GL::ReadPixels(32, 14, 1, 1, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, result.data() + 4);
                    const int blue = premultiplied ? 127 : 0;
                    if (std::abs(static_cast<int>(result[0]) - 128) > 2 || result[1] > 2
                        || std::abs(static_cast<int>(result[2]) - blue) > 2
                        || result[4] > 2 || std::abs(static_cast<int>(result[5]) - 128) > 2
                        || std::abs(static_cast<int>(result[6]) - blue) > 2)
                        throw System::InvalidOperationException("Window composite blending or texture orientation failed.");
                }
                float color[4]{};
                GL::GetFloat(GL::GetPName::CurrentColor, color);
                if (color[0] != 0.125F || color[1] != 0.25F || color[2] != 0.5F || color[3] != 0.75F)
                    throw System::InvalidOperationException("Window composite changed explicit inherited color.");
            }
            device.TrimCaches();
            device.WaitIdle();
            const auto statistics = device.Statistics();
            if (device.DrainErrors() != 0 || statistics.LiveObjects() != 0 || statistics.Retired != 0)
                throw System::InvalidOperationException("Window composite left graphics errors or resources.");
            NativeRuntime::ConsoleWriteLine("Window composite check passed: premultiplied / opaque, orientation, inherited color, release=0.");
        }
    }
#endif

    int ThumbnailWindowCheck::Run(bool legacyCheck)
    {
        try
        {
            // Use the worker's actual settings in a separate process, before
            // the launcher's context can initialize GLFW or mask a failure.
            RendererPlatform::WindowSettings settings = ThumbnailCapture::WindowSettings(64, 64);
            if (legacyCheck)
            {
                settings.ApiMajor = 2;
                settings.ApiMinor = 1;
                settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Any;
            }
            const std::shared_ptr<RendererPlatform::Window> window = RendererPlatform::CreateWindow(settings);
            auto session = NativeRuntime::Rhi::FindBackendProvider(NativeRuntime::Rhi::GraphicsBackend::OpenGl)->CreateSession({});
            (void)session->Device();
            bool debugSkipped = false;
            ScreenCapture::EnableDebugOutput([&debugSkipped](const std::string& line)
            {
                NativeRuntime::ConsoleWriteLine(line);
                debugSkipped |= line.find("GL debug output unavailable") != std::string::npos;
            });
            NativeRuntime::ConsoleWriteLine(ScreenCapture::DescribeContext());
            if (GL::GetError() != GL::ErrorCode::NoError)
            {
                throw System::InvalidOperationException("Thumbnail diagnostics raised an OpenGL error.");
            }
            if (legacyCheck
                && (!debugSkipped || !GL::GetString(GL::StringName::Version).starts_with("2.1")))
            {
                throw System::InvalidOperationException(
                    "Legacy regression requires GL 2.1 without KHR_debug.");
            }
            (void)window;

            const std::int32_t texture = GL::GenTexture();
            const std::int32_t framebuffer = GL::GenFramebuffer();
            const std::int32_t vertexBuffer = GL::GenBuffer();
            const std::int32_t indexBuffer = GL::GenBuffer();
            try
            {
                GL::BindTexture(GL::TextureTarget::Texture2D, texture);
                GL::TexImage2D(GL::TextureTarget::Texture2D, 0, GL::PixelInternalFormat::Rgba8,
                    64, 64, 0, GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte, nullptr);
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, framebuffer);
                GL::FramebufferTexture2D(GL::FramebufferTarget::Framebuffer,
                    GL::FramebufferAttachment::ColorAttachment0,
                    GL::TextureTarget::Texture2D, texture, 0);
                if (GL::CheckFramebufferStatus(GL::FramebufferTarget::Framebuffer)
                    != GL::FramebufferErrorCode::FramebufferComplete)
                {
                    throw System::InvalidOperationException("Thumbnail framebuffer is incomplete.");
                }
                GL::DrawBuffer(GL::DrawBufferMode::ColorAttachment0);
                GL::ReadBuffer(GL::ReadBufferMode::ColorAttachment0);
                GL::Viewport(0, 0, 64, 64);
                GL::ClearColor(0, 0, 0, 1);
                GL::Clear(GL::ClearBufferMask::ColorBufferBit);
                // Keep the GL 2.1 diagnostic usable, with explicit attributes
                // instead of conventional arrays or fixed-function colour.
                namespace Rhi = MphRead::NativeRuntime::Rhi;
                auto& device = Rhi::OpenGL::ContextDevice();
                const std::string vertexSource = R"glsl(#version 120
attribute vec2 a_position;
attribute vec4 a_color;
varying vec4 color;
void main() { gl_Position = vec4(a_position, 0.0, 1.0); color = a_color; }
)glsl";
                const std::string fragmentSource = R"glsl(#version 120
varying vec4 color;
void main() { gl_FragColor = color; }
)glsl";
                auto vertexShader = Rhi::OpenGL::CreateGlslShader(device, Rhi::ShaderStage::Vertex, vertexSource);
                auto fragmentShader = Rhi::OpenGL::CreateGlslShader(device, Rhi::ShaderStage::Fragment, fragmentSource);
                GL::UseProgram(Rhi::OpenGL::ProgramFor(device, *vertexShader, *fragmentShader));
                constexpr float vertices[]{
                    -1.0F, -1.0F,
                     1.0F, -1.0F,
                     1.0F,  1.0F,
                    -1.0F,  1.0F
                };
                constexpr std::uint32_t indices[]{0U, 1U, 3U, 2U};
                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, vertexBuffer);
                GL::BufferData(GL::BufferTarget::ArrayBuffer, sizeof(vertices),
                    vertices, GL::BufferUsageHint::StaticDraw);
                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, indexBuffer);
                GL::BufferData(GL::BufferTarget::ElementArrayBuffer, sizeof(indices),
                    indices, GL::BufferUsageHint::StaticDraw);
                GL::Color3(1.0F, 0.25F, 0.5F);
                GL::DisableVertexAttribArray(GL::VertexInput::Color);
                GL::EnableVertexAttribArray(GL::VertexInput::Position);
                GL::VertexAttribPointer(GL::VertexInput::Position, 2, GL::PointerType::Float, false, 0, nullptr);
                GL::DrawElements(GL::PrimitiveType::TriangleStrip, 4,
                    GL::DrawElementsType::UnsignedInt, nullptr);
                GL::DisableVertexAttribArray(GL::VertexInput::Position);
                GL::UseProgram(0);
                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);
                std::vector<std::uint8_t> pixel(4);
                GL::ReadPixels(32, 32, 1, 1, GL::PixelFormat::Rgba,
                    GL::PixelType::UnsignedByte, pixel.data());
                if (GL::GetError() != GL::ErrorCode::NoError || pixel[0] < 240
                    || std::abs(static_cast<int>(pixel[1]) - 64) > 4
                    || std::abs(static_cast<int>(pixel[2]) - 128) > 4)
                {
                    throw System::InvalidOperationException("Thumbnail legacy rendering/readback failed.");
                }
            }
            catch (...)
            {
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, 0);
                GL::DeleteBuffer(indexBuffer);
                GL::DeleteBuffer(vertexBuffer);
                GL::DeleteFramebuffer(framebuffer);
                GL::DeleteTexture(texture);
                throw;
            }
            GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, 0);
            GL::DeleteBuffer(indexBuffer);
            GL::DeleteBuffer(vertexBuffer);
            GL::DeleteFramebuffer(framebuffer);
            GL::DeleteTexture(texture);
#if !defined(__ANDROID__)
            if (!legacyCheck) CheckWindowComposite();
#endif
            NativeRuntime::ConsoleWriteLine("Thumbnail window check passed.");
            return 0;
        }
        catch (const std::exception&)
        {
            NativeRuntime::ConsoleErrorWriteLine("[thumbnailwindowcheck] "
                + NativeRuntime::ExceptionToString(std::current_exception()));
            return 1;
        }
    }
}

#endif
