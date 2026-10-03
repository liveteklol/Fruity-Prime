#include "BackendFactory.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include "../../Renderer.hpp"

#include <stdexcept>
#include <utility>

#if defined(MPHREAD_QT)
#include "../../../MphRead.Native.Qt/Platform/QtSwapchain.hpp"
#elif !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

namespace MphRead::NativeRuntime::Rhi
{
#if !defined(__ANDROID__) && !defined(MPHREAD_QT)
    namespace
    {
        class OpenGlBackbufferTexture final : public Texture
        {
        public:
            explicit OpenGlBackbufferTexture(const SwapchainDesc& desc)
            {
                Resize(desc);
            }

            [[nodiscard]] const TextureDesc& Desc() const noexcept override
            {
                return _desc;
            }

            // The window's own surface has no texture handle to bind.
            [[nodiscard]] TextureHandle Handle() const noexcept override
            {
                return {};
            }

            void Resize(const SwapchainDesc& desc) noexcept
            {
                _desc.width = desc.width;
                _desc.height = desc.height;
                _desc.depth = 1;
                _desc.mipLevels = 1;
                _desc.arrayLayers = 1;
                _desc.sampleCount = 1;
                _desc.format = desc.format;
                _desc.usage = TextureUsage::ColorAttachment;
                _desc.memoryUsage = MemoryUsage::GpuOnly;
                _desc.initialState = ResourceState::Present;
            }

        private:
            TextureDesc _desc{};
        };

        class GlfwOpenGlSwapchain final : public Swapchain
        {
        public:
            GlfwOpenGlSwapchain(
                ::MphRead::RendererPlatform::Window& window, SwapchainDesc desc)
                : _window(window), _desc(std::move(desc)), _backbuffer(_desc)
            {
                if (_window.GraphicsMode()
                    != ::MphRead::RendererPlatform::GraphicsWindowMode::OpenGL)
                {
                    throw std::invalid_argument(
                        "An OpenGL swapchain requires an OpenGL graphics window.");
                }
                _handle = static_cast<GLFWwindow*>(_window.NativeHandle());
                if (_handle == nullptr)
                {
                    throw std::invalid_argument(
                        "An OpenGL swapchain requires a native GLFW window handle.");
                }
                SetPresentMode(_desc.presentMode);
            }

            [[nodiscard]] const SwapchainDesc& Desc() const noexcept override
            {
                return _desc;
            }

            void Resize(std::uint32_t width, std::uint32_t height) override
            {
                _desc.width = width;
                _desc.height = height;
                _backbuffer.Resize(_desc);
            }

            [[nodiscard]] Texture& AcquireNextTexture() override
            {
                // The legacy OpenGL path renders directly to GLFW's implicit
                // default backbuffer. The RHI texture object represents that
                // presentable image without exposing a GLuint to the frontend.
                return _backbuffer;
            }

            AcquireResult TryAcquireTexture() override
            {
                // A close request leaves the drawable valid until the owner
                // tears it down. The shell uses that request for switching.
                if (::glfwWindowShouldClose(_handle)) return {PresentationStatus::TemporarilyUnavailable, nullptr};
                if (::glfwGetWindowAttrib(_handle, GLFW_ICONIFIED))
                    return {PresentationStatus::TemporarilyUnavailable, nullptr};
                int width = 0, height = 0;
                ::glfwGetFramebufferSize(_handle, &width, &height);
                if (width <= 0 || height <= 0) return {PresentationStatus::TemporarilyUnavailable, nullptr};
                return {PresentationStatus::Ready, &_backbuffer};
            }
            PresentationCapabilities PresentationCaps() const noexcept override
            { return {true, true, false, 2, 2}; }
            PresentMode RequestedPresentMode() const noexcept override { return _requestedMode; }

            void SetPresentMode(PresentMode mode) override
            {
                _requestedMode = mode;
                if (mode == PresentMode::Mailbox) mode = PresentMode::Fifo;
                ::glfwMakeContextCurrent(_handle);
                ::glfwSwapInterval(mode == PresentMode::Fifo ? 1 : 0);
                _desc.presentMode = mode;
            }

            void Present() override
            {
                ::glfwSwapBuffers(_handle);
            }
            PresentResult TryPresent() override
            {
                const auto acquire = TryAcquireTexture();
                if (!acquire.texture) return {acquire.status, acquire.failure};
                Present();
                return {PresentationStatus::Ready, std::nullopt, true};
            }

        private:
            ::MphRead::RendererPlatform::Window& _window;
            GLFWwindow* _handle = nullptr;
            SwapchainDesc _desc{};
            PresentMode _requestedMode = PresentMode::Fifo;
            OpenGlBackbufferTexture _backbuffer;
        };
    }
#endif

    std::unique_ptr<Swapchain> BackendFactory::CreateSwapchain(
        GraphicsBackend backend, ::MphRead::RendererPlatform::Window& window,
        const SwapchainDesc& desc)
    {
        switch (backend)
        {
        case GraphicsBackend::OpenGl:
#if defined(MPHREAD_QT)
            return ::MphRead::Qt::CreateOpenGlSwapchain(window, desc);
#elif !defined(__ANDROID__)
            return std::make_unique<GlfwOpenGlSwapchain>(window, desc);
#else
            (void)window;
            (void)desc;
            throw std::runtime_error(
                "The desktop OpenGL swapchain is not available on Android.");
#endif
        case GraphicsBackend::Vulkan:
            return Vulkan::CreateSwapchain(window, desc);
        case GraphicsBackend::Metal:
            throw std::runtime_error(
                "The Metal backend is not implemented.");
        case GraphicsBackend::D3D12:
            throw std::runtime_error(
                "The D3D12 backend is not implemented.");
        }
        throw std::invalid_argument("Unknown graphics backend.");
    }
}
