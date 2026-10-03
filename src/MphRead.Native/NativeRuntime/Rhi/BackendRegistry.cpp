#include "BackendSession.hpp"

namespace MphRead::NativeRuntime::Rhi::OpenGL { const BackendProvider& ProviderInstance() noexcept; }
namespace MphRead::NativeRuntime::Rhi::Vulkan { const BackendProvider* ProviderInstance() noexcept; }

namespace MphRead::NativeRuntime::Rhi
{
    const BackendProvider* FindBackendProvider(GraphicsBackend backend) noexcept
    {
        switch (backend)
        {
        case GraphicsBackend::OpenGl: return &OpenGL::ProviderInstance();
        case GraphicsBackend::Vulkan: return Vulkan::ProviderInstance();
        case GraphicsBackend::Metal:
        case GraphicsBackend::D3D12: return nullptr;
        }
        return nullptr;
    }
}
