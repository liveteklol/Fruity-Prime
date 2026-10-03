#include "../BackendSession.hpp"
#include "../BackendFactory.hpp"
#include "OpenGlDevice.hpp"
#include "OpenGlGeometry.hpp"
#include "OpenGlShaderInterface.hpp"

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace
    {
        class Session final : public BackendSession
        {
        public:
            // GL teardown requires a current context; the window owner calls
            // Shutdown before destroying that context, never at process exit.
            ~Session() override { Shutdown(); }
            GraphicsBackend Backend() const noexcept override { return GraphicsBackend::OpenGl; }
            GraphicsDevice& Device() override
            {
                // Harnesses can close a window before releasing their lazy
                // scene session. Its old native state was closed by the window;
                // acquire a new device instead of reusing that expired context.
                if (_device && !HasNativeContext(*_device)) _device.reset();
                if (!_device) _device = CreateGraphicsDevice();
                return *_device;
            }
            bool PresentsWindow() const noexcept override { return false; }
            unsigned ValidationErrors() const noexcept override { return 0; }
            std::string Describe() override { return Device().AdapterDescription(); }
            std::unique_ptr<Swapchain> CreateSwapchain(
                ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc) override
            { (void)Device(); return BackendFactory::CreateSwapchain(Backend(), window, desc); }
            PresentResult Present(Swapchain& swapchain) override { return swapchain.TryPresent(); }
            void ResetViewport(std::int32_t width, std::int32_t height) override { ResetWindowViewport(width, height); }
            void Shutdown() noexcept override
            {
                _device.reset();
            }
            std::unique_ptr<SceneShaderSet> CreateShaders(GraphicsDevice& device, CommandList&,
                const SceneShaderSources& sources) override { return OpenGL::CreateSceneShaderSet(device, sources); }
            std::shared_ptr<MphRead::GpuMeshResource> CreateMesh(GraphicsDevice& device, CommandList& commands,
                const MphRead::RendererGeometry& geometry) override { return CreateGpuMeshResource(device, commands, geometry); }
            std::shared_ptr<MphRead::TransientGeometryResource> CreateTransient(GraphicsDevice& device, CommandList& commands) override
            { return CreateTransientGeometryResource(device, commands); }
        private:
            std::unique_ptr<GraphicsDevice> _device;
        };

        class Provider final : public BackendProvider
        {
        public:
            GraphicsBackend Backend() const noexcept override { return GraphicsBackend::OpenGl; }
            std::string ProbePassive(bool) const override { return {}; }
            std::unique_ptr<BackendSession> CreateSession(BackendSessionOptions) const override
            { return std::make_unique<Session>(); }
        };
    }
    const BackendProvider& ProviderInstance() noexcept { static Provider provider; return provider; }
}
