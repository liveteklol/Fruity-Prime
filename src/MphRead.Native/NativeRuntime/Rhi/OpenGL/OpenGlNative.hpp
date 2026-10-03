#pragma once

#include "../../OpenTK/GLFW.hpp"
#if !defined(__ANDROID__)
#include "../../OpenTK/GL.hpp"
#endif
#if defined(__ANDROID__)
#include <EGL/egl.h>
#endif
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define FRUITY_GL_CALL __stdcall
#else
#define FRUITY_GL_CALL
#endif

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // Native entry points used only by this backend, loaded with its current
    // context. No process-lifetime cache ties a recreated session to old GL.
    struct OpenGlNative final
    {
        template <class T> static T Load(const char* name)
        {
#if defined(__ANDROID__)
            return reinterpret_cast<T>(eglGetProcAddress(name));
#else
            return reinterpret_cast<T>(::OpenTK::Graphics::OpenGL::GetEntryPoint(name));
#endif
        }
        template <class T> static T Require(T entry, const char* name)
        {
            if (!entry) throw std::runtime_error(std::string("OpenGL entry point unavailable: ") + name);
            return entry;
        }

#define FRUITY_GL_ENTRY(name, result, ...) \
        using name##Type = result(FRUITY_GL_CALL*)(__VA_ARGS__); \
        name##Type name = Load<name##Type>("gl" #name)
        FRUITY_GL_ENTRY(GenSamplers, void, int, unsigned*);
        FRUITY_GL_ENTRY(DeleteSamplers, void, int, const unsigned*);
        FRUITY_GL_ENTRY(SamplerParameteri, void, unsigned, unsigned, int);
        FRUITY_GL_ENTRY(SamplerParameterf, void, unsigned, unsigned, float);
        FRUITY_GL_ENTRY(SamplerParameterfv, void, unsigned, unsigned, const float*);
        FRUITY_GL_ENTRY(BindSampler, void, unsigned, unsigned);
        FRUITY_GL_ENTRY(GenVertexArrays, void, int, unsigned*);
        FRUITY_GL_ENTRY(DeleteVertexArrays, void, int, const unsigned*);
        FRUITY_GL_ENTRY(BindVertexArray, void, unsigned);
        FRUITY_GL_ENTRY(VertexAttribIPointer, void, unsigned, int, unsigned, int, const void*);
        FRUITY_GL_ENTRY(VertexAttribPointer, void, unsigned, int, unsigned, unsigned char, int, const void*);
        FRUITY_GL_ENTRY(EnableVertexAttribArray, void, unsigned);
        FRUITY_GL_ENTRY(VertexAttribDivisor, void, unsigned, unsigned);
        FRUITY_GL_ENTRY(BufferSubData, void, unsigned, std::ptrdiff_t, std::ptrdiff_t, const void*);
        FRUITY_GL_ENTRY(MapBufferRange, void*, unsigned, std::ptrdiff_t, std::ptrdiff_t, unsigned);
        FRUITY_GL_ENTRY(UnmapBuffer, unsigned char, unsigned);
        FRUITY_GL_ENTRY(CopyBufferSubData, void, unsigned, unsigned, std::ptrdiff_t, std::ptrdiff_t, std::ptrdiff_t);
        FRUITY_GL_ENTRY(BindBufferRange, void, unsigned, unsigned, unsigned, std::ptrdiff_t, std::ptrdiff_t);
        FRUITY_GL_ENTRY(BindImageTexture, void, unsigned, unsigned, int, unsigned char, int, unsigned, unsigned);
        FRUITY_GL_ENTRY(MemoryBarrier, void, unsigned);
        FRUITY_GL_ENTRY(FenceSync, void*, unsigned, unsigned);
        FRUITY_GL_ENTRY(ClientWaitSync, unsigned, void*, unsigned, std::uint64_t);
        FRUITY_GL_ENTRY(DeleteSync, void, void*);
        FRUITY_GL_ENTRY(Flush, void);
        FRUITY_GL_ENTRY(IsBuffer, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsTexture, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsRenderbuffer, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsShader, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsProgram, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsSampler, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsFramebuffer, unsigned char, unsigned);
        FRUITY_GL_ENTRY(IsVertexArray, unsigned char, unsigned);
        FRUITY_GL_ENTRY(DrawArraysInstanced, void, unsigned, int, int, int);
        FRUITY_GL_ENTRY(DrawArraysInstancedBaseInstance, void, unsigned, int, int, int, unsigned);
        FRUITY_GL_ENTRY(DrawElementsInstanced, void, unsigned, int, unsigned, const void*, int);
        FRUITY_GL_ENTRY(DrawElementsInstancedBaseVertex, void, unsigned, int, unsigned, const void*, int, int);
        FRUITY_GL_ENTRY(DrawElementsInstancedBaseVertexBaseInstance, void, unsigned, int, unsigned, const void*, int, int, unsigned);
        FRUITY_GL_ENTRY(GetTexImage, void, unsigned, int, unsigned, unsigned, void*);
        FRUITY_GL_ENTRY(FrontFace, void, unsigned);
        FRUITY_GL_ENTRY(BlendFuncSeparate, void, unsigned, unsigned, unsigned, unsigned);
        FRUITY_GL_ENTRY(BlendEquationSeparate, void, unsigned, unsigned);
        FRUITY_GL_ENTRY(StencilFuncSeparate, void, unsigned, unsigned, int, unsigned);
        FRUITY_GL_ENTRY(StencilOpSeparate, void, unsigned, unsigned, unsigned, unsigned);
        FRUITY_GL_ENTRY(DepthRange, void, double, double);
        FRUITY_GL_ENTRY(DepthRangef, void, float, float);
        FRUITY_GL_ENTRY(GenQueries, void, int, unsigned*);
        FRUITY_GL_ENTRY(DeleteQueries, void, int, const unsigned*);
        FRUITY_GL_ENTRY(QueryCounter, void, unsigned, unsigned);
        FRUITY_GL_ENTRY(GetQueryiv, void, unsigned, unsigned, int*);
        FRUITY_GL_ENTRY(GetQueryObjectiv, void, unsigned, unsigned, int*);
        FRUITY_GL_ENTRY(GetQueryObjectui64v, void, unsigned, unsigned, std::uint64_t*);
        FRUITY_GL_ENTRY(PushDebugGroup, void, unsigned, unsigned, int, const char*);
        FRUITY_GL_ENTRY(PopDebugGroup, void);
        FRUITY_GL_ENTRY(DebugMessageInsert, void, unsigned, unsigned, unsigned, unsigned, int, const char*);
        FRUITY_GL_ENTRY(ObjectLabel, void, unsigned, unsigned, int, const char*);
#undef FRUITY_GL_ENTRY
    };
}
#undef FRUITY_GL_CALL
