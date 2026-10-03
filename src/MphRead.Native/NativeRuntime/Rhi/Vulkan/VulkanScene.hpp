#pragma once

#include "../../../RendererGpuMesh.hpp"
#include "../CommandList.hpp"
#include "../GraphicsDevice.hpp"
#include "../SceneShaders.hpp"
#include "../WindowUi.hpp"

#include <memory>
#include <span>

// The scene renderer's Vulkan resources: the four programs from the SPIR-V
// generated out of Shaders.cpp, and the mesh and transient geometry the
// renderer draws. The OpenGL counterparts are OpenGlShaderInterface and
// OpenGlGeometry; these take the command list the geometry is drawn through,
// since Vulkan has no context to draw into implicitly.
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    [[nodiscard]] std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(GraphicsDevice& device,
        std::span<const float> toonTable, std::span<const float> shiftTable);
    [[nodiscard]] std::shared_ptr<MphRead::GpuMeshResource> CreateGpuMeshResource(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry);
    [[nodiscard]] std::shared_ptr<MphRead::TransientGeometryResource> CreateTransientGeometryResource(
        GraphicsDevice& device, CommandList& commands);

    // Rhi::WindowUi on the Vulkan window target, in OpenGL's rows, as a
    // scene's window passes do.
    using WindowQuadVertex = Rhi::WindowQuadVertex;

    class WindowUi final : public Rhi::WindowUi
    {
    public:
        explicit WindowUi(GraphicsDevice& device);
        ~WindowUi() override;
        WindowUi(const WindowUi&) = delete;
        WindowUi& operator=(const WindowUi&) = delete;

        void Begin(std::uint32_t width, std::uint32_t height, bool clear) override;
        void DrawTexture(const Texture& texture, const Sampler& sampler,
            std::span<const WindowQuadVertex, 4> strip, bool premultiplied) override;
        void DrawBackdrop(const Texture& photo, const Sampler& sampler,
            std::span<const WindowQuadVertex, 4> strip, float strength, float seconds) override;
        void End() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
