#pragma once
namespace MphRead::NativeRuntime::Rhi { class GraphicsDevice; class BackendProvider; }
namespace MphRead::Mods::Diagnostics
{
    void CheckAsyncReadback(NativeRuntime::Rhi::GraphicsDevice&);
    void CheckReadbackSessionLifetime(const NativeRuntime::Rhi::BackendProvider&);
}
