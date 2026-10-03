#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace MphRead::NativeRuntime::Rhi { class GraphicsDevice; }

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // The current context's GL_KHR_debug hook and identity, for the capture
    // diagnostics; the only GL they need, kept in the backend.
    using DebugProc = void (*)(
        std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
        const char*, const void*);

    void EnableCapability(std::int32_t capability);
    void DebugMessageCallback(DebugProc callback, const void* userParam);
    // Empty when the context has no answer.
    [[nodiscard]] std::string ContextString(std::int32_t name);
    [[nodiscard]] std::int32_t ContextInteger(std::int32_t name);
    // Capture driver object identities while they are live, then prove that
    // session shutdown released them. The context must still be current.
    [[nodiscard]] std::function<void()> NativeReleaseCheck(GraphicsDevice& device);
    void CheckMemoryAdmission(GraphicsDevice& device);
}
