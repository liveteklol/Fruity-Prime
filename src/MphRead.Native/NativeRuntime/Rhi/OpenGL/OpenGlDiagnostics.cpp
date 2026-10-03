#include "OpenGlDiagnostics.hpp"

#include "../../OpenTK/GL.hpp"

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;

    void EnableCapability(std::int32_t capability)
    {
        GL::Enable(static_cast<GL::EnableCap>(capability));
    }

    void DebugMessageCallback(DebugProc callback, const void* userParam)
    {
        GL::DebugMessageCallback(reinterpret_cast<void*>(callback), userParam);
    }

    std::string ContextString(std::int32_t name)
    {
        return GL::GetString(static_cast<GL::StringName>(name));
    }

    std::int32_t ContextInteger(std::int32_t name)
    {
        return GL::GetInteger(name);
    }
}
