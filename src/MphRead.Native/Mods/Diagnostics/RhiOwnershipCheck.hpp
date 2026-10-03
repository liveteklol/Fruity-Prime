#pragma once
namespace MphRead::NativeRuntime::Rhi { class GraphicsDevice; }
namespace MphRead::Mods::Diagnostics
{
    void CheckResourceOwnership(NativeRuntime::Rhi::GraphicsDevice& device);
}
