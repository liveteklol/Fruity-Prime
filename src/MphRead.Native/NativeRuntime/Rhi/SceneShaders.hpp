#pragma once

#include "Resources.hpp"
#include "ShaderConstants.hpp"

#include <cstdint>
#include <span>
#include <string>

// The renderer's shader programs as the frontend sees them: which program a
// pipeline runs, and where its constants go. No program id, shader name or
// uniform location reaches the frontend; a backend builds the set (OpenGL:
// Rhi/OpenGL/OpenGlShaderInterface, from the GLSL in Shaders.cpp) and a
// pipeline names a program by passing the set's shaders in its desc.
namespace MphRead::NativeRuntime::Rhi
{
    // Source assets used by the current desktop/ES shader adapters. Preserve
    // string identity: Android substitutes ES sources by these references.
    // This descriptor carries no native shader object or API packing layout.
    struct SceneShaderSources final
    {
        const std::string* MainVertex = nullptr;
        const std::string* MainFragment = nullptr;
        const std::string* CompositeVertex = nullptr;
        const std::string* CompositeFragment = nullptr;
        const std::string* ShiftFragment = nullptr;
        const std::string* CelFragment = nullptr;
        std::span<const float> ToonTable{};
        std::span<const float> ShiftTable{};
    };

    enum class SceneProgram : std::uint8_t
    {
        Main,       // the world, the preview and the HUD models
        Composite,  // the scene target into the window, the HUD layers and objects, the fade
        Shift,      // the composite under disruption / whiteout
        CelOutline, // the cel pass's ink
        Backdrop    // the launcher photograph; only a backend that draws the
                    // window itself (Vulkan) builds it
    };

    class SceneShaderSet
    {
    public:
        virtual ~SceneShaderSet() = default;

        [[nodiscard]] virtual const Shader& Vertex(SceneProgram program) const = 0;
        [[nodiscard]] virtual const Shader& Fragment(SceneProgram program) const = 0;
        // The constants of whichever of these programs the current pipeline runs.
        [[nodiscard]] virtual ShaderConstantSink& Constants() = 0;
    };
}
