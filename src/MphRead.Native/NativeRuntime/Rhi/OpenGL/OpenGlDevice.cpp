#include "OpenGlDevice.hpp"
#include "../ResourceStatePolicy.hpp"
#include "OpenGlNative.hpp"
#include "OpenGlFrameScheduler.hpp"
#include "OpenGlDiagnostics.hpp"
#include "OpenGlMemory.hpp"
#include "../../../Testing/MemoryAdmissionCheck.hpp"
#include "../SceneBackend.hpp"
#include "../../../Renderer.hpp"

#include "../../OpenTK/GL.hpp"
#include "../../../Mods/Render/GlNames.hpp"
#include "../../System/Runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace
    {
        namespace GL = ::OpenTK::Graphics::OpenGL::GL;

        constexpr std::int32_t FramebufferBinding = 0x8CA6; // GL_FRAMEBUFFER_BINDING
        // These two enums share their names with the functions that take them,
        // so the type is read off the function's own parameter. (An
        // elaborated `enum X` works on GCC and Clang, and MSVC reads it as
        // redeclaring the scoped enum as an unscoped one.)
        template <std::size_t N, typename F>
        struct ParameterOf;
        template <std::size_t N, typename R, typename... A>
        struct ParameterOf<N, R (*)(A...)>
        {
            using type = std::tuple_element_t<N, std::tuple<A...>>;
        };
        using GlRenderbufferStorage = ParameterOf<1, decltype(&GL::RenderbufferStorage)>::type;
        using GlStencilOp = ParameterOf<0, decltype(&GL::StencilOp)>::type;

        [[nodiscard]] bool IsDepthFormat(TextureFormat format) noexcept
        {
            return format == TextureFormat::D16Unorm || format == TextureFormat::D24UnormS8Uint
                || format == TextureFormat::D32Float || format == TextureFormat::D32FloatS8Uint;
        }

        [[nodiscard]] bool Has(TextureUsage usage, TextureUsage bit) noexcept
        {
            return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(bit)) != 0;
        }

        [[nodiscard]] bool Has(BufferUsage usage, BufferUsage bit) noexcept
        {
            return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(bit)) != 0;
        }

        // A texture format as TexImage2D spells it.
        struct GlTextureFormat final
        {
            GL::PixelInternalFormat Internal;
            GL::PixelFormat Format;
            GL::PixelType Type;
        };

        [[nodiscard]] GlTextureFormat ToGl(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::RGBA8Unorm:
                return {static_cast<GL::PixelInternalFormat>(0x8058), GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte};
            case TextureFormat::RGBA8Srgb:
                return {static_cast<GL::PixelInternalFormat>(0x8C43), GL::PixelFormat::Rgba, GL::PixelType::UnsignedByte};
            case TextureFormat::BGRA8Unorm:
            case TextureFormat::BGRA8Srgb:
                return {static_cast<GL::PixelInternalFormat>(format == TextureFormat::BGRA8Srgb ? 0x8C43 : 0x8058),
                    static_cast<GL::PixelFormat>(0x80E1), GL::PixelType::UnsignedByte};
            case TextureFormat::R8Unorm:
                return {static_cast<GL::PixelInternalFormat>(0x8229), static_cast<GL::PixelFormat>(0x1903), GL::PixelType::UnsignedByte};
            case TextureFormat::RG8Unorm:
                return {static_cast<GL::PixelInternalFormat>(0x822B), static_cast<GL::PixelFormat>(0x8227), GL::PixelType::UnsignedByte};
            case TextureFormat::R16Float:
            case TextureFormat::RG16Float:
            case TextureFormat::RGBA16Float:
                return {static_cast<GL::PixelInternalFormat>(format == TextureFormat::R16Float ? 0x822D
                    : format == TextureFormat::RG16Float ? 0x822F : 0x881A),
                    static_cast<GL::PixelFormat>(format == TextureFormat::R16Float ? 0x1903
                        : format == TextureFormat::RG16Float ? 0x8227 : 0x1908), static_cast<GL::PixelType>(0x140B)};
            case TextureFormat::R32Float:
            case TextureFormat::RG32Float:
            case TextureFormat::RGB32Float:
            case TextureFormat::RGBA32Float:
                return {static_cast<GL::PixelInternalFormat>(format == TextureFormat::R32Float ? 0x822E
                    : format == TextureFormat::RG32Float ? 0x8230 : format == TextureFormat::RGB32Float ? 0x8815 : 0x8814),
                    static_cast<GL::PixelFormat>(format == TextureFormat::R32Float ? 0x1903
                        : format == TextureFormat::RG32Float ? 0x8227 : format == TextureFormat::RGB32Float ? 0x1907 : 0x1908),
                    static_cast<GL::PixelType>(0x1406)};
            case TextureFormat::D16Unorm:
            case TextureFormat::D32Float:
                return {static_cast<GL::PixelInternalFormat>(format == TextureFormat::D16Unorm ? 0x81A5 : 0x8CAC),
                    static_cast<GL::PixelFormat>(0x1902), static_cast<GL::PixelType>(format == TextureFormat::D16Unorm ? 0x1403 : 0x1406)};
            case TextureFormat::RGB8Unorm:
                return {GL::PixelInternalFormat::Rgb, GL::PixelFormat::Rgb, GL::PixelType::UnsignedByte};
            case TextureFormat::D24UnormS8Uint:
                return {GL::PixelInternalFormat::Depth24Stencil8, GL::PixelFormat::DepthStencil,
                    GL::PixelType::UnsignedInt248};
            case TextureFormat::D32FloatS8Uint:
                // GL_DEPTH32F_STENCIL8 / GL_FLOAT_32_UNSIGNED_INT_24_8_REV
                return {static_cast<GL::PixelInternalFormat>(0x8CAD), GL::PixelFormat::DepthStencil,
                    static_cast<GL::PixelType>(0x8DAD)};
            default:
                throw std::invalid_argument("OpenGL RHI: unsupported texture format");
            }
        }

        [[nodiscard]] GlRenderbufferStorage ToGlRenderbuffer(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::D24UnormS8Uint: return GlRenderbufferStorage::Depth24Stencil8;
            case TextureFormat::D32FloatS8Uint: return static_cast<GlRenderbufferStorage>(0x8CAD);
            case TextureFormat::D16Unorm: return static_cast<GlRenderbufferStorage>(0x81A5);
            case TextureFormat::D32Float: return static_cast<GlRenderbufferStorage>(0x8CAC);
            default: throw std::invalid_argument("OpenGL RHI: unsupported renderbuffer format");
            }
        }

        [[nodiscard]] std::int32_t ToGl(Filter filter, bool minification) noexcept
        {
            if (minification)
            {
                return static_cast<std::int32_t>(filter == Filter::Linear
                    ? GL::TextureMinFilter::Linear : GL::TextureMinFilter::Nearest);
            }
            return static_cast<std::int32_t>(filter == Filter::Linear
                ? GL::TextureMagFilter::Linear : GL::TextureMagFilter::Nearest);
        }

        [[nodiscard]] std::int32_t ToGl(SamplerAddressMode mode) noexcept
        {
            switch (mode)
            {
            case SamplerAddressMode::ClampToBorder: return 0x812D;
            case SamplerAddressMode::ClampToEdge:
                return static_cast<std::int32_t>(GL::TextureWrapMode::ClampToEdge);
            case SamplerAddressMode::MirroredRepeat:
                return static_cast<std::int32_t>(GL::TextureWrapMode::MirroredRepeat);
            case SamplerAddressMode::Repeat:
            default:
                return static_cast<std::int32_t>(GL::TextureWrapMode::Repeat);
            }
        }

        void DrainErrors()
        {
            for (std::int32_t i = 0; i < 64 && static_cast<std::int32_t>(GL::GetError()) != 0; ++i)
            {
            }
        }

        class OpenGlGraphicsDevice;
        class OpenGlCommandList;
        class OpenGlTimestampSet final : public TimestampQuerySet
        {
        public:
            OpenGlTimestampSet(OpenGlGraphicsDevice& device, std::uint32_t count, std::string label, std::shared_ptr<void> reservation);
            ~OpenGlTimestampSet() override;
            std::uint32_t Count() const noexcept override { return Writes.Count(); }
            TimestampProperties Properties() const noexcept override { return _properties; }
            TimestampStatus ReadResults(std::span<std::uint64_t> destination) override;
            void Close(bool shutdown = false) noexcept;
            OpenGlGraphicsDevice* Device;
            TimestampWriteState Writes;
            std::array<unsigned, 64> Names{};
            std::string Label;
        private:
            TimestampProperties _properties;
            std::shared_ptr<void> _reservation;
        };
        std::weak_ptr<void> DeviceLifetime(OpenGlGraphicsDevice& device);
        void ForgetPipeline(OpenGlGraphicsDevice& device, const GraphicsPipeline& pipeline);
        void* ContextKey() noexcept
        {
#if defined(__ANDROID__)
            return reinterpret_cast<void*>(eglGetCurrentContext());
#else
            return ::MphRead::RendererPlatform::CurrentGlContext();
#endif
        }
        // Borrowed lookup only; the session is the unique native owner.
        auto& ContextDevices()
        {
            static thread_local std::unordered_map<void*, OpenGlGraphicsDevice*> devices;
            return devices;
        }

        // A GL object waiting in the retirement queue.
        struct GlObject final
        {
            enum class Kind : std::uint8_t { Texture, Renderbuffer, Framebuffer, Buffer, Shader, Program, Sampler, VertexArray, Query };
            Kind What = Kind::Texture;
            std::int32_t Name = 0;
            OpenGlNative::DeleteSamplersType DeleteNames = nullptr;
            std::shared_ptr<void> Reservation;
        };
        using NativeObject = std::pair<OpenGlNative::IsBufferType, unsigned>;

        void DestroyNative(const GlObject& object) noexcept
        {
            switch (object.What)
            {
            case GlObject::Kind::Texture: GL::DeleteTexture(object.Name); break;
            case GlObject::Kind::Renderbuffer: GL::DeleteRenderbuffer(object.Name); break;
            case GlObject::Kind::Framebuffer: GL::DeleteFramebuffer(object.Name); break;
            case GlObject::Kind::Buffer: GL::DeleteBuffer(object.Name); break;
            case GlObject::Kind::Shader: GL::DeleteShader(object.Name); break;
            case GlObject::Kind::Program: GL::DeleteProgram(object.Name); break;
            case GlObject::Kind::Sampler:
            case GlObject::Kind::VertexArray:
            case GlObject::Kind::Query:
            {
                const auto name = static_cast<unsigned>(object.Name);
                object.DeleteNames(1, &name);
                break;
            }
            }
        }

        // GL's own numbers for the RHI's pipeline enums. The wrapper's enums
        // carry only the values upstream used, so these are cast from the
        // registry values directly.
        [[nodiscard]] std::int32_t ToGl(CompareOp op) noexcept
        {
            return 0x0200 + static_cast<std::int32_t>(op); // GL_NEVER..GL_ALWAYS are in RHI order
        }

        [[nodiscard]] std::int32_t ToGl(StencilOp op) noexcept
        {
            switch (op)
            {
            case StencilOp::Zero: return 0;
            case StencilOp::Replace: return 0x1E01;
            case StencilOp::IncrementClamp: return 0x1E02;
            case StencilOp::DecrementClamp: return 0x1E03;
            case StencilOp::Invert: return 0x150A;
            case StencilOp::IncrementWrap: return 0x8507;
            case StencilOp::DecrementWrap: return 0x8508;
            case StencilOp::Keep:
            default: return 0x1E00;
            }
        }

        [[nodiscard]] std::int32_t ToGl(BlendFactor factor) noexcept
        {
            switch (factor)
            {
            case BlendFactor::Zero: return 0;
            case BlendFactor::One: return 1;
            case BlendFactor::SrcColor: return 0x0300;
            case BlendFactor::OneMinusSrcColor: return 0x0301;
            case BlendFactor::SrcAlpha: return 0x0302;
            case BlendFactor::OneMinusSrcAlpha: return 0x0303;
            case BlendFactor::DstAlpha: return 0x0304;
            case BlendFactor::OneMinusDstAlpha: return 0x0305;
            case BlendFactor::DstColor: return 0x0306;
            case BlendFactor::OneMinusDstColor: return 0x0307;
            case BlendFactor::ConstantColor: return 0x8001;
            case BlendFactor::OneMinusConstantColor: return 0x8002;
            case BlendFactor::ConstantAlpha: return 0x8003;
            case BlendFactor::OneMinusConstantAlpha: return 0x8004;
            }
            return 1;
        }

        constexpr std::int32_t CapDepthTest = 0x0B71;
        constexpr std::int32_t CapStencilTest = 0x0B90;
        constexpr std::int32_t CapBlend = 0x0BE2;
        constexpr std::int32_t CapCullFace = 0x0B44;
        constexpr std::int32_t CapPolygonOffsetFill = 0x8037;
        constexpr std::int32_t CurrentProgram = 0x8B8D; // GL_CURRENT_PROGRAM

        void SetCap(std::int32_t cap, bool on)
        {
            if (on)
            {
                GL::Enable(static_cast<GL::EnableCap>(cap));
            }
            else
            {
                GL::Disable(static_cast<GL::EnableCap>(cap));
            }
        }

        struct OpenGlProgramStorage final
        {
            OpenGlProgramStorage(OpenGlGraphicsDevice& device, std::int32_t name);
            ~OpenGlProgramStorage();
            OpenGlGraphicsDevice* Device;
            std::int32_t Name;
            void Detach() noexcept { Device = nullptr; Name = 0; }
        };

        class OpenGlGraphicsPipeline final : public GraphicsPipeline
        {
        public:
            OpenGlGraphicsPipeline(OpenGlGraphicsDevice& device, const GraphicsPipelineDesc& desc, std::shared_ptr<OpenGlProgramStorage> program)
                : _device(&device), _lifetime(DeviceLifetime(device)), _desc(desc), _program(std::move(program))
            {
                _desc.vertexShader = _desc.fragmentShader = nullptr;
            }
            ~OpenGlGraphicsPipeline() override
            { if (!_lifetime.expired()) ForgetPipeline(*_device, *this); }
            [[nodiscard]] const GraphicsPipelineDesc& Desc() const noexcept override { return _desc; }
            // 0: the pipeline leaves the current program alone.
            [[nodiscard]] std::int32_t Program() const noexcept { return _program ? _program->Name : 0; }
            OpenGlGraphicsDevice* Device() const noexcept { return _lifetime.expired() ? nullptr : _device; }

        private:
            OpenGlGraphicsDevice* _device;
            std::weak_ptr<void> _lifetime;
            GraphicsPipelineDesc _desc;
            std::shared_ptr<OpenGlProgramStorage> _program;
        };

        class OpenGlShader final : public Shader
        {
        public:
            OpenGlShader(OpenGlGraphicsDevice& device, ShaderStage stage, std::int32_t name)
                : _device(&device), _name(name)
            {
                _desc.stage = stage;
                _desc.format = ShaderCodeFormat::GlslSource;
            }
            ~OpenGlShader() override;

            [[nodiscard]] const ShaderDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] std::int32_t Name() const noexcept { return _name; }
            OpenGlGraphicsDevice* Device() const noexcept { return _device; }
            void Source(const std::string& source)
            {
                _desc.code.resize(source.size());
                std::memcpy(_desc.code.data(), source.data(), source.size());
            }
            void Detach() noexcept { _device = nullptr; _name = 0; }

        private:
            OpenGlGraphicsDevice* _device;
            ShaderDesc _desc{};
            std::int32_t _name;
        };

        class OpenGlTexture final : public Texture
        {
        public:
            OpenGlTexture(OpenGlGraphicsDevice& device, const TextureDesc& desc,
                std::int32_t name, bool renderbuffer)
                : _device(&device), _desc(desc), _name(name), _renderbuffer(renderbuffer)
            {
            }

            ~OpenGlTexture() override;

            [[nodiscard]] const TextureDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] TextureHandle Handle() const noexcept override
            {
                return _renderbuffer ? TextureHandle{} : TextureHandle{_name};
            }

            [[nodiscard]] std::int32_t Name() const noexcept { return _name; }
            [[nodiscard]] bool IsRenderbuffer() const noexcept { return _renderbuffer; }
            OpenGlGraphicsDevice* Device() const noexcept { return _device; }
            [[nodiscard]] bool HasStorage() const noexcept { return _hasStorage; }
            [[nodiscard]] ResourceState State() const noexcept { return _state; }
            void State(ResourceState state) noexcept { _state = state; }
            [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
            void SetExtent(std::uint32_t width, std::uint32_t height) noexcept
            {
                _desc.width = width;
                _desc.height = height;
                _hasStorage = true;
            }
            void Detach() noexcept { _device = nullptr; _name = 0; }

        private:
            OpenGlGraphicsDevice* _device;
            TextureDesc _desc;
            std::int32_t _name;
            bool _renderbuffer;
            bool _hasStorage = false;
            ResourceState _state = ResourceState::Undefined;
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
        };

        class OpenGlTextureView final : public TextureView
        {
        public:
            OpenGlTextureView(OpenGlTexture& texture, const TextureViewDesc& desc)
                : _texture(texture), _desc(desc), _textureLifetime(texture.Lifetime())
            {
            }

            [[nodiscard]] const TextureViewDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] const Texture& TextureResource() const noexcept override { return _texture; }
            [[nodiscard]] bool TextureAlive() const noexcept { return !_textureLifetime.expired(); }
            [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }

        private:
            Texture& _texture;
            TextureViewDesc _desc;
            std::weak_ptr<void> _textureLifetime;
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
        };

        #include "OpenGlResourcesInternal.inc"

        [[nodiscard]] const OpenGlTexture& Native(const Texture& texture, const OpenGlGraphicsDevice* device = nullptr)
        {
            const auto* native = dynamic_cast<const OpenGlTexture*>(&texture);
            if (!native || !native->Device() || (device && native->Device() != device))
                throw std::invalid_argument("OpenGL RHI: texture belongs to another or closed session.");
            return *native;
        }

        [[nodiscard]] OpenGlTexture& Native(Texture& texture, const OpenGlGraphicsDevice* device = nullptr)
        {
            return const_cast<OpenGlTexture&>(Native(static_cast<const Texture&>(texture), device));
        }

        const OpenGlTexture& ViewTexture(const TextureView& view, const OpenGlGraphicsDevice* device)
        {
            const auto* native = dynamic_cast<const OpenGlTextureView*>(&view);
            if (!native || !native->TextureAlive()) throw std::invalid_argument("OpenGL RHI: texture view is foreign or expired.");
            return Native(native->TextureResource(), device);
        }
        void ValidateTargets(const RenderingInfo& info, const OpenGlGraphicsDevice* device)
        {
            for (const auto& attachment : info.colorAttachments)
                if (attachment.view) (void)ViewTexture(*attachment.view, device);
            if (info.depthStencilAttachment && info.depthStencilAttachment->view)
                (void)ViewTexture(*info.depthStencilAttachment->view, device);
        }

        // Attach the rendering info's targets to the framebuffer bound for drawing.
        void AttachTargets(const RenderingInfo& info)
        {
            if (!info.colorAttachments.empty() && info.colorAttachments[0].view != nullptr)
            {
                const OpenGlTexture& color = Native(info.colorAttachments[0].view->TextureResource());
                GL::FramebufferTexture2D(GL::FramebufferTarget::Framebuffer,
                    GL::FramebufferAttachment::ColorAttachment0, GL::TextureTarget::Texture2D,
                    color.Name(), 0);
            }
            if (info.depthStencilAttachment != nullptr && info.depthStencilAttachment->view != nullptr)
            {
                const OpenGlTexture& depth = Native(info.depthStencilAttachment->view->TextureResource());
                if (depth.IsRenderbuffer())
                {
                    GL::FramebufferRenderbuffer(GL::FramebufferTarget::Framebuffer,
                        GL::FramebufferAttachment::DepthStencilAttachment,
                        GL::RenderbufferTarget::Renderbuffer, depth.Name());
                }
                else
                {
                    GL::FramebufferTexture2D(GL::FramebufferTarget::Framebuffer,
                        GL::FramebufferAttachment::DepthStencilAttachment, GL::TextureTarget::Texture2D,
                        depth.Name(), 0);
                }
            }
        }

        struct FramebufferKey final
        {
            const Texture* Color = nullptr;
            const Texture* Depth = nullptr;
            bool operator==(const FramebufferKey&) const = default;
        };

        struct FramebufferKeyHash final
        {
            std::size_t operator()(const FramebufferKey& key) const noexcept
            {
                return std::hash<const void*>()(key.Color) * 31U ^ std::hash<const void*>()(key.Depth);
            }
        };

        [[nodiscard]] FramebufferKey KeyOf(const RenderingInfo& info) noexcept
        {
            FramebufferKey key{};
            if (!info.colorAttachments.empty() && info.colorAttachments[0].view != nullptr)
            {
                key.Color = &info.colorAttachments[0].view->TextureResource();
            }
            if (info.depthStencilAttachment != nullptr && info.depthStencilAttachment->view != nullptr)
            {
                key.Depth = &info.depthStencilAttachment->view->TextureResource();
            }
            return key;
        }

        OpenGlFrameScheduler::Dispatch SchedulerDispatch(OpenGlNative& api)
        {
            const auto version = GL::GetString(GL::StringName::Version);
            const auto start = version.find_first_of("0123456789");
            const auto dot = version.find('.', start);
            const int major = start == std::string::npos ? 0 : std::stoi(version.substr(start));
            const int minor = dot == std::string::npos ? 0 : std::stoi(version.substr(dot + 1));
            const bool coreSync = version.find("OpenGL ES") != std::string::npos ? major >= 3
                : major > 3 || (major == 3 && minor >= 2);
            const bool sync = coreSync
                || GL::GetString(static_cast<GL::StringName>(0x1F03)).find("GL_ARB_sync") != std::string::npos;
            if (sync && (!api.FenceSync || !api.ClientWaitSync || !api.DeleteSync || !api.Flush))
                throw BackendError(GraphicsBackend::OpenGl, BackendErrorKind::Unsupported, 0,
                    "OpenGL context did not expose its required synchronization entry points.");
            return {&api,
                [](void* context) { return static_cast<OpenGlNative*>(context)->FenceSync(0x9117, 0); },
                [](void* context, void* fence, bool flush, std::uint64_t timeout) {
                    return static_cast<OpenGlFrameScheduler::WaitStatus>(static_cast<OpenGlNative*>(context)->ClientWaitSync(fence, flush ? 1 : 0, timeout));
                },
                [](void* context, void* fence) { static_cast<OpenGlNative*>(context)->DeleteSync(fence); },
                [](void* context) { OpenGlNative::Require(static_cast<OpenGlNative*>(context)->Flush, "glFlush")(); },
                [](void*) { GL::Finish(); }, [](void*) { return static_cast<unsigned>(GL::GetError()); }, sync};
        }

        class OpenGlGraphicsDevice final : public GraphicsDevice
        {
        public:
            OpenGlGraphicsDevice() : _scheduler(SchedulerDispatch(_api)), _contextKey(ContextKey())
            {
                _capabilities.backend = GraphicsBackend::OpenGl;
                _capabilities.maxColorAttachments = 1;
                _capabilities.supportsWireframe = true;
                _capabilities.maxTexture2DDimension = static_cast<std::uint32_t>(GL::GetInteger(0x0D33));
                _capabilities.maxVertexBuffers = static_cast<std::uint32_t>(GL::GetInteger(0x8869));
                _capabilities.maxTextureArrayLayers = 1;
                _capabilities.maxTextureMipLevels = 1;
                _capabilities.maxTexture3DDimension = 0;
                _capabilities.maxBindingGroups = 4;
#if !defined(__ANDROID__)
                const auto version = GL::GetString(GL::StringName::Version);
                const auto diagnosticExtensions = GL::GetString(static_cast<GL::StringName>(0x1F03));
                const int major = version.empty() ? 0 : version[0] - '0';
                const int minor = version.size() < 3 ? 0 : version[2] - '0';
                const bool timer = major > 3 || (major == 3 && minor >= 3) || diagnosticExtensions.find("GL_ARB_timer_query") != std::string::npos;
                const bool debug = major > 4 || (major == 4 && minor >= 3) || diagnosticExtensions.find("GL_KHR_debug") != std::string::npos;
                _capabilities.supportsDebugLabels = debug && _api.PushDebugGroup && _api.PopDebugGroup && _api.DebugMessageInsert;
                if (timer && _api.GenQueries && _api.DeleteQueries && _api.QueryCounter && _api.GetQueryiv
                    && _api.GetQueryObjectiv && _api.GetQueryObjectui64v)
                {
                    int bits = 0; _api.GetQueryiv(0x8E28, 0x8864, &bits); // TIMESTAMP / COUNTER_BITS
                    CheckStorageResult("glGetQueryiv(timestamp bits)");
                    _timestampProperties = {static_cast<std::uint32_t>(bits), 1.0};
                    _capabilities.supportsTimestampQueries = bits > 0 && bits <= 64;
                }
#endif
#if !defined(__ANDROID__)
                _capabilities.supportsDepthClamp = true;
                _capabilities.supportsAnisotropy = GL::GetString(static_cast<GL::StringName>(0x1F03)).find("texture_filter_anisotropic") != std::string::npos;
                const auto extensions = " " + GL::GetString(static_cast<GL::StringName>(0x1F03)) + " ";
                _memory = OpenGlMemory(extensions.find(" GL_NVX_gpu_memory_info ") != std::string::npos,
                    [](std::int32_t name) { return GL::GetInteger(name); });
                if (_capabilities.supportsAnisotropy)
                {
                    float maximum = 1;
                    GL::GetFloat(static_cast<GL::GetPName>(0x84FF), &maximum);
                    _capabilities.maxSamplerAnisotropy = maximum;
                }
#endif
            }

            ~OpenGlGraphicsDevice() override;
            void CloseNative();
            std::function<void()> NativeReleaseCheck();
            void CheckMemoryAdmission();
            std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }

            [[nodiscard]] GraphicsBackend GetBackend() const noexcept override { return GraphicsBackend::OpenGl; }
            [[nodiscard]] const Capabilities& GetCapabilities() const noexcept override { return _capabilities; }
            [[nodiscard]] MemoryBudgetSnapshot MemoryBudget() const override { return _memory.Snapshot(_reservedStorage); }
            [[nodiscard]] MemoryTelemetry MemoryUsageTelemetry() const override { return _memory.Telemetry(); }
            std::unique_ptr<TimestampQuerySet> CreateTimestampQuerySet(std::uint32_t count, std::string_view label) override
            {
                TimestampWriteState validate(count); ValidateDebugLabel({std::string(label)});
                if (!_capabilities.supportsTimestampQueries) return {};
                auto reservation = _timestampBudget->TryReserve();
                if (!reservation) return {};
                return std::make_unique<OpenGlTimestampSet>(*this, count, std::string(label), std::move(reservation));
            }
            TimestampProperties TimestampInfo() const noexcept { return _timestampProperties; }
            void Register(OpenGlTimestampSet& set) { _timestampSets.insert(&set); }
            void Unregister(OpenGlTimestampSet& set) noexcept { _timestampSets.erase(&set); }
            void AdmitStorage(std::uint64_t bytes) { _memory.Admit(bytes, _reservedStorage); }
            void CheckStorageResult(const char* operation)
            {
                try { _memory.CheckNativeResult(static_cast<int>(GL::GetError()), operation); }
                catch (const BackendError& error)
                {
                    if (error.Kind() == BackendErrorKind::DeviceLost)
                        _scheduler.NoteDeviceLost(std::current_exception());
                    throw;
                }
            }
            std::array<std::array<float, 4>, VertexSemanticCount> CurrentAttributes{{
                {0, 0, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}}};

            [[nodiscard]] std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
            void WriteBuffer(Buffer& buffer, std::uint64_t offset, std::span<const std::byte> data) override;
            void ReadBuffer(Buffer& buffer, std::uint64_t offset, std::span<std::byte> data) override;
            bool SupportsAsyncReadback() const noexcept override
            {
#if defined(__ANDROID__)
                return false;
#else
                return _api.FenceSync && _api.ClientWaitSync && _api.MapBufferRange && _api.UnmapBuffer;
#endif
            }
            ReadbackTicket EnqueueReadback(Buffer&, std::uint64_t, std::uint64_t) override;
            ReadbackTicket EnqueueBytes(std::uint64_t, const std::function<void(Buffer&)>&);
            bool ReadbackReady(SubmissionSerial serial) { return _scheduler.Poll() >= serial; }
            void PollReadbacks() override { _readbacks.Poll(); }
            void SetReadbackLimits(ReadbackLimits limits) override { _readbacks.SetLimits(limits); }
            ReadbackUsage ReadbackStatistics() const override { return _readbacks.Usage(); }
            OpenGlNative& Api() noexcept { return _api; }
            void Track(OpenGlBuffer& buffer)
            { if (_resourceBuffers.insert(&buffer).second) _reservedStorage += buffer.Desc().size; }
            void Untrack(OpenGlBuffer& buffer)
            { if (_resourceBuffers.erase(&buffer)) _reservedStorage -= buffer.Desc().size; }
            void Track(OpenGlSamplerStorage& sampler) { _samplers.insert(&sampler); }
            void Untrack(OpenGlSamplerStorage& sampler) { _samplers.erase(&sampler); }
            void Track(OpenGlProgramStorage& program) { _livePrograms.insert(&program); }
            void Untrack(OpenGlProgramStorage& program) { _livePrograms.erase(&program); }
            void ForgetProgram(std::int32_t program);
            void ForgetPipeline(const GraphicsPipeline& pipeline);

            [[nodiscard]] std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override
            {
                ValidateTexture(desc);
                const bool renderbuffer = IsDepthFormat(desc.format)
                    && !Has(desc.usage, TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst);
                const std::int32_t name = renderbuffer ? GL::GenRenderbuffer()
                    : ::MphRead::Mods::Render::GlNames::NextTexture();
                return Make(desc, name, renderbuffer);
            }

            [[nodiscard]] std::unique_ptr<Texture> CreateTexture(
                const TextureDesc& desc, TextureHandle handle) override
            {
                ValidateTexture(desc);
                if (!handle || IsDepthFormat(desc.format))
                {
                    throw std::invalid_argument("OpenGL RHI: a chosen handle must be a nonzero colour texture");
                }
                if (_byHandle.contains(handle.value))
                {
                    throw std::invalid_argument("OpenGL RHI: texture handle already live");
                }
                // The name may be waiting to be deleted from its last life; it is
                // this texture now, so it must not be.
                _retired.Cancel([&handle](const GlObject& object)
                {
                    return object.What == GlObject::Kind::Texture && object.Name == handle.value;
                });
                return Make(desc, handle.value, false);
            }

            [[nodiscard]] Texture* FindTexture(TextureHandle handle) noexcept override
            {
                const auto found = _byHandle.find(handle.value);
                return found == _byHandle.end() ? nullptr : found->second;
            }

            Texture& RetainTexture(std::unique_ptr<Texture> texture) override
            {
                if (!texture) throw std::invalid_argument("OpenGL RHI: cannot retain an empty texture.");
                (void)Native(*texture, this);
                Texture& kept = *texture;
                _retained.push_back(std::move(texture));
                return kept;
            }

            [[nodiscard]] std::unique_ptr<TextureView> CreateTextureView(
                Texture& texture, const TextureViewDesc& desc) override
            {
                auto& native = Native(texture, this);
                if (native.Device() != this || (desc.format != TextureFormat::Undefined && desc.format != texture.Desc().format)
                    || desc.baseMipLevel || desc.mipLevelCount != 1 || desc.baseArrayLayer || desc.arrayLayerCount != 1
                    || (!native.IsRenderbuffer() && FindTexture(native.Handle()) != &texture))
                    throw std::invalid_argument("OpenGL RHI: invalid texture view.");
                auto normalized = desc; normalized.format = texture.Desc().format;
                return std::make_unique<OpenGlTextureView>(native, normalized);
            }

            [[nodiscard]] std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) override
            {
                for (auto it = _samplerCache.begin(); it != _samplerCache.end();)
                {
                    if (const auto storage = it->lock())
                    {
                        if (storage->Desc == desc) return std::make_unique<OpenGlSampler>(storage);
                        ++it;
                    }
                    else it = _samplerCache.erase(it);
                }
                auto storage = std::make_shared<OpenGlSamplerStorage>(*this, desc);
                _samplerCache.push_back(storage);
                return std::make_unique<OpenGlSampler>(std::move(storage));
            }

            // Shaders come from GLSL source here (CreateGlslShader): the
            // renderer's programs are GLSL, and SPIR-V is the Vulkan backend's.
            [[nodiscard]] std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) override
            {
                if (desc.format != ShaderCodeFormat::GlslSource || desc.entryPoint != "main"
                    || (desc.stage != ShaderStage::Vertex && desc.stage != ShaderStage::Fragment)
                    || desc.code.empty())
                    throw std::invalid_argument("OpenGL RHI: expected GLSL source with a main entry point.");
                const std::string source(reinterpret_cast<const char*>(desc.code.data()), desc.code.size());
                return CreateGlsl(desc.stage, source);
            }

            [[nodiscard]] std::unique_ptr<Shader> CreateGlsl(ShaderStage stage, const std::string& source)
            {
                const std::int32_t name = GL::CreateShader(stage == ShaderStage::Vertex
                    ? GL::ShaderType::VertexShader : GL::ShaderType::FragmentShader);
                GL::ShaderSource(name, source);
                GL::CompileShader(name);
                std::int32_t status = 0;
                GL::GetShader(name, GL::ShaderParameter::CompileStatus, status);
                if (::MphRead::NativeRuntime::DebuggerAttached() && !GL::GetShaderInfoLog(name).empty())
                {
                    ::MphRead::NativeRuntime::DebuggerBreak();
                }
                if (status == 0)
                {
                    const std::string log = GL::GetShaderInfoLog(name);
                    GL::DeleteShader(name);
                    throw std::runtime_error(log);
                }
                auto shader = std::make_unique<OpenGlShader>(*this, stage, name);
                shader->Source(source);
                _shaders.insert(shader.get());
                return shader;
            }

            [[nodiscard]] std::shared_ptr<OpenGlProgramStorage> Program(const Shader& vertex, const Shader& fragment)
            {
                const auto* vs = dynamic_cast<const OpenGlShader*>(&vertex);
                const auto* fs = dynamic_cast<const OpenGlShader*>(&fragment);
                if (!vs || !fs || vs->Device() != this || fs->Device() != this
                    || vertex.Desc().stage != ShaderStage::Vertex || fragment.Desc().stage != ShaderStage::Fragment)
                    throw std::invalid_argument("OpenGL RHI: invalid shader pair.");
                const auto key = std::make_pair(&vertex, &fragment);
                const auto found = _programs.find(key);
                if (found != _programs.end())
                {
                    return found->second;
                }
                const std::int32_t program = GL::CreateProgram();
                GL::AttachShader(program, static_cast<const OpenGlShader&>(vertex).Name());
                GL::AttachShader(program, static_cast<const OpenGlShader&>(fragment).Name());
                GL::LinkProgram(program);
                std::int32_t linked = 0;
                GL::GetProgram(program, GL::GetProgramParameterName::LinkStatus, linked);
                if (!linked)
                {
                    const auto log = GL::GetProgramInfoLog(program);
                    GL::DeleteProgram(program);
                    throw std::runtime_error(log);
                }
                // The linked executable is independent of public shader
                // wrappers. Pipelines keep the executable alive themselves.
                GL::DetachShader(program, vs->Name());
                GL::DetachShader(program, fs->Name());
                std::shared_ptr<OpenGlProgramStorage> storage;
                try
                {
                    storage = std::make_shared<OpenGlProgramStorage>(*this, program);
                    _programs.emplace(key, storage);
                    return storage;
                }
                catch (...) { if (!storage) GL::DeleteProgram(program); throw; }
            }

            // Drop cache entries that borrow this shader's identity. Pipelines
            // retain their independently linked executables until release.
            void Forget(const OpenGlShader& shader) noexcept
            {
                for (auto it = _programs.begin(); it != _programs.end();)
                {
                    if (it->first.first == &shader || it->first.second == &shader)
                    {
                        it = _programs.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                _shaders.erase(const_cast<OpenGlShader*>(&shader));
            }

            FrameContext BeginFrame() override
            {
                if (_frameOpen) EndFrame();
                ++_frame;
                const auto slot = static_cast<std::size_t>(_frame % FramesInFlight);
                _scheduler.Wait(_slots[slot].Serial);
                CollectCompleted();
                _frameOpen = true;
                _readbacks.Poll();
                return {_frame, static_cast<std::uint32_t>(slot)};
            }

            void EndFrame() override
            {
                if (!_frameOpen) return;
                // This marker covers the entire context stream, including the
                // window compositor. A frame number never acts as completion.
                const auto serial = _scheduler.Submit(true);
                _slots[_frame % FramesInFlight] = {_frame, serial};
                _frameOpen = false;
                CollectCompleted();
                _readbacks.Poll();
            }

            void WaitIdle() override
            {
                _scheduler.Finish();
                _frameOpen = false;
                _completedFrame = _frame;
                _retired.Collect(_scheduler.Completed(), DestroyNative);
            }

            void SubmitCommands() { (void)_scheduler.Submit(true); }
            [[nodiscard]] LowLatencyCapabilities LowLatencyCaps() const noexcept override
            { return {true, false, LowLatencyProvider::Generic}; }
            [[nodiscard]] bool WaitForLatestSubmission(std::uint64_t timeoutNanoseconds) override
            { return _scheduler.WaitForLatest(timeoutNanoseconds); }
            [[nodiscard]] PresentationWaitStatistics PresentationWaits() const noexcept override
            { return _scheduler.PresentationWaits(); }

            [[nodiscard]] GpuResourceStatistics Statistics() const override;

            void Retire(const GlObject& object)
            {
                if (object.What == GlObject::Kind::Buffer)
                {
                    ForgetBuffer(object.Name);
                    _buffers.erase(object.Name);
                }
                // Conservatively capture the actual stream here, so unframed
                // uploads and native/Skia work are covered without assuming a
                // frame or relying on an adapter to announce every use.
                _retired.Retire(object, _scheduler.SubmitForRetirement());
            }

            std::int32_t CreateGeometryBuffer()
            {
                const auto name = GL::GenBuffer();
                if (name != 0) _buffers.insert(name);
                return name;
            }

            [[nodiscard]] std::string AdapterDescription() override
            {
                return "vendor=" + GL::GetString(GL::StringName::Vendor)
                    + "\nrenderer=" + GL::GetString(GL::StringName::Renderer)
                    + "\nversion=" + GL::GetString(GL::StringName::Version)
                    + "\nshading language=" + GL::GetString(GL::StringName::ShadingLanguageVersion);
            }

            [[nodiscard]] std::int32_t DrainErrors() override
            {
                std::int32_t first = 0;
                for (std::int32_t i = 0; i < 64; ++i)
                {
                    const auto code = static_cast<std::int32_t>(GL::GetError());
                    if (code == 0)
                    {
                        break;
                    }
                    if (first == 0)
                    {
                        first = code;
                    }
                }
                return first;
            }

            [[nodiscard]] std::unique_ptr<BindingLayout> CreateBindingLayout(const BindingLayoutDesc& desc) override
            {
                return std::make_unique<OpenGlBindingLayout>(*this, desc);
            }

            [[nodiscard]] std::unique_ptr<BindingSet> CreateBindingSet(const BindingSetDesc& desc) override
            {
                return std::make_unique<OpenGlBindingSet>(*this, desc);
            }

            // The shaders, binding layout and vertex layout in the desc are
            // for backends that bake them in; OpenGL takes the program and the
            // arrays from what is bound, and applies the fixed state here.
            [[nodiscard]] std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline(
                const GraphicsPipelineDesc& desc) override
            {
                if (desc.sampleCount != 1 || desc.colorFormats.size() > 1 || desc.blendAttachments.size() > 1
                    || desc.pipelineLayout.groups.size() > _capabilities.maxBindingGroups
                    || !std::isfinite(desc.rasterizer.lineWidth) || desc.rasterizer.lineWidth <= 0
                    || !std::isfinite(desc.rasterizer.depthBiasSlope) || !std::isfinite(desc.rasterizer.depthBiasConstant))
                    throw std::invalid_argument("OpenGL RHI: unsupported or invalid graphics pipeline.");
                auto program = desc.vertexShader != nullptr && desc.fragmentShader != nullptr
                    ? Program(*desc.vertexShader, *desc.fragmentShader) : nullptr;
                return std::make_unique<OpenGlGraphicsPipeline>(*this, desc, program);
            }

            [[nodiscard]] std::unique_ptr<CommandList> CreateCommandList() override;

            void WriteTexture(Texture& texture, const TextureWrite& write) override
            {
                InvalidateDrawState();
                OpenGlTexture& gl = Native(texture, this);
                if (FindTexture(gl.Handle()) != &texture || !write.width || !write.height
                    || write.width > _capabilities.maxTexture2DDimension || write.height > _capabilities.maxTexture2DDimension)
                    throw std::invalid_argument("OpenGL RHI: invalid texture upload extent or ownership.");
                if (!gl.HasStorage() || gl.Desc().width != write.width || gl.Desc().height != write.height || !write.data)
                    AllocateStorage(gl, write.width, write.height);
                if (!write.data) return;
                const GlTextureFormat format = ToGl(write.format);
                GL::BindTexture(GL::TextureTarget::Texture2D, gl.Name());
                const auto alignment = GL::GetInteger(0x0CF5);
                GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, 1);
                GL::TexSubImage2D(GL::TextureTarget::Texture2D, 0, 0, 0, write.width, write.height,
                    format.Format, format.Type, write.data);
                GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, alignment);
                GL::BindTexture(GL::TextureTarget::Texture2D, 0);
                gl.SetExtent(write.width, write.height);
                GL::BindTexture(GL::TextureTarget::Texture2D, gl.Name());
                GL::TexParameter(GL::TextureTarget::Texture2D, static_cast<GL::TextureParameterName>(0x813D), 0);
                GL::BindTexture(GL::TextureTarget::Texture2D, 0);
                const auto previous = gl.State();
                gl.State(previous != ResourceState::Undefined && previous != ResourceState::CopyDst ? previous
                    : (Has(gl.Desc().usage, TextureUsage::Sampled) ? ResourceState::ShaderRead
                        : (Has(gl.Desc().usage, TextureUsage::ColorAttachment) ? ResourceState::ColorAttachment : ResourceState::Common)));
            }

            void ResizeTexture(Texture& texture, std::uint32_t width, std::uint32_t height) override
            {
                OpenGlTexture& gl = Native(texture, this);
                if (gl.Device() != this) throw std::invalid_argument("OpenGL RHI: texture belongs to another session.");
                if (!width || !height || width > _capabilities.maxTexture2DDimension || height > _capabilities.maxTexture2DDimension)
                    throw std::out_of_range("OpenGL RHI: invalid texture resize extent.");
                if (gl.HasStorage() && gl.Desc().width == width && gl.Desc().height == height) return;
                AllocateStorage(gl, width, height);
            }

            [[nodiscard]] bool CanRender(const RenderingInfo& info) override
            {
                bool complete = false;
                WithScratchFramebuffer(info, [&]
                {
                    complete = GL::CheckFramebufferStatus(GL::FramebufferTarget::Framebuffer)
                        == GL::FramebufferErrorCode::FramebufferComplete;
                });
                return complete;
            }

            [[nodiscard]] std::uint32_t DepthBits(const RenderingInfo& info) override
            {
                std::uint32_t bits = 0;
                WithScratchFramebuffer(info, [&]
                {
                    try
                    {
                        DrainErrors();
                        std::int32_t answer = 0;
                        GL::GetFramebufferAttachmentParameter(GL::FramebufferTarget::Framebuffer,
                            GL::FramebufferAttachment::DepthAttachment,
                            GL::FramebufferParameterName::FramebufferAttachmentDepthSize, answer);
                        if (static_cast<std::int32_t>(GL::GetError()) == 0 && answer >= 8 && answer <= 32)
                        {
                            bits = static_cast<std::uint32_t>(answer);
                        }
                    }
                    catch (...)
                    {
                        bits = 0;
                    }
                });
                return bits;
            }

            // Called by a texture as it is destroyed.
            void Forget(OpenGlTexture& texture) noexcept;

            void Register(OpenGlCommandList& list) { _lists.insert(&list); }
            void Unregister(OpenGlCommandList& list) noexcept
            { _lists.erase(&list); if (_nativeOwner == &list) _nativeOwner = nullptr; }
            bool Activate(OpenGlCommandList& list) noexcept
            { return std::exchange(_nativeOwner, &list) != &list; }
            void InvalidateDrawState() noexcept { _nativeOwner = nullptr; }
            void ForgetBuffer(std::int32_t name);

            // Storage for a render target: TexImage2D with no data, or a
            // renderbuffer's storage, at this extent.
            void AllocateStorage(OpenGlTexture& gl, std::uint32_t width, std::uint32_t height)
            {
                InvalidateDrawState();
                const auto before = gl.HasStorage() ? TextureStorageEstimate(gl.Desc().format, gl.Desc().width, gl.Desc().height) : 0;
                const auto after = TextureStorageEstimate(gl.Desc().format, width, height);
                AdmitStorage(after);
                if (gl.IsRenderbuffer())
                {
                    GL::BindRenderbuffer(GL::RenderbufferTarget::Renderbuffer, gl.Name());
                    GL::RenderbufferStorage(GL::RenderbufferTarget::Renderbuffer,
                        ToGlRenderbuffer(gl.Desc().format),
                        static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
                    GL::BindRenderbuffer(GL::RenderbufferTarget::Renderbuffer, 0);
                }
                else
                {
                    const GlTextureFormat format = ToGl(gl.Desc().format);
                    GL::BindTexture(GL::TextureTarget::Texture2D, gl.Name());
                    GL::TexImage2D(GL::TextureTarget::Texture2D, 0, format.Internal,
                        static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), 0,
                        format.Format, format.Type, nullptr);
                    GL::BindTexture(GL::TextureTarget::Texture2D, 0);
                }
                CheckStorageResult("OpenGL texture storage allocation");
                _reservedStorage = _reservedStorage - before + after;
                gl.SetExtent(width, height);
                gl.State(ResourceState::Undefined);
                if (!gl.IsRenderbuffer())
                {
                    GL::BindTexture(GL::TextureTarget::Texture2D, gl.Name());
                    GL::TexParameter(GL::TextureTarget::Texture2D, static_cast<GL::TextureParameterName>(0x813D), 0);
                    GL::BindTexture(GL::TextureTarget::Texture2D, 0);
                }
            }

        private:
            void ValidateTexture(const TextureDesc& desc) const
            {
                if (!desc.width || !desc.height || desc.width > _capabilities.maxTexture2DDimension
                    || desc.height > _capabilities.maxTexture2DDimension || desc.usage == TextureUsage::None)
                    throw std::out_of_range("OpenGL RHI: invalid texture extent or usage.");
                if ((desc.depth != 1 && desc.depth > _capabilities.maxTexture3DDimension)
                    || desc.arrayLayers > _capabilities.maxTextureArrayLayers
                    || desc.mipLevels > _capabilities.maxTextureMipLevels || desc.sampleCount != 1)
                    throw std::invalid_argument("OpenGL RHI: the texture's subresources exceed Capabilities "
                        "(single-level 2D images on this backend).");
                if (!IsValidTextureState(desc, desc.initialState))
                    throw std::invalid_argument("OpenGL RHI: texture usage, format and initial state are incompatible.");
                // One aspect of a GL_DEPTH_STENCIL texture cannot be written
                // on its own; see Capabilities::supportsPackedDepthStencilTransfer.
                if ((desc.format == TextureFormat::D24UnormS8Uint || desc.format == TextureFormat::D32FloatS8Uint)
                    && Has(desc.usage, TextureUsage::TransferSrc | TextureUsage::TransferDst))
                    throw std::invalid_argument("OpenGL RHI: packed depth/stencil images have no transfer usage.");
                (void)ToGl(desc.format);
            }
            OpenGlNative _api;
            OpenGlMemory _memory{false, [](std::int32_t name) { return GL::GetInteger(name); }};
            std::uint64_t _reservedStorage = 0;
            std::unordered_set<OpenGlBuffer*> _resourceBuffers;
            std::unordered_set<OpenGlSamplerStorage*> _samplers;
            std::vector<std::weak_ptr<OpenGlSamplerStorage>> _samplerCache;
            [[nodiscard]] std::unique_ptr<Texture> Make(
                const TextureDesc& desc, std::int32_t name, bool renderbuffer)
            {
                auto texture = std::make_unique<OpenGlTexture>(*this, desc, name, renderbuffer);
                if (!renderbuffer)
                {
                    _byHandle[name] = texture.get();
                }
                _live.insert(texture.get());
                // A render target has storage from the start; a sampled
                // texture gets its storage from its first WriteTexture.
                AllocateStorage(*texture, desc.width, desc.height);
                texture->State(desc.initialState);
                return texture;
            }

            template <typename F>
            void WithScratchFramebuffer(const RenderingInfo& info, F&& query)
            {
                ValidateTargets(info, this);
                const std::int32_t previous = GL::GetInteger(FramebufferBinding);
                const std::int32_t framebuffer = GL::GenFramebuffer();
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, framebuffer);
                AttachTargets(info);
                query();
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, previous);
                GL::DeleteFramebuffer(framebuffer);
            }

            Capabilities _capabilities{};
            std::unordered_map<std::int32_t, OpenGlTexture*> _byHandle{};
            std::unordered_set<OpenGlTexture*> _live{};
            std::vector<std::unique_ptr<Texture>> _retained{};
            std::unordered_set<OpenGlCommandList*> _lists{};
            OpenGlCommandList* _nativeOwner = nullptr;
            std::unordered_set<OpenGlShader*> _shaders{};
            std::unordered_set<std::int32_t> _buffers{};
            std::map<std::pair<const Shader*, const Shader*>, std::shared_ptr<OpenGlProgramStorage>> _programs{};
            std::unordered_set<OpenGlProgramStorage*> _livePrograms;
            std::unordered_set<OpenGlTimestampSet*> _timestampSets;
            std::shared_ptr<TimestampBudget> _timestampBudget = std::make_shared<TimestampBudget>();
            TimestampProperties _timestampProperties;
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
            void* _contextKey;

            void CollectCompleted()
            {
                const auto completed = _scheduler.Poll();
                for (const auto& slot : _slots)
                    if (slot.Serial <= completed) _completedFrame = std::max(_completedFrame, slot.Frame);
                _retired.Collect(completed, DestroyNative);
            }

            RetirementQueue<GlObject> _retired{};
            OpenGlFrameScheduler _scheduler;
            ReadbackQueue _readbacks;
            struct FrameSlot final { std::uint64_t Frame = 0; SubmissionSerial Serial{}; };
            std::array<FrameSlot, FramesInFlight> _slots{};
            std::uint64_t _frame = 0, _completedFrame = 0;
            bool _frameOpen = false;
        };

        #include "OpenGlResourcesImplementation.inc"
        #include "OpenGlGpuDiagnosticsInternal.inc"

        class OpenGlCommandList final : public CommandList
        {
        public:
            explicit OpenGlCommandList(OpenGlGraphicsDevice& device) : _device(&device)
            {
                device.Register(*this);
            }

            ~OpenGlCommandList() override
            {
                while (_device && _debugDepth) EndDebugLabel();
                for (const auto& [key, vao] : _vertexArrays)
                    if (_device) _device->Retire({GlObject::Kind::VertexArray, static_cast<int>(vao.Name), _device->Api().DeleteVertexArrays});
                for (const auto& [key, framebuffer] : _framebuffers)
                {
                    (void)key;
                    RetireFramebuffer(framebuffer);
                }
                if (_device != nullptr)
                {
                    _device->Unregister(*this);
                }
            }

            [[nodiscard]] std::size_t FramebufferCount() const noexcept { return _framebuffers.size(); }
            [[nodiscard]] std::size_t VertexArrayCount() const noexcept { return _vertexArrays.size(); }
            void CaptureNativeObjects(std::vector<NativeObject>& objects) const
            {
                for (const auto& [key, vao] : _vertexArrays)
                    objects.emplace_back(_device->Api().IsVertexArray, vao.Name);
                for (const auto& [key, name] : _framebuffers)
                    objects.emplace_back(_device->Api().IsFramebuffer, static_cast<unsigned>(name));
            }

            // Opening a recording interval does not change drawing state;
            // rendering targets and pipelines own all native state setup.
            void Begin() override
            {
                RequireAlive();
                if (_recording) throw std::logic_error("OpenGL RHI: command list is already recording.");
                _genericSets.clear(); _units = {}; _vertexBindings.clear(); _indexBuffer = nullptr;
                _applied = nullptr; _hasViewport = false; _nativeDirty = true;
                _recording = true;
            }
            [[nodiscard]] CommandListReadiness QueryReadiness() const override
            {
                if (!_device) return CommandListReadiness::Unavailable;
                return _recording ? CommandListReadiness::Recording : CommandListReadiness::Ready;
            }
            void End() override
            {
                RequireAlive();
                if (!_recording) throw std::logic_error("OpenGL RHI: command list is not recording.");
                if (_debugDepth) throw std::logic_error("GPU debug label scope was not ended.");
                _device->SubmitCommands();
                _recording = false;
                _renderingOpen = false;
            }
            void BeginDebugLabel(const DebugLabel& label) override
            {
                RequireRecording();
                ValidateDebugLabel(label);
                if (!_device) throw std::logic_error("The OpenGL command list's session has ended.");
                if (!_device->GetCapabilities().supportsDebugLabels) return;
                if (_debugDepth == 32) throw std::logic_error("GPU debug label nesting exceeds 32.");
                _device->Api().PushDebugGroup(0x824A, 0, static_cast<int>(label.name.size()), label.name.c_str());
                ++_debugDepth;
            }
            void EndDebugLabel() override
            {
                RequireRecording();
                if (!_device->GetCapabilities().supportsDebugLabels) return;
                if (!_debugDepth) throw std::logic_error("No GPU debug label to end.");
                _device->Api().PopDebugGroup(); --_debugDepth;
            }
            void InsertDebugMarker(const DebugLabel& label) override
            {
                RequireRecording();
                ValidateDebugLabel(label);
                if (!_device) throw std::logic_error("The OpenGL command list's session has ended.");
                if (_device->GetCapabilities().supportsDebugLabels)
                    _device->Api().DebugMessageInsert(0x824A, 0x8268, 0, 0x826B,
                        static_cast<int>(label.name.size()), label.name.c_str());
            }
            OpenGlTimestampSet& CheckedTimestamp(TimestampQuerySet& set)
            {
                auto* native = dynamic_cast<OpenGlTimestampSet*>(&set);
                if (!_device || !native || native->Device != _device)
                    throw std::invalid_argument("OpenGL timestamp belongs to another or closed session.");
                return *native;
            }
            void InitializeTimestamps(TimestampQuerySet& set) override
            {
                RequireRecording();
                if (_renderingOpen) throw std::logic_error("Timestamp initialization must be outside rendering.");
                CheckedTimestamp(set).Writes.Initialize();
            }
            void WriteTimestamp(TimestampQuerySet& set, std::uint32_t index) override
            {
                RequireRecording();
                auto& native = CheckedTimestamp(set); native.Writes.Write(index);
                _device->Api().QueryCounter(native.Names[index], 0x8E28);
                _device->CheckStorageResult("glQueryCounter");
                if (_device->GetCapabilities().supportsDebugLabels && _device->Api().ObjectLabel)
                    _device->Api().ObjectLabel(0x82E3, native.Names[index], static_cast<int>(native.Label.size()), native.Label.c_str());
            }

            void BeginRendering(const RenderingInfo& info) override
            {
                RequireRecording();
                ValidateTargets(info, _device);
                Touch(); _renderWidth = info.width; _renderHeight = info.height;
                _scissorEnabled = info.renderArea.width > 0 && info.renderArea.height > 0;
                _scissor = info.renderArea;
                if (info.swapchain)
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, 0);
                    _current = {};
                }
                else
                {
                    const FramebufferKey key = KeyOf(info);
                    auto found = _framebuffers.find(key);
                    if (found == _framebuffers.end())
                    {
                        const std::int32_t framebuffer = GL::GenFramebuffer();
                        GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, framebuffer);
                        AttachTargets(info);
                        found = _framebuffers.emplace(key, framebuffer).first;
                    }
                    else
                    {
                        GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, found->second);
                    }
                    _current = key;
                }
                // Code outside the RHI may have changed GL state since the last
                // pipeline was applied; the next SetPipeline applies in full.
                _applied = nullptr;
                ClearFor(info);
                for (const auto& color : info.colorAttachments)
                    if (color.view) const_cast<OpenGlTexture&>(ViewTexture(*color.view, _device)).State(ResourceState::ColorAttachment);
                if (info.depthStencilAttachment && info.depthStencilAttachment->view)
                    const_cast<OpenGlTexture&>(ViewTexture(*info.depthStencilAttachment->view, _device)).State(ResourceState::DepthStencilWrite);
                _renderingOpen = true;
            }

            void EndRendering() override
            {
                RequireRecording();
                _renderingOpen = false;
            }

            void SetPipeline(const GraphicsPipeline& pipeline) override
            {
                RequireRecording();
                const auto* native = dynamic_cast<const OpenGlGraphicsPipeline*>(&pipeline);
                if (!native || native->Device() != _device) throw std::invalid_argument("OpenGL RHI: pipeline belongs to another device.");
                Touch();
                if (&pipeline == _applied && !_nativeDirty)
                {
                    return;
                }
                _applied = &pipeline;
                for (auto it = _genericSets.begin(); it != _genericSets.end();)
                    if (it->first >= pipeline.Desc().pipelineLayout.groups.size()
                        || it->second->Desc().layout->Desc() != pipeline.Desc().pipelineLayout.groups[it->first])
                        it = _genericSets.erase(it);
                    else ++it;
                const GraphicsPipelineDesc& desc = pipeline.Desc();
                const std::int32_t program = static_cast<const OpenGlGraphicsPipeline&>(pipeline).Program();
                if (program != 0)
                {
                    GL::UseProgram(program);
                }

                const RasterizerStateDesc& raster = desc.rasterizer;
                auto& api = _device->Api();
                OpenGlNative::Require(api.FrontFace, "glFrontFace")(raster.frontFace == FrontFace::Clockwise ? 0x0900 : 0x0901);
#if !defined(__ANDROID__)
                SetCap(0x864F, raster.depthClampEnable);
#endif
                SetCap(CapCullFace, raster.cullMode != CullMode::None);
                if (raster.cullMode != CullMode::None)
                {
                    GL::CullFace(raster.cullMode == CullMode::Front ? GL::TriangleFace::Front : GL::TriangleFace::Back);
                }
                GL::PolygonMode(GL::TriangleFace::FrontAndBack,
                    raster.fillMode == FillMode::Wireframe ? GL::PolygonMode::Line : GL::PolygonMode::Fill);
                GL::LineWidth(raster.lineWidth);
                SetCap(CapPolygonOffsetFill, raster.depthBiasEnable);
                GL::PolygonOffset(raster.depthBiasSlope, raster.depthBiasConstant);

                const DepthStencilStateDesc& ds = desc.depthStencil;
                SetCap(CapDepthTest, ds.depthTestEnable);
                GL::DepthFunc(static_cast<GL::DepthFunction>(ToGl(ds.depthCompareOp)));
                GL::DepthMask(ds.depthWriteEnable);
                SetCap(CapStencilTest, ds.stencilTestEnable);
                GL::StencilMask(ds.stencilWriteMask);
                GL::StencilOp(static_cast<GlStencilOp>(ToGl(ds.front.failOp)),
                    static_cast<GlStencilOp>(ToGl(ds.front.depthFailOp)),
                    static_cast<GlStencilOp>(ToGl(ds.front.passOp)));
                OpenGlNative::Require(api.StencilOpSeparate, "glStencilOpSeparate")(0x0405, ToGl(ds.back.failOp), ToGl(ds.back.depthFailOp), ToGl(ds.back.passOp));
                ApplyStencilFunc();

                const BlendAttachmentDesc blend = desc.blendAttachments.empty()
                    ? BlendAttachmentDesc{} : desc.blendAttachments[0];
                SetCap(CapBlend, blend.blendEnable);
                GL::BlendFunc(static_cast<GL::BlendingFactor>(ToGl(blend.srcColorFactor)),
                    static_cast<GL::BlendingFactor>(ToGl(blend.dstColorFactor)));
                OpenGlNative::Require(api.BlendFuncSeparate, "glBlendFuncSeparate")(ToGl(blend.srcColorFactor), ToGl(blend.dstColorFactor), ToGl(blend.srcAlphaFactor), ToGl(blend.dstAlphaFactor));
                const auto equation = [](BlendOp op) { return op == BlendOp::Add ? 0x8006U : op == BlendOp::Subtract ? 0x800AU : op == BlendOp::ReverseSubtract ? 0x800BU : op == BlendOp::Min ? 0x8007U : 0x8008U; };
                OpenGlNative::Require(api.BlendEquationSeparate, "glBlendEquationSeparate")(equation(blend.colorOp), equation(blend.alphaOp));
                const auto mask = static_cast<std::uint8_t>(blend.writeMask);
                GL::ColorMask((mask & 1U) != 0, (mask & 2U) != 0, (mask & 4U) != 0, (mask & 8U) != 0);

                ApplyAlphaTest(desc.alphaTest);
            }

            void SetViewport(const Viewport& viewport) override
            {
                RequireRecording();
                Touch(); _viewport = viewport; _hasViewport = true;
                GL::Viewport(static_cast<std::int32_t>(viewport.x), static_cast<std::int32_t>(viewport.y),
                    static_cast<std::int32_t>(viewport.width), static_cast<std::int32_t>(viewport.height));
#if defined(__ANDROID__)
                OpenGlNative::Require(_device->Api().DepthRangef, "glDepthRangef")(viewport.minDepth, viewport.maxDepth);
#else
                OpenGlNative::Require(_device->Api().DepthRange, "glDepthRange")(viewport.minDepth, viewport.maxDepth);
#endif
            }

            void SetScissor(const Scissor& scissor) override
            {
                RequireRecording();
                Touch(); _scissor = scissor; _scissorEnabled = true;
                SetCap(0x0C11, true);
                GL::Scissor(scissor.x, scissor.y, static_cast<std::int32_t>(scissor.width),
                    static_cast<std::int32_t>(scissor.height));
            }

            void SetVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint64_t offset) override;
            void SetIndexBuffer(const Buffer& buffer, IndexType type, std::uint64_t offset) override;
            void SetBindingSet(std::uint32_t group, const BindingSet& set) override;
            void SetSmallConstants(const SmallDrawConstants& constants) override
            {
                RequireRecording();
                if (constants.alphaTest < 0 || constants.alphaTest > 2)
                    throw std::invalid_argument("OpenGL RHI: invalid small draw constants.");
                RestoreDrawState();
                ApplyAlphaTest(static_cast<AlphaTestMode>(constants.alphaTest));
                const auto program = GL::GetInteger(CurrentProgram);
                if (program)
                {
                    const auto location = GL::GetUniformLocation(program, "mat_alpha");
                    if (location != -1) GL::Uniform1(location, constants.materialAlpha);
                }
            }
            void ApplyBindingSet(std::uint32_t group, const OpenGlBindingSet& set);
            void SetStencilReference(std::uint32_t reference) override
            {
                RequireRecording();
                Touch();
                _stencilReference = reference;
                ApplyStencilFunc();
            }
            void Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance) override;
            void DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base, std::uint32_t firstInstance) override;
            void DrawSceneGeometry(std::span<const VertexBufferLayoutDesc> buffers,
                std::span<const VertexAttributeDesc> attributes, PrimitiveTopology topology,
                std::uint32_t count, std::uint32_t first);
            void BindInteropTexture(std::int32_t texture, const Sampler& sampler)
            {
                RequireRecording(); Touch();
                const auto* native = dynamic_cast<const OpenGlSampler*>(&sampler);
                if (!native || native->Device() != _device) throw std::invalid_argument("Interop sampler belongs to another device.");
                _units[0] = {nullptr, native, {}, native->Lifetime(), texture, true};
                GL::ActiveTexture(GL::TextureUnit::Texture0);
                GL::BindTexture(GL::TextureTarget::Texture2D, texture);
                OpenGlNative::Require(_device->Api().BindSampler, "glBindSampler")(0, texture ? native->Name() : 0);
            }
            void CopyBuffer(const Buffer& source, std::uint64_t sourceOffset, Buffer& destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyBufferToTexture(const Buffer& source, Texture& destination, const BufferTextureCopy& region) override;
            void CopyTextureToBuffer(const Texture& source, Buffer& destination, const BufferTextureCopy& region) override;
            // OpenGL tracks resource state itself.
            void Transition(Buffer&, ResourceState before, ResourceState after) override;
            void Transition(Texture&, ResourceState before, ResourceState after) override;

            void BindSampledTexture(std::uint32_t slot, const Texture* texture, const Sampler* sampler) override
            {
                RequireRecording();
                if ((texture != nullptr) != (sampler != nullptr)) throw std::invalid_argument("OpenGL RHI: texture and sampler must be paired.");
                const auto* image = texture ? &Native(*texture, _device) : nullptr;
                const auto* native = dynamic_cast<const OpenGlSampler*>(sampler);
                if (sampler && (!native || native->Device() != _device))
                    throw std::invalid_argument("OpenGL RHI: sampler belongs to another device.");
                if (image && !Has(image->Desc().usage, TextureUsage::Sampled))
                    throw std::invalid_argument("OpenGL RHI: bound texture requires Sampled usage.");
                if (slot >= _units.size()) throw std::out_of_range("OpenGL RHI: texture unit exceeds the command contract.");
                Touch();
                _units[slot] = {image, native, image ? image->Lifetime() : std::weak_ptr<void>{},
                    native ? native->Lifetime() : std::weak_ptr<void>{}, 0, true};
                if (slot != 0)
                {
                    GL::ActiveTexture(static_cast<GL::TextureUnit>(
                        static_cast<std::int32_t>(GL::TextureUnit::Texture0) + static_cast<std::int32_t>(slot)));
                }
                GL::BindTexture(GL::TextureTarget::Texture2D, image ? image->Name() : 0);
                OpenGlNative::Require(_device->Api().BindSampler, "glBindSampler")(slot, texture && native ? native->Name() : 0);
                if (slot != 0)
                {
                    GL::ActiveTexture(GL::TextureUnit::Texture0);
                }
            }

            void ReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
                std::uint32_t width, std::uint32_t height, TextureFormat format, void* destination) override
            {
                RequireAlive();
                ValidateTargets(info, _device);
                const GlTextureFormat gl = ToGl(format);
                if (info.swapchain)
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, 0);
                    GL::ReadBuffer(GL::ReadBufferMode::Back);
                }
                else
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, FramebufferFor(info));
                    GL::ReadBuffer(GL::ReadBufferMode::ColorAttachment0);
                }
                GL::PixelStore(GL::PixelStoreParameter::PackAlignment, 1);
                GL::ReadPixels(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                    static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                    gl.Format, gl.Type, destination);
                if (!info.swapchain)
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, 0);
                }
            }

            bool SupportsAsyncReadback() const noexcept override
            { return _device && _device->SupportsAsyncReadback(); }
            ReadbackTicket EnqueueReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
                std::uint32_t width, std::uint32_t height, TextureFormat format) override
            {
                RequireAlive();
                if (!SupportsAsyncReadback()) throw std::logic_error("OpenGL async readback needs sync and mapped-buffer support.");
                if (format != TextureFormat::RGB8Unorm && format != TextureFormat::RGBA8Unorm)
                    throw std::invalid_argument("Async color readback requires RGB8 or RGBA8.");
                if (!info.swapchain)
                {
                    if (info.colorAttachments.empty() || !info.colorAttachments[0].view)
                        throw std::invalid_argument("Async color source is absent.");
                    const auto& texture = ViewTexture(*info.colorAttachments[0].view, _device);
                    if (Native(texture).Device() != _device)
                        throw std::invalid_argument("Async color source belongs to another device.");
                    const auto& desc = texture.Desc();
                    if (!Has(desc.usage, TextureUsage::TransferSrc))
                        throw std::invalid_argument("Async color source requires TransferSrc.");
                    if (desc.format != TextureFormat::RGB8Unorm && desc.format != TextureFormat::RGBA8Unorm
                        && desc.format != TextureFormat::RGBA8Srgb && desc.format != TextureFormat::BGRA8Unorm
                        && desc.format != TextureFormat::BGRA8Srgb)
                        throw std::invalid_argument("Async color source requires an RGB8/RGBA8/BGRA8 image.");
                    if (desc.sampleCount != 1)
                        throw std::invalid_argument("Async color source must be resolved first.");
                    if (x > desc.width || width > desc.width - x || y > desc.height || height > desc.height - y)
                        throw std::out_of_range("Async color readback is outside the image extent.");
                }
                if (x > info.width || width > info.width - x || y > info.height || height > info.height - y)
                    throw std::out_of_range("Async color readback is outside the rendering extent.");
                const auto bytes = ReadbackColorBytes(width, height, format == TextureFormat::RGB8Unorm ? 3 : 4);
                return _device->EnqueueBytes(bytes, [&](Buffer& target) {
                    // PBO write, not a CPU pointer. Preserve pack and read state
                    // even when validation or native operations throw.
                    struct PackState final
                    {
                        int Buffer = GL::GetInteger(0x88ED), Framebuffer = GL::GetInteger(0x8CAA);
                        int ReadBuffer = GL::GetInteger(0x0C02);
                        std::array<int, 8> Names{0x0D05, 0x0D02, 0x0D03, 0x0D04, 0x806C, 0x806B, 0x0D00, 0x0D01};
                        std::array<int, 8> Values{};
                        PackState() { for (std::size_t i = 0; i < Names.size(); ++i) Values[i] = GL::GetInteger(Names[i]); }
                        ~PackState()
                        {
                            GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EB), Buffer);
                            GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, Framebuffer);
                            GL::ReadBuffer(static_cast<GL::ReadBufferMode>(ReadBuffer));
                            for (std::size_t i = 0; i < Names.size(); ++i)
                                GL::PixelStore(static_cast<GL::PixelStoreParameter>(Names[i]), Values[i]);
                        }
                    } saved;
                    for (auto name : saved.Names) GL::PixelStore(static_cast<GL::PixelStoreParameter>(name), name == 0x0D05 ? 1 : 0);
                    GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EB), CheckedBuffer(*_device, target).Name());
                    ReadColor(info, x, y, width, height, format, nullptr);
                    _device->CheckStorageResult("OpenGL asynchronous pixel copy");
                });
            }

            void CopyColorAttachmentToTexture(Texture& destination, std::uint32_t width, std::uint32_t height) override
            {
                RequireRecording();
                auto& target = Native(destination, _device);
                RequireRendering();
                struct Restore final
                {
                    int Texture = GL::GetInteger(0x8069);
                    int ReadFramebuffer = GL::GetInteger(0x8CAA);
                    ~Restore()
                    {
                        GL::BindTexture(GL::TextureTarget::Texture2D, Texture);
                        GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, ReadFramebuffer);
                    }
                } restore;
                const auto current = _framebuffers.find(_current);
                if (current != _framebuffers.end())
                {
                    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, current->second);
                }
                GL::BindTexture(GL::TextureTarget::Texture2D, target.Name());
                GL::CopyTexSubImage2D(GL::TextureTarget::Texture2D, 0, 0, 0, 0, 0,
                    static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
                target.State(Has(target.Desc().usage, TextureUsage::Sampled) ? ResourceState::ShaderRead : ResourceState::CopyDst);
            }

            // A texture is going away: drop every framebuffer built on it.
            void Forget(const Texture& texture) noexcept
            {
                for (auto it = _framebuffers.begin(); it != _framebuffers.end();)
                {
                    if (it->first.Color == &texture || it->first.Depth == &texture)
                    {
                        RetireFramebuffer(it->second);
                        if (_current == it->first)
                        {
                            _current = {};
                            _renderingOpen = false;
                        }
                        it = _framebuffers.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            void Detach() noexcept
            {
                while (_debugDepth) { _device->Api().PopDebugGroup(); --_debugDepth; }
                for (const auto& [key, vao] : _vertexArrays)
                    _device->Api().DeleteVertexArrays(1, &vao.Name);
                for (const auto& [key, framebuffer] : _framebuffers)
                    DestroyNative({GlObject::Kind::Framebuffer, framebuffer});
                _vertexArrays.clear(); _framebuffers.clear(); _vertexBindings.clear();
                _genericSets.clear(); _units = {};
                _applied = nullptr; _indexBuffer = nullptr; _device = nullptr;
                _recording = false;
                _renderingOpen = false;
            }

            void ForgetBuffer(std::int32_t name);

            void ForgetPipeline(const GraphicsPipeline& pipeline) noexcept
            { if (_applied == &pipeline) { _applied = nullptr; _genericSets.clear(); } }
            void ForgetProgram(std::int32_t program) { _alphaTestLocations.erase(program); }

        private:
            unsigned VertexArray();
            void Touch() { if (_device->Activate(*this)) _nativeDirty = true; }
            void RestoreDrawState();
            bool _nativeDirty = true;
            Viewport _viewport{};
            bool _hasViewport = false;
            Scissor _scissor{};
            bool _scissorEnabled = false;
            std::uint32_t _renderWidth = 0, _renderHeight = 0;
            std::map<std::uint32_t, std::unique_ptr<OpenGlBindingSet>> _genericSets;
            struct SampledBinding final
            {
                const OpenGlTexture* Texture = nullptr;
                const OpenGlSampler* Sampler = nullptr;
                std::weak_ptr<void> TextureLifetime, SamplerLifetime;
                std::int32_t InteropTexture = 0;
                bool Bound = false;
            };
            std::array<SampledBinding, 4> _units{};
            void RequireAlive() const
            { if (!_device) throw std::logic_error("The OpenGL command list's session has ended."); }
            void RequireRecording() const
            {
                RequireAlive();
                if (!_recording) throw std::logic_error("OpenGL RHI: command list is not recording.");
            }
            void RequireRendering() const
            {
                RequireRecording();
                if (!_renderingOpen) throw std::logic_error("OpenGL RHI: draw needs a rendering interval.");
            }
            struct VertexBinding final { const OpenGlBuffer* Buffer = nullptr; std::uint64_t Offset = 0; };
            std::unordered_map<std::uint32_t, VertexBinding> _vertexBindings;
            const OpenGlBuffer* _indexBuffer = nullptr;
            std::uint64_t _indexOffset = 0;
            IndexType _indexType = IndexType::UInt32;
            struct VertexArrayEntry final { unsigned Name; std::vector<int> Buffers; };
            std::map<std::vector<std::uint64_t>, VertexArrayEntry> _vertexArrays;
            bool _sceneGeometry = false;
            std::span<const VertexBufferLayoutDesc> _sceneBuffers;
            std::span<const VertexAttributeDesc> _sceneAttributes;
            PrimitiveTopology _sceneTopology = PrimitiveTopology::TriangleList;
            std::uint32_t _debugDepth = 0;
            bool _recording = false;
            bool _renderingOpen = false;
            void RetireFramebuffer(std::int32_t framebuffer) noexcept
            {
                if (_device != nullptr)
                {
                    _device->Retire(GlObject{GlObject::Kind::Framebuffer, framebuffer});
                }
            }

            void ApplyStencilFunc()
            {
                if (_applied == nullptr)
                {
                    return;
                }
                const DepthStencilStateDesc& ds = _applied->Desc().depthStencil;
                GL::StencilFunc(static_cast<GL::StencilFunction>(ToGl(ds.front.compareOp)),
                    static_cast<std::int32_t>(_stencilReference), ds.stencilReadMask);
                OpenGlNative::Require(_device->Api().StencilFuncSeparate, "glStencilFuncSeparate")(0x0405, ToGl(ds.back.compareOp), _stencilReference, ds.stencilReadMask);
            }

            // The alpha test is a discard in the fragment shader, driven by the
            // program's alpha_test uniform; a program without one has no test.
            void ApplyAlphaTest(AlphaTestMode mode)
            {
#if defined(__ANDROID__)
                // The ES wrapper already emulates glAlphaFunc as this same
                // uniform and writes it before every draw from its own state,
                // so the state is what has to be set.
                if (mode == AlphaTestMode::Disabled)
                {
                    GL::Disable(GL::EnableCap::AlphaTest);
                }
                else
                {
                    GL::Enable(GL::EnableCap::AlphaTest);
                    GL::AlphaFunc(mode == AlphaTestMode::EqualOne
                        ? GL::AlphaFunction::Equal : GL::AlphaFunction::Less, 1.0F);
                }
                return;
#endif
                const std::int32_t program = GL::GetInteger(CurrentProgram);
                if (program == 0)
                {
                    return;
                }
                auto found = _alphaTestLocations.find(program);
                if (found == _alphaTestLocations.end())
                {
                    found = _alphaTestLocations.emplace(program,
                        GL::GetUniformLocation(program, "alpha_test")).first;
                }
                if (found->second != -1)
                {
                    GL::Uniform1(found->second, static_cast<std::int32_t>(mode));
                }
            }

            // The framebuffer for these attachments, built if need be without
            // disturbing whatever is bound for drawing.
            [[nodiscard]] std::int32_t FramebufferFor(const RenderingInfo& info)
            {
                ValidateTargets(info, _device);
                const FramebufferKey key = KeyOf(info);
                const auto found = _framebuffers.find(key);
                if (found != _framebuffers.end())
                {
                    return found->second;
                }
                const std::int32_t previous = GL::GetInteger(FramebufferBinding);
                const std::int32_t framebuffer = GL::GenFramebuffer();
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, framebuffer);
                AttachTargets(info);
                GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, previous);
                _framebuffers.emplace(key, framebuffer);
                return framebuffer;
            }

            // The load ops, within the render area. A clear clears the whole
            // attachment (or area) whatever the last pipeline's write masks
            // were, so the masks a clear needs are set first; the next
            // SetPipeline applies its own in full, since BeginRendering forgot
            // the last one.
            void ClearFor(const RenderingInfo& info)
            {
                constexpr std::int32_t CapScissorTest = 0x0C11;
                if (info.renderArea.width > 0 && info.renderArea.height > 0)
                {
                    SetCap(CapScissorTest, true);
                    GL::Scissor(info.renderArea.x, info.renderArea.y,
                        static_cast<std::int32_t>(info.renderArea.width),
                        static_cast<std::int32_t>(info.renderArea.height));
                }
                else
                {
                    SetCap(CapScissorTest, false);
                }
                std::int32_t mask = 0;
                const bool clearColor = !info.colorAttachments.empty()
                    && info.colorAttachments[0].loadOp == LoadOp::Clear;
                const bool clearDepth = info.depthStencilAttachment != nullptr
                    && info.depthStencilAttachment->depthLoadOp == LoadOp::Clear;
                const bool clearStencil = info.depthStencilAttachment != nullptr
                    && info.depthStencilAttachment->stencilLoadOp == LoadOp::Clear;
                if (clearColor)
                {
                    const ClearColor& value = info.colorAttachments[0].clearValue;
                    GL::ClearColor(value.red, value.green, value.blue, value.alpha);
                    GL::ColorMask(true, true, true, true);
                    mask |= static_cast<std::int32_t>(GL::ClearBufferMask::ColorBufferBit);
                }
                if (clearDepth)
                {
                    // clearDepth is 1.0 everywhere the renderer clears; glClearDepth
                    // is never changed from it.
                    GL::DepthMask(true);
                    mask |= static_cast<std::int32_t>(GL::ClearBufferMask::DepthBufferBit);
                }
                if (clearStencil)
                {
                    GL::ClearStencil(static_cast<std::int32_t>(info.depthStencilAttachment->clearStencil));
                    GL::StencilMask(0xFF);
                    mask |= static_cast<std::int32_t>(GL::ClearBufferMask::StencilBufferBit);
                }
                if (mask != 0)
                {
                    GL::Clear(static_cast<GL::ClearBufferMask>(mask));
                }
            }

            OpenGlGraphicsDevice* _device;
            std::unordered_map<FramebufferKey, std::int32_t, FramebufferKeyHash> _framebuffers{};
            FramebufferKey _current{};
            const GraphicsPipeline* _applied = nullptr;
            std::uint32_t _stencilReference = 0;
            std::unordered_map<std::int32_t, std::int32_t> _alphaTestLocations{};
        };

        #include "OpenGlCommandsInternal.inc"
        #include "OpenGlReadbackInternal.inc"

        OpenGlTexture::~OpenGlTexture()
        {
            const GlObject object{_renderbuffer ? GlObject::Kind::Renderbuffer : GlObject::Kind::Texture, _name};
            if (_device != nullptr)
            {
                _device->Forget(*this);
                _device->Retire(object);
            }
        }

        void OpenGlGraphicsDevice::Forget(OpenGlTexture& texture) noexcept
        {
            for (OpenGlCommandList* list : _lists)
            {
                list->Forget(texture);
            }
            if (_live.erase(&texture) && texture.HasStorage())
                _reservedStorage -= TextureStorageEstimate(texture.Desc().format, texture.Desc().width, texture.Desc().height);
            const auto found = _byHandle.find(texture.Name());
            if (found != _byHandle.end() && found->second == &texture)
            {
                _byHandle.erase(found);
            }
        }

        OpenGlGraphicsDevice::~OpenGlGraphicsDevice()
        {
            if (!_contextKey) return;
#if !defined(__ANDROID__)
            void* const previous = ::MphRead::RendererPlatform::CurrentGlContext();
            void* const context = _contextKey;
            if (previous != context) ::MphRead::RendererPlatform::MakeGlContextCurrent(context);
#endif
            CloseNative();
#if !defined(__ANDROID__)
            if (previous != context) ::MphRead::RendererPlatform::MakeGlContextCurrent(previous);
#endif
        }

        void OpenGlGraphicsDevice::CloseNative()
        {
            if (!_contextKey) return;
            // Closing the context also closes ownership after a native failure.
            // Keep failed completion unproven; detach all wrappers regardless
            // so a destructor cannot terminate while handling the first error.
            try { _scheduler.Finish(); }
            catch (...) {}
            while (!_timestampSets.empty()) (*_timestampSets.begin())->Close(true);
            _readbacks.Close();
            GL::UseProgram(0);
            GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, 0);
            if (_api.BindVertexArray) _api.BindVertexArray(0);
            for (OpenGlTexture* texture : _live)
            {
                DestroyNative({texture->IsRenderbuffer() ? GlObject::Kind::Renderbuffer : GlObject::Kind::Texture, texture->Name()});
                texture->Detach();
            }
            for (OpenGlCommandList* list : _lists)
            {
                list->Detach();
            }
            for (OpenGlShader* shader : _shaders)
            {
                DestroyNative({GlObject::Kind::Shader, shader->Name()});
                shader->Detach();
            }
            for (auto* program : _livePrograms)
            {
                DestroyNative({GlObject::Kind::Program, program->Name});
                program->Detach();
            }
            _programs.clear(); _livePrograms.clear();
            for (auto name : _buffers) DestroyNative({GlObject::Kind::Buffer, name});
            for (OpenGlBuffer* buffer : _resourceBuffers) buffer->Detach();
            for (OpenGlSamplerStorage* sampler : _samplers)
            {
                _api.DeleteSamplers(1, &sampler->Name);
                sampler->Name = 0; sampler->Device = nullptr;
            }
            _retired.CollectAll(DestroyNative);
            _live.clear(); _byHandle.clear(); _lists.clear(); _shaders.clear(); _nativeOwner = nullptr;
            _buffers.clear(); _resourceBuffers.clear(); _samplers.clear(); _samplerCache.clear();
            _retained.clear(); _lifetime.reset();
            _reservedStorage = 0;
            _memory.Close();
            ContextDevices().erase(_contextKey);
            _contextKey = nullptr;
        }

        GpuResourceStatistics OpenGlGraphicsDevice::Statistics() const
        {
            GpuResourceStatistics statistics{};
            statistics.Textures = static_cast<std::uint32_t>(_live.size());
            statistics.Buffers = static_cast<std::uint32_t>(_buffers.size());
            for (const auto* texture : _live)
            {
                if (texture->IsRenderbuffer())
                {
                    ++statistics.Renderbuffers;
                    --statistics.Textures;
                }
            }
            statistics.Shaders = static_cast<std::uint32_t>(_shaders.size());
            statistics.Programs = static_cast<std::uint32_t>(_livePrograms.size());
            for (const OpenGlCommandList* list : _lists)
            {
                statistics.Framebuffers += static_cast<std::uint32_t>(list->FramebufferCount());
                statistics.VertexArrays += static_cast<std::uint32_t>(list->VertexArrayCount());
            }
            statistics.Retired = static_cast<std::uint32_t>(_retired.Size());
            statistics.Samplers = static_cast<std::uint32_t>(_samplers.size());
            statistics.TimestampSets = _timestampBudget->Sets();
            statistics.CompletedFrame = _completedFrame;
            statistics.Submitted = _scheduler.Submitted();
            statistics.Completed = _scheduler.Completed();
            statistics.HostWaits = _scheduler.HostWaits();
            statistics.DeviceWideWaits = _scheduler.DeviceWideWaits();
            return statistics;
        }

        OpenGlShader::~OpenGlShader()
        {
            const GlObject object{GlObject::Kind::Shader, _name};
            if (_device != nullptr)
            {
                _device->Forget(*this);
                _device->Retire(object);
            }
        }

        std::unique_ptr<CommandList> OpenGlGraphicsDevice::CreateCommandList()
        {
            return std::make_unique<OpenGlCommandList>(*this);
        }

        std::weak_ptr<void> DeviceLifetime(OpenGlGraphicsDevice& device) { return device.Lifetime(); }
        void OpenGlGraphicsDevice::CheckMemoryAdmission()
        {
            Testing::CheckMemoryAdmission(*this, [this](std::uint64_t bytes) { _memory.SetBudgetCeilingForCheck(bytes); },
                [](Texture& texture, TextureView&) { return static_cast<OpenGlTexture&>(texture).Name(); });
        }
        std::function<void()> OpenGlGraphicsDevice::NativeReleaseCheck()
        {
            std::vector<NativeObject> objects;
            for (auto name : _buffers) objects.emplace_back(_api.IsBuffer, name);
            for (auto* texture : _live) objects.emplace_back(texture->IsRenderbuffer() ? _api.IsRenderbuffer : _api.IsTexture, texture->Name());
            for (auto* shader : _shaders) objects.emplace_back(_api.IsShader, shader->Name());
            for (auto* program : _livePrograms) objects.emplace_back(_api.IsProgram, program->Name);
            for (auto* sampler : _samplers) objects.emplace_back(_api.IsSampler, sampler->Name);
            for (auto* list : _lists) list->CaptureNativeObjects(objects);
            if (objects.empty()) throw std::logic_error("Native release check needs live objects.");
            for (const auto& [exists, name] : objects)
                if (!exists || !exists(name)) throw std::logic_error("Native release witness was not live.");
            return [objects = std::move(objects), context = _contextKey] {
                if (ContextKey() != context) throw std::logic_error("Native release check needs its original context.");
                for (const auto& [exists, name] : objects)
                    if (exists(name)) throw std::logic_error("Session shutdown retained an OpenGL native object.");
            };
        }
        void ForgetPipeline(OpenGlGraphicsDevice& device, const GraphicsPipeline& pipeline) { device.ForgetPipeline(pipeline); }
        void OpenGlGraphicsDevice::ForgetPipeline(const GraphicsPipeline& pipeline)
        { for (auto* list : _lists) list->ForgetPipeline(pipeline); }
        void OpenGlGraphicsDevice::ForgetProgram(std::int32_t program)
        {
            if (GL::GetInteger(CurrentProgram) == program) GL::UseProgram(0);
            for (auto* list : _lists) list->ForgetProgram(program);
        }
        OpenGlProgramStorage::OpenGlProgramStorage(OpenGlGraphicsDevice& device, std::int32_t name)
            : Device(&device), Name(name) { device.Track(*this); }
        OpenGlProgramStorage::~OpenGlProgramStorage()
        {
            if (!Device) return;
            Device->ForgetProgram(Name);
            Device->Untrack(*this);
            Device->Retire({GlObject::Kind::Program, Name});
        }
    }

    GraphicsDevice& ContextDevice()
    {
        const auto found = ContextDevices().find(ContextKey());
        if (found != ContextDevices().end()) return *found->second;
        auto& device = SceneDevice();
        const auto current = ContextDevices().find(ContextKey());
        if (device.GetBackend() != GraphicsBackend::OpenGl || current == ContextDevices().end() || current->second != &device)
            throw std::logic_error("The current context has no OpenGL session.");
        return device;
    }

    std::function<void()> NativeReleaseCheck(GraphicsDevice& device)
    {
        auto* native = dynamic_cast<OpenGlGraphicsDevice*>(&device);
        if (!native) throw std::invalid_argument("Native release check requires OpenGL.");
        return native->NativeReleaseCheck();
    }

    void CheckMemoryAdmission(GraphicsDevice& device)
    {
        auto* native = dynamic_cast<OpenGlGraphicsDevice*>(&device);
        if (!native) throw std::invalid_argument("Memory admission check requires OpenGL.");
        native->CheckMemoryAdmission();
    }

    void AdmitInteropTextureStorage(TextureFormat format, std::uint32_t width, std::uint32_t height)
    {
        static_cast<OpenGlGraphicsDevice&>(ContextDevice()).AdmitStorage(TextureStorageEstimate(format, width, height));
    }

    void CheckInteropStorageResult(const char* operation)
    {
        static_cast<OpenGlGraphicsDevice&>(ContextDevice()).CheckStorageResult(operation);
    }

    std::unique_ptr<GraphicsDevice> CreateGraphicsDevice()
    {
        const auto key = ContextKey();
        if (!key) throw std::logic_error("An OpenGL session requires a current context.");
        if (ContextDevices().contains(key))
            throw std::logic_error("An OpenGL context already has a session device.");
        auto device = std::make_unique<OpenGlGraphicsDevice>();
        ContextDevices().emplace(key, device.get());
        return device;
    }

    bool HasNativeContext(const GraphicsDevice& device) noexcept
    {
        const auto* native = dynamic_cast<const OpenGlGraphicsDevice*>(&device);
        return native && !native->Lifetime().expired();
    }

    std::unique_ptr<Shader> CreateGlslShader(GraphicsDevice& device, ShaderStage stage, const std::string& source)
    {
        return static_cast<OpenGlGraphicsDevice&>(device).CreateGlsl(stage, source);
    }

    std::int32_t ProgramFor(GraphicsDevice& device, const Shader& vertex, const Shader& fragment)
    {
        return static_cast<OpenGlGraphicsDevice&>(device).Program(vertex, fragment)->Name;
    }

    void DrawSceneGeometry(CommandList& commands,
        std::span<const VertexBufferLayoutDesc> buffers, std::span<const VertexAttributeDesc> attributes,
        PrimitiveTopology topology, std::uint32_t count, std::uint32_t first)
    {
        auto* native = dynamic_cast<OpenGlCommandList*>(&commands);
        if (!native) throw std::invalid_argument("Scene geometry needs an OpenGL command list.");
        native->DrawSceneGeometry(buffers, attributes, topology, count, first);
    }

    void ResetWindowViewport(std::int32_t width, std::int32_t height)
    {
        GL::Viewport(0, 0, width, height);
    }

    void SetCurrentAttribute(VertexSemantic semantic, float x, float y, float z, float w)
    {
        static_cast<OpenGlGraphicsDevice&>(ContextDevice()).CurrentAttributes.at(static_cast<unsigned>(semantic)) = {x, y, z, w};
    }
    std::array<float, 4> CurrentAttribute(VertexSemantic semantic)
    { return static_cast<OpenGlGraphicsDevice&>(ContextDevice()).CurrentAttributes.at(static_cast<unsigned>(semantic)); }
    void BindInteropTexture(CommandList& commands, std::int32_t texture, const Sampler& sampler)
    {
        auto* native = dynamic_cast<OpenGlCommandList*>(&commands);
        if (!native) throw std::invalid_argument("Interop needs an OpenGL command list.");
        native->BindInteropTexture(texture, sampler);
    }

#if defined(__ANDROID__)
    void RetireAndroidGeometryBuffer(std::int32_t buffer) noexcept
    {
        if (!buffer) return;
        if (const auto found = ContextDevices().find(ContextKey()); found != ContextDevices().end()) found->second->Retire({GlObject::Kind::Buffer, buffer});
        else DestroyNative({GlObject::Kind::Buffer, buffer});
    }
    std::int32_t CreateAndroidGeometryBuffer()
    { return static_cast<OpenGlGraphicsDevice&>(ContextDevice()).CreateGeometryBuffer(); }
#endif

    void ReleaseContextDevice() noexcept
    {
        // The window can defensively close native state while the context is
        // current. Its session remains the unique owner of the inert wrapper.
        if (const auto found = ContextDevices().find(ContextKey()); found != ContextDevices().end())
            found->second->CloseNative();
    }

}
