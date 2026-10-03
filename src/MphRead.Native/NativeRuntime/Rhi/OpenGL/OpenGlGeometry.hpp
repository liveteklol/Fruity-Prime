#pragma once

#include "../../../RendererGpuMesh.hpp"
#include "../GraphicsDevice.hpp"

#include <memory>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    [[nodiscard]] std::shared_ptr<MphRead::GpuMeshResource> CreateGpuMeshResource(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry);
    [[nodiscard]] std::shared_ptr<MphRead::TransientGeometryResource>
        CreateTransientGeometryResource(GraphicsDevice& device, CommandList& commands);
}
