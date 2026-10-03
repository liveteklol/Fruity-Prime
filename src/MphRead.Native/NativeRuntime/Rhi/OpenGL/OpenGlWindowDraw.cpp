#if !defined(__ANDROID__)
#include "OpenGlWindowDraw.hpp"
#include "OpenGlDevice.hpp"
#include "../SceneShaderAbi.hpp"
#include "../BackdropNoise.hpp"
#include "../../OpenTK/GL.hpp"
#include "../../../Shaders.hpp"
#include "../../../Mods/DebugLog.hpp"

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;
    struct OpenGlWindowDraw::Impl final
    {
        Impl() : Device(ContextDevice()), Commands(Device.CreateCommandList())
        {
            const std::string vertex = R"glsl(#version 120
attribute vec3 a_position;
attribute vec2 a_texcoord;
varying vec2 uv;
void main() { gl_Position = vec4(a_position, 1.0); uv = a_texcoord; }
)glsl";
            const std::string fragment = R"glsl(#version 120
uniform sampler2D image;
varying vec2 uv;
void main() { gl_FragColor = texture2D(image, uv); }
)glsl";
            ImageVertex = CreateGlslShader(Device, ShaderStage::Vertex, vertex);
            ImageFragment = CreateGlslShader(Device, ShaderStage::Fragment, fragment);
            Premultiplied = MakePipeline(*ImageVertex, *ImageFragment, true);
            Opaque = MakePipeline(*ImageVertex, *ImageFragment, false);
            BufferDesc buffer{}; buffer.size = 4 * sizeof(WindowVertex);
            buffer.usage = BufferUsage::Vertex | BufferUsage::TransferDst; buffer.memoryUsage = MemoryUsage::CpuToGpu;
            Vertices = Device.CreateBuffer(buffer);
            SamplerDesc sampler{}; sampler.addressU = sampler.addressV = sampler.addressW = SamplerAddressMode::ClampToEdge;
            sampler.maxLod = 0;
            ImageSampler = Device.CreateSampler(sampler);
            Commands->Begin();
        }
        std::unique_ptr<GraphicsPipeline> MakePipeline(const Shader& vs, const Shader& fs, bool blend)
        {
            GraphicsPipelineDesc desc{};
            desc.vertexShader = &vs; desc.fragmentShader = &fs;
            desc.topology = PrimitiveTopology::TriangleStrip;
            desc.vertexBuffers = {{0, sizeof(WindowVertex)}};
            desc.vertexAttributes = {{GL::VertexInput::Position, 0, VertexFormat::Float3, offsetof(WindowVertex, Position)},
                {GL::VertexInput::TexCoord, 0, VertexFormat::Float2, offsetof(WindowVertex, TexCoord)},
                {GL::VertexInput::TexCoord1, 0, VertexFormat::Float2, offsetof(WindowVertex, NoiseCoord)}};
            desc.colorFormats = {TextureFormat::RGBA8Unorm}; desc.rasterizer.cullMode = CullMode::None;
            BlendAttachmentDesc attachment{}; attachment.blendEnable = blend;
            attachment.srcColorFactor = attachment.srcAlphaFactor = BlendFactor::One;
            attachment.dstColorFactor = attachment.dstAlphaFactor = BlendFactor::OneMinusSrcAlpha;
            desc.blendAttachments = {attachment};
            return Device.CreateGraphicsPipeline(desc);
        }
        bool EnsureBackdrop()
        {
            if (BackdropTried) return Backdrop != nullptr;
            BackdropTried = true;
            try
            {
                PhotoVertex = CreateGlslShader(Device, ShaderStage::Vertex, MphRead::Shaders::BackdropVertexShader);
                PhotoFragment = CreateGlslShader(Device, ShaderStage::Fragment, MphRead::Shaders::BackdropFragmentShader);
                Backdrop = MakePipeline(*PhotoVertex, *PhotoFragment, false);
                const auto program = ProgramFor(Device, *PhotoVertex, *PhotoFragment);
                PhotoLocation = GL::GetUniformLocation(program, "photo");
                NoiseLocation = GL::GetUniformLocation(program, "noise_tex");
                StrengthLocation = GL::GetUniformLocation(program, "strength");
                TimeLocation = GL::GetUniformLocation(program, "time");
                WidthLocation = GL::GetUniformLocation(program, "view_width");
                HeightLocation = GL::GetUniformLocation(program, "view_height");
                Noise = CreateBackdropNoiseResources(Device);
            }
            catch (const std::exception& error)
            {
                Mods::DebugLog::Line("ui", std::string("no moving backdrop: ") + error.what());
                Backdrop.reset();
            }
            return Backdrop != nullptr;
        }
        GraphicsDevice& Device;
        std::unique_ptr<CommandList> Commands;
        std::unique_ptr<Shader> ImageVertex, ImageFragment, PhotoVertex, PhotoFragment;
        std::unique_ptr<GraphicsPipeline> Premultiplied, Opaque, Backdrop;
        std::unique_ptr<Buffer> Vertices;
        std::unique_ptr<Rhi::Sampler> ImageSampler;
        BackdropNoiseResources Noise;
        int NoiseLocation = -1;
        bool BackdropTried = false;
        int PhotoLocation = -1, StrengthLocation = -1, TimeLocation = -1, WidthLocation = -1, HeightLocation = -1;
    };
    OpenGlWindowDraw::OpenGlWindowDraw() : _impl(std::make_unique<Impl>()) {}
    OpenGlWindowDraw::~OpenGlWindowDraw() = default;
    void OpenGlWindowDraw::Draw(std::int32_t texture, const std::array<WindowVertex, 4>& vertices,
        std::int32_t width, std::int32_t height, bool premultiplied, bool backdrop, float strength, float seconds)
    {
        auto& state = *_impl;
        backdrop = backdrop && state.EnsureBackdrop();
        state.Device.WriteBuffer(*state.Vertices, 0, std::as_bytes(std::span(vertices)));
        RenderingInfo target{}; target.swapchain = true; target.width = width; target.height = height;
        state.Commands->BeginRendering(target);
        state.Commands->SetViewport({0, 0, static_cast<float>(width), static_cast<float>(height)});
        state.Commands->SetPipeline(backdrop ? *state.Backdrop : premultiplied ? *state.Premultiplied : *state.Opaque);
        if (backdrop)
        {
            constexpr auto photoUnit = SceneShaderAbi::TextureUnit("backdrop", "photo");
            static_assert(photoUnit == 0); // Window photo interop uses logical slot 0.
            GL::Uniform1(state.PhotoLocation, static_cast<std::int32_t>(photoUnit));
            constexpr auto noiseUnit = SceneShaderAbi::TextureUnit("backdrop", "noise_tex");
            static_assert(noiseUnit == 1);
            GL::Uniform1(state.NoiseLocation, static_cast<std::int32_t>(noiseUnit));
            state.Commands->BindSampledTexture(noiseUnit, state.Noise.Texture.get(), state.Noise.Sampler.get());
            GL::Uniform1(state.StrengthLocation, strength);
            GL::Uniform1(state.TimeLocation, seconds); GL::Uniform1(state.WidthLocation, static_cast<float>(width));
            GL::Uniform1(state.HeightLocation, static_cast<float>(height));
        }
        BindInteropTexture(*state.Commands, texture, *state.ImageSampler);
        state.Commands->SetVertexBuffer(0, *state.Vertices); state.Commands->Draw(4);
        state.Commands->EndRendering();
        if (backdrop) state.Commands->BindSampledTexture(1, nullptr, nullptr);
        BindInteropTexture(*state.Commands, 0, *state.ImageSampler);
        GL::UseProgram(0); GL::Enable(GL::EnableCap::DepthTest);
        GL::BlendFunc(GL::BlendingFactor::SrcAlpha, GL::BlendingFactor::OneMinusSrcAlpha);
    }
}

#endif
