#pragma once
namespace MphRead::NativeRuntime::Rhi { class GraphicsDevice; }
namespace MphRead::Mods::Diagnostics
{
    void CheckRhiResourceStates(NativeRuntime::Rhi::GraphicsDevice& device);
    void CheckRhiRgbCopies(NativeRuntime::Rhi::GraphicsDevice& device);
    void CheckRhiFormatCopies(NativeRuntime::Rhi::GraphicsDevice& device);
    void CheckRhiPackedDepthStencilCopies(NativeRuntime::Rhi::GraphicsDevice& device);
    void CheckRhiSubresourceCapabilities(NativeRuntime::Rhi::GraphicsDevice& device);
    void CheckRhiSubresourceReadbacks(NativeRuntime::Rhi::GraphicsDevice& device);
}
