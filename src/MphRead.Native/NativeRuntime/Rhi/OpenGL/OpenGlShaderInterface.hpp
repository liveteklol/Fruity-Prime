#pragma once

#include "../GraphicsDevice.hpp"
#include "../SceneShaders.hpp"

#include <memory>
#include <span>
#include <string>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // Compile and link the four programs, look up every uniform the
    // renderer sets (the location cache stays in here), and set the
    // uniforms that never change: the texture units and the two tables.
    // Leaves the main program current, as the renderer always has. Throws
    // with the compiler's log when a shader does not compile.
    [[nodiscard]] std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(
        GraphicsDevice& device, const SceneShaderSources& sources);
}
