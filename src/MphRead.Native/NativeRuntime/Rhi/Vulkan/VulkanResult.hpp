#pragma once
#include "../BackendError.hpp"
#include <vulkan/vulkan.h>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    [[nodiscard]] constexpr BackendErrorKind ClassifyFailure(VkResult result) noexcept
    {
        switch (result)
        {
        case VK_ERROR_DEVICE_LOST: return BackendErrorKind::DeviceLost;
        case VK_ERROR_SURFACE_LOST_KHR: return BackendErrorKind::SurfaceLost;
        case VK_ERROR_OUT_OF_HOST_MEMORY: case VK_ERROR_OUT_OF_DEVICE_MEMORY: return BackendErrorKind::OutOfMemory;
        case VK_ERROR_EXTENSION_NOT_PRESENT: case VK_ERROR_FEATURE_NOT_PRESENT:
        case VK_ERROR_INCOMPATIBLE_DRIVER: case VK_ERROR_FORMAT_NOT_SUPPORTED: return BackendErrorKind::Unsupported;
        default: return BackendErrorKind::Unknown;
        }
    }
    inline void Check(VkResult result, const char* operation)
    {
        if (result == VK_SUCCESS) return;
        throw BackendError(GraphicsBackend::Vulkan, ClassifyFailure(result), result,
            std::string(operation) + " failed: " + std::to_string(result));
    }
}
