#include "../NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace MphRead::NativeRuntime::Rhi;
int main()
{
    try
    {
        const std::pair<VkResult, BackendErrorKind> cases[]{
            {VK_ERROR_DEVICE_LOST, BackendErrorKind::DeviceLost},
            {VK_ERROR_SURFACE_LOST_KHR, BackendErrorKind::SurfaceLost},
            {VK_ERROR_OUT_OF_DEVICE_MEMORY, BackendErrorKind::OutOfMemory},
            {VK_ERROR_OUT_OF_HOST_MEMORY, BackendErrorKind::OutOfMemory},
            {VK_ERROR_EXTENSION_NOT_PRESENT, BackendErrorKind::Unsupported},
            {VK_ERROR_FEATURE_NOT_PRESENT, BackendErrorKind::Unsupported},
            {VK_ERROR_INCOMPATIBLE_DRIVER, BackendErrorKind::Unsupported},
            {VK_ERROR_FORMAT_NOT_SUPPORTED, BackendErrorKind::Unsupported},
            {VK_ERROR_INITIALIZATION_FAILED, BackendErrorKind::Unknown},
            {VK_ERROR_MEMORY_MAP_FAILED, BackendErrorKind::Unknown},
            {static_cast<VkResult>(-876543), BackendErrorKind::Unknown}
        };
        Vulkan::Check(VK_SUCCESS, "successful operation");
        for (const auto& [code, kind] : cases)
        {
            bool failed = false;
            try { Vulkan::Check(code, "injected Vulkan operation"); }
            catch (const BackendError& error)
            {
                failed = error.Backend() == GraphicsBackend::Vulkan && error.Kind() == kind && error.NativeCode() == code
                    && std::string(error.what()) == "injected Vulkan operation failed: " + std::to_string(code);
            }
            if (!failed) throw std::runtime_error("Vulkan result classification or original diagnostic was lost.");
        }
        std::cout << "Vulkan result classification/native code/operation preservation PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
