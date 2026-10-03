#include "../BackendSession.hpp"
#include "VulkanWindowSystem.hpp"
#include "../../Skia/VulkanInterop.hpp"
#include "../../../Renderer.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include "VulkanContext.hpp"
#include "VulkanGraphicsDevice.hpp"
#include "VulkanScene.hpp"
#include "VulkanSwapchain.hpp"
#if !defined(__ANDROID__)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
#endif

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
#if defined(FRUITY_HAS_VULKAN)
    namespace
    {
        class Session final : public BackendSession
        {
        public:
            explicit Session(BackendSessionOptions options) : _options(options) {}
            ~Session() override { Shutdown(); }
            GraphicsBackend Backend() const noexcept override { return GraphicsBackend::Vulkan; }
            GraphicsDevice& Device() override
            {
                if (!_device)
                {
                    _context = std::make_unique<Context>(_options.validation);
                    _device = CreateGraphicsDevice(*_context);
                }
                return *_device;
            }
            bool PresentsWindow() const noexcept override { return true; }
            unsigned ValidationErrors() const noexcept override
            { return _errors + (_context ? _context->ValidationErrors() : 0U); }
            bool ValidationEnabled() const noexcept override
            { return _context && _context->ValidationEnabled(); }
            std::string Describe() override { (void)Device(); return _context->Describe(); }
            std::unique_ptr<Swapchain> CreateSwapchain(
                ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc) override
            {
#if !defined(__ANDROID__)
                if (_device && _window && _window != &window)
                    throw std::logic_error("The Vulkan session already belongs to another window.");
                if (!_device)
                {
                    _context = std::make_unique<Context>(_options.validation, window);
                    _device = CreateGraphicsDevice(*_context);
                }
                else if (!_window) _context->ReplaceWindowSurface(window.NativeHandle());
                _window = &window;
                return Vulkan::CreateSwapchain(*_context, window, desc);
#else
                (void)window; (void)desc;
                throw std::runtime_error("Android uses a native surface swapchain.");
#endif
            }
            PresentResult Present(Swapchain& swapchain) override { return PresentWindow(Device(), swapchain); }
            void ResetViewport(std::int32_t, std::int32_t) override {}
            void Shutdown() noexcept override
            {
                if (!_context) return;
                try { if (_device) _device->WaitIdle(); } catch (...) {}
                _device.reset();
                // Include vkDestroyDevice's child-object validation in the
                // session result, while the debug messenger is still alive.
                try { _context->Shutdown(); } catch (...) {}
                _errors += _context->ValidationErrors();
                _context.reset();
                _window = nullptr;
            }
            std::unique_ptr<SceneShaderSet> CreateShaders(GraphicsDevice& device, CommandList&,
                const SceneShaderSources& sources) override
            { return Vulkan::CreateSceneShaderSet(device, sources.ToonTable, sources.ShiftTable); }
            std::shared_ptr<MphRead::GpuMeshResource> CreateMesh(GraphicsDevice& device, CommandList& commands,
                const MphRead::RendererGeometry& geometry) override
            { return CreateGpuMeshResource(device, commands, geometry); }
            std::shared_ptr<MphRead::TransientGeometryResource> CreateTransient(
                GraphicsDevice& device, CommandList& commands) override
            { return CreateTransientGeometryResource(device, commands); }
            std::unique_ptr<Rhi::WindowUi> CreateUi(GraphicsDevice& device) override
            { return std::make_unique<Vulkan::WindowUi>(device); }
            void AttachSurface(void* native) override
            {
#if defined(__ANDROID__)
                if (!_device)
                {
                    _context = std::make_unique<Context>(_options.validation, Context::AndroidWindow{native});
                    _device = CreateGraphicsDevice(*_context);
                }
                else _context->ReplaceAndroidSurface(native);
#else
                (void)native;
                throw std::runtime_error("Native surface attachment is available only on Android.");
#endif
            }
            void DetachSurface() noexcept override
            {
#if defined(__ANDROID__)
                if (!_context) return;
                try { _device->WaitIdle(); _context->ReplaceAndroidSurface(nullptr); } catch (...) {}
#endif
            }
            std::unique_ptr<Swapchain> CreateSurfaceSwapchain(const SwapchainDesc& desc) override
            {
#if defined(__ANDROID__)
                if (!_context) throw std::logic_error("The Vulkan session has no attached surface.");
                return Vulkan::CreateSurfaceSwapchain(*_context, desc);
#else
                (void)desc;
                throw std::runtime_error("Native surface swapchains are available only on Android.");
#endif
            }
        private:
            BackendSessionOptions _options;
            std::unique_ptr<Context> _context;
            std::unique_ptr<GraphicsDevice> _device;
            ::MphRead::RendererPlatform::Window* _window = nullptr;
            unsigned _errors = 0;
        };
        class Provider final : public BackendProvider
        {
        public:
            GraphicsBackend Backend() const noexcept override { return GraphicsBackend::Vulkan; }
            std::string ProbePassive(bool windowUi) const override
            {
#if !defined(__ANDROID__)
#if !defined(MPHREAD_QT)
                // The Avalonia launcher draws through Skia; the Qt menus draw
                // through Qt Quick on the same device and need no Skia.
                if (windowUi && !Skia::VulkanInterop::Available())
                    return "this build's Skia has no Vulkan backend";
#else
                (void)windowUi;
#endif
                if (std::string reason; !WindowSystem::Available(reason)) return reason;
#else
                (void)windowUi;
#endif
                return Context::ProbePassive();
            }
            std::unique_ptr<BackendSession> CreateSession(BackendSessionOptions options) const override
            { return std::make_unique<Session>(options); }
        };
    }
    const BackendProvider* ProviderInstance() noexcept { static Provider provider; return &provider; }
#else
    const BackendProvider* ProviderInstance() noexcept { return nullptr; }
#endif
}
