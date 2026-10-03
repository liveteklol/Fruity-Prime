#include "GL.hpp"
#include "../System/Enum.hpp"
#include "../Rhi/OpenGL/OpenGlDevice.hpp"

#include <algorithm>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

// The GL entry points this file calls. Only the 1.1 set is exported by the
// platform's own library; everything newer is resolved at run time, which is
// what the driver requires and what OpenTK's own loader does.
namespace
{
    using GLchar = char;
    using GLsizeiptr = std::ptrdiff_t;

    using PFN_ActiveTexture = void(APIENTRY*)(GLenum);
    using PFN_AttachShader = void(APIENTRY*)(GLuint, GLuint);
    using PFN_BindBuffer = void(APIENTRY*)(GLenum, GLuint);
    using PFN_BufferData = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
    using PFN_BindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
    using PFN_BindRenderbuffer = void(APIENTRY*)(GLenum, GLuint);
    using PFN_CheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
    using PFN_CompileShader = void(APIENTRY*)(GLuint);
    using PFN_CreateProgram = GLuint(APIENTRY*)();
    using PFN_CreateShader = GLuint(APIENTRY*)(GLenum);
    using PFN_DeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFN_DeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFN_FenceSync = void*(APIENTRY*)(GLenum, GLbitfield);
    using PFN_ClientWaitSync = GLenum(APIENTRY*)(void*, GLbitfield, std::uint64_t);
    using PFN_DeleteSync = void(APIENTRY*)(void*);
    using PFN_DeleteProgram = void(APIENTRY*)(GLuint);
    using PFN_DeleteRenderbuffers = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFN_DeleteShader = void(APIENTRY*)(GLuint);
    using PFN_DetachShader = void(APIENTRY*)(GLuint, GLuint);
    using PFN_DisableVertexAttribArray = void(APIENTRY*)(GLuint);
    using PFN_EnableVertexAttribArray = void(APIENTRY*)(GLuint);
    using PFN_FramebufferRenderbuffer = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
    using PFN_FramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using PFN_GenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
    using PFN_GenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
    using PFN_GenRenderbuffers = void(APIENTRY*)(GLsizei, GLuint*);
    using PFN_GetFramebufferAttachmentParameteriv
        = void(APIENTRY*)(GLenum, GLenum, GLenum, GLint*);
    using PFN_GetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using PFN_GetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using PFN_GetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using PFN_GetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using PFN_GetUniformLocation = GLint(APIENTRY*)(GLuint, const GLchar*);
    using PFN_LinkProgram = void(APIENTRY*)(GLuint);
    using PFN_BindAttribLocation = void(APIENTRY*)(GLuint, GLuint, const GLchar*);
    using PFN_VertexAttrib4f = void(APIENTRY*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
    using PFN_RenderbufferStorage = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
    using PFN_ShaderSource = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    using PFN_Uniform1f = void(APIENTRY*)(GLint, GLfloat);
    using PFN_Uniform1i = void(APIENTRY*)(GLint, GLint);
    using PFN_Uniform1fv = void(APIENTRY*)(GLint, GLsizei, const GLfloat*);
    using PFN_Uniform3f = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
    using PFN_Uniform3fv = void(APIENTRY*)(GLint, GLsizei, const GLfloat*);
    using PFN_Uniform4f = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    using PFN_Uniform4i = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint);
    using PFN_UniformMatrix4fv = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
    using PFN_UseProgram = void(APIENTRY*)(GLuint);
    using PFN_VertexAttribPointer
        = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    using PFN_DebugMessageCallback = void(APIENTRY*)(void*, const void*);

    [[nodiscard]] void* ResolveEntryPoint(const char* name)
    {
#if defined(_WIN32)
        // wglGetProcAddress answers for everything above 1.1; the rest is an
        // export of opengl32.dll itself.
        if (void* const address = reinterpret_cast<void*>(::wglGetProcAddress(name)))
        {
            // Some drivers report the 1.1 entry points as these sentinels.
            const auto value = reinterpret_cast<std::intptr_t>(address);
            if (value != 0 && value != 1 && value != 2 && value != 3 && value != -1)
            {
                return address;
            }
        }
        static HMODULE library = ::LoadLibraryW(L"opengl32.dll");
        if (library == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<void*>(::GetProcAddress(library, name));
#else
#if defined(__APPLE__)
        return ::dlsym(RTLD_DEFAULT, name);
#else
        static void* library = ::dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (library == nullptr)
        {
            library = ::dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (library == nullptr)
        {
            return nullptr;
        }
        return ::dlsym(library, name);
#endif
#endif
    }

    // Each entry point is resolved exactly once against the context current
    // on its first use. Function-local static initialization is synchronized by
    // C++11, so two threads cannot race while publishing the cached pointer.
#define MPHREAD_GL_ENTRY(type, name)                                        \
    [[nodiscard]] type Get##name()                                          \
    {                                                                       \
        static const type slot = reinterpret_cast<type>(ResolveEntryPoint("gl" #name)); \
        return slot;                                                        \
    }

    MPHREAD_GL_ENTRY(PFN_ActiveTexture, ActiveTexture)
    MPHREAD_GL_ENTRY(PFN_AttachShader, AttachShader)
    MPHREAD_GL_ENTRY(PFN_BindBuffer, BindBuffer)
    MPHREAD_GL_ENTRY(PFN_BufferData, BufferData)
    MPHREAD_GL_ENTRY(PFN_BindFramebuffer, BindFramebuffer)
    MPHREAD_GL_ENTRY(PFN_BindRenderbuffer, BindRenderbuffer)
    MPHREAD_GL_ENTRY(PFN_CheckFramebufferStatus, CheckFramebufferStatus)
    MPHREAD_GL_ENTRY(PFN_CompileShader, CompileShader)
    MPHREAD_GL_ENTRY(PFN_CreateProgram, CreateProgram)
    MPHREAD_GL_ENTRY(PFN_CreateShader, CreateShader)
    MPHREAD_GL_ENTRY(PFN_DeleteBuffers, DeleteBuffers)
    MPHREAD_GL_ENTRY(PFN_DeleteFramebuffers, DeleteFramebuffers)
    MPHREAD_GL_ENTRY(PFN_FenceSync, FenceSync)
    MPHREAD_GL_ENTRY(PFN_ClientWaitSync, ClientWaitSync)
    MPHREAD_GL_ENTRY(PFN_DeleteSync, DeleteSync)
    MPHREAD_GL_ENTRY(PFN_DeleteProgram, DeleteProgram)
    MPHREAD_GL_ENTRY(PFN_DeleteRenderbuffers, DeleteRenderbuffers)
    MPHREAD_GL_ENTRY(PFN_DeleteShader, DeleteShader)
    MPHREAD_GL_ENTRY(PFN_DetachShader, DetachShader)
    MPHREAD_GL_ENTRY(PFN_DisableVertexAttribArray, DisableVertexAttribArray)
    MPHREAD_GL_ENTRY(PFN_EnableVertexAttribArray, EnableVertexAttribArray)
    MPHREAD_GL_ENTRY(PFN_FramebufferRenderbuffer, FramebufferRenderbuffer)
    MPHREAD_GL_ENTRY(PFN_FramebufferTexture2D, FramebufferTexture2D)
    MPHREAD_GL_ENTRY(PFN_GenBuffers, GenBuffers)
    MPHREAD_GL_ENTRY(PFN_GenFramebuffers, GenFramebuffers)
    MPHREAD_GL_ENTRY(PFN_GenRenderbuffers, GenRenderbuffers)
    MPHREAD_GL_ENTRY(PFN_GetFramebufferAttachmentParameteriv, GetFramebufferAttachmentParameteriv)
    MPHREAD_GL_ENTRY(PFN_GetProgramiv, GetProgramiv)
    MPHREAD_GL_ENTRY(PFN_GetProgramInfoLog, GetProgramInfoLog)
    MPHREAD_GL_ENTRY(PFN_GetShaderiv, GetShaderiv)
    MPHREAD_GL_ENTRY(PFN_GetShaderInfoLog, GetShaderInfoLog)
    MPHREAD_GL_ENTRY(PFN_GetUniformLocation, GetUniformLocation)
    MPHREAD_GL_ENTRY(PFN_LinkProgram, LinkProgram)
    MPHREAD_GL_ENTRY(PFN_BindAttribLocation, BindAttribLocation)
    MPHREAD_GL_ENTRY(PFN_VertexAttrib4f, VertexAttrib4f)
    MPHREAD_GL_ENTRY(PFN_RenderbufferStorage, RenderbufferStorage)
    MPHREAD_GL_ENTRY(PFN_ShaderSource, ShaderSource)
    MPHREAD_GL_ENTRY(PFN_Uniform1f, Uniform1f)
    MPHREAD_GL_ENTRY(PFN_Uniform1i, Uniform1i)
    MPHREAD_GL_ENTRY(PFN_Uniform1fv, Uniform1fv)
    MPHREAD_GL_ENTRY(PFN_Uniform3f, Uniform3f)
    MPHREAD_GL_ENTRY(PFN_Uniform3fv, Uniform3fv)
    MPHREAD_GL_ENTRY(PFN_Uniform4f, Uniform4f)
    MPHREAD_GL_ENTRY(PFN_Uniform4i, Uniform4i)
    MPHREAD_GL_ENTRY(PFN_UniformMatrix4fv, UniformMatrix4fv)
    MPHREAD_GL_ENTRY(PFN_UseProgram, UseProgram)
    MPHREAD_GL_ENTRY(PFN_VertexAttribPointer, VertexAttribPointer)
    MPHREAD_GL_ENTRY(PFN_DebugMessageCallback, DebugMessageCallback)

#undef MPHREAD_GL_ENTRY

    namespace RhiGL = ::MphRead::NativeRuntime::Rhi::OpenGL;
    using VertexSemantic = ::MphRead::NativeRuntime::Rhi::VertexSemantic;

    void SetGenericCurrent(std::uint32_t location, float x, float y, float z, float w)
    {
        if (const auto fn = GetVertexAttrib4f()) fn(location, x, y, z, w);
    }
    void SetCurrent(VertexSemantic semantic, float x, float y, float z, float w)
    {
        RhiGL::SetCurrentAttribute(semantic, x, y, z, w);
        SetGenericCurrent(::MphRead::NativeRuntime::Rhi::Location(
            ::MphRead::NativeRuntime::Rhi::OpenGlDesktopLocations, semantic), x, y, z, w);
    }
    void SyncGenericCurrentValues()
    {
        // Array draws may invalidate native current values. Keep the scene's
        // explicit terminal values in its context device, never in aliases.
        for (const auto semantic : {VertexSemantic::Normal, VertexSemantic::Color,
            VertexSemantic::TexCoord, VertexSemantic::TexCoord1})
        {
            const auto v = RhiGL::CurrentAttribute(semantic);
            SetGenericCurrent(::MphRead::NativeRuntime::Rhi::Location(
                ::MphRead::NativeRuntime::Rhi::OpenGlDesktopLocations, semantic), v[0], v[1], v[2], v[3]);
        }
    }

    template <typename T>
    [[nodiscard]] GLenum ToEnum(T value) noexcept
    {
        return static_cast<GLenum>(static_cast<std::int32_t>(value));
    }
}

namespace OpenTK::Graphics::OpenGL
{
    void* GetEntryPoint(const char* name)
    {
        return ResolveEntryPoint(name);
    }
}

namespace OpenTK::Graphics::OpenGL::GL
{
    void ActiveTexture(TextureUnit texture)
    {
        if (const auto fn = GetActiveTexture())
        {
            fn(ToEnum(texture));
        }
    }

    void AlphaFunc(AlphaFunction func, float reference)
    {
        ::glAlphaFunc(ToEnum(func), reference);
    }

    void AttachShader(std::int32_t program, std::int32_t shader)
    {
        if (const auto fn = GetAttachShader())
        {
            fn(static_cast<GLuint>(program), static_cast<GLuint>(shader));
        }
    }

    void BindBuffer(BufferTarget target, std::int32_t buffer)
    {
        const auto fn = GetBindBuffer();
        if (fn == nullptr)
        {
            throw std::runtime_error("glBindBuffer is unavailable.");
        }
        fn(ToEnum(target), static_cast<GLuint>(buffer));
    }

    void BufferData(BufferTarget target, std::size_t size, const void* data, BufferUsageHint usage)
    {
        const auto fn = GetBufferData();
        if (fn == nullptr)
        {
            throw std::runtime_error("glBufferData is unavailable.");
        }
        fn(ToEnum(target), static_cast<GLsizeiptr>(size), data, ToEnum(usage));
    }

    void BindFramebuffer(FramebufferTarget target, std::int32_t framebuffer)
    {
        if (const auto fn = GetBindFramebuffer())
        {
            fn(ToEnum(target), static_cast<GLuint>(framebuffer));
        }
    }

    void BindRenderbuffer(RenderbufferTarget target, std::int32_t renderbuffer)
    {
        if (const auto fn = GetBindRenderbuffer())
        {
            fn(ToEnum(target), static_cast<GLuint>(renderbuffer));
        }
    }

    void BindTexture(TextureTarget target, std::int32_t texture)
    {
        ::glBindTexture(ToEnum(target), static_cast<GLuint>(texture));
    }

    void BlendFunc(BlendingFactor sfactor, BlendingFactor dfactor)
    {
        ::glBlendFunc(ToEnum(sfactor), ToEnum(dfactor));
    }

    FramebufferErrorCode CheckFramebufferStatus(FramebufferTarget target)
    {
        if (const auto fn = GetCheckFramebufferStatus())
        {
            return static_cast<FramebufferErrorCode>(
                static_cast<std::int32_t>(fn(ToEnum(target))));
        }
        return FramebufferErrorCode::FramebufferUndefined;
    }

    void Clear(ClearBufferMask mask)
    {
        ::glClear(static_cast<GLbitfield>(static_cast<std::int32_t>(mask)));
    }

    void ClearColor(::OpenTK::Mathematics::Vector4 color)
    {
        ::glClearColor(color.X, color.Y, color.Z, color.W);
    }

    void ClearColor(float red, float green, float blue, float alpha)
    {
        ::glClearColor(red, green, blue, alpha);
    }

    void ClearStencil(std::int32_t s)
    {
        ::glClearStencil(static_cast<GLint>(s));
    }

    void Color3(float red, float green, float blue)
    {
        SetCurrent(VertexSemantic::Color, red, green, blue, 1.0F);
    }

    void Color3(::OpenTK::Mathematics::Vector3 color)
    {
        Color3(color.X, color.Y, color.Z);
    }

    void Color4(float red, float green, float blue, float alpha)
    {
        SetCurrent(VertexSemantic::Color, red, green, blue, alpha);
    }

    void ColorMask(bool red, bool green, bool blue, bool alpha)
    {
        ::glColorMask(
            red ? GL_TRUE : GL_FALSE, green ? GL_TRUE : GL_FALSE,
            blue ? GL_TRUE : GL_FALSE, alpha ? GL_TRUE : GL_FALSE);
    }

    void CompileShader(std::int32_t shader)
    {
        if (const auto fn = GetCompileShader())
        {
            fn(static_cast<GLuint>(shader));
        }
    }

    void CopyTexSubImage2D(TextureTarget target, std::int32_t level, std::int32_t xoffset,
        std::int32_t yoffset, std::int32_t x, std::int32_t y, std::int32_t width,
        std::int32_t height)
    {
        ::glCopyTexSubImage2D(ToEnum(target), level, xoffset, yoffset, x, y, width, height);
    }

    std::int32_t CreateProgram()
    {
        if (const auto fn = GetCreateProgram())
        {
            return static_cast<std::int32_t>(fn());
        }
        return 0;
    }

    std::int32_t CreateShader(ShaderType type)
    {
        if (const auto fn = GetCreateShader())
        {
            return static_cast<std::int32_t>(fn(ToEnum(type)));
        }
        return 0;
    }

    void CullFace(TriangleFace mode)
    {
        ::glCullFace(ToEnum(mode));
    }

    void DeleteBuffer(std::int32_t buffer)
    {
        if (buffer == 0)
        {
            return;
        }
        const auto fn = GetDeleteBuffers();
        if (fn == nullptr)
        {
            throw std::runtime_error("glDeleteBuffers is unavailable.");
        }
        const GLuint value = static_cast<GLuint>(buffer);
        fn(1, &value);
    }

    void DeleteProgram(std::int32_t program)
    {
        if (const auto fn = GetDeleteProgram())
        {
            fn(static_cast<GLuint>(program));
        }
    }

    void DeleteShader(std::int32_t shader)
    {
        if (const auto fn = GetDeleteShader())
        {
            fn(static_cast<GLuint>(shader));
        }
    }

    void DeleteTexture(std::int32_t texture)
    {
        const GLuint name = static_cast<GLuint>(texture);
        ::glDeleteTextures(1, &name);
    }

    void DepthFunc(DepthFunction func)
    {
        ::glDepthFunc(ToEnum(func));
    }

    void DepthMask(bool flag)
    {
        ::glDepthMask(flag ? GL_TRUE : GL_FALSE);
    }

    void DetachShader(std::int32_t program, std::int32_t shader)
    {
        if (const auto fn = GetDetachShader())
        {
            fn(static_cast<GLuint>(program), static_cast<GLuint>(shader));
        }
    }

    void Disable(EnableCap cap)
    {
        ::glDisable(ToEnum(cap));
    }

    void DisableVertexAttribArray(std::uint32_t index)
    {
        const auto fn = GetDisableVertexAttribArray();
        if (!fn) throw std::runtime_error("glDisableVertexAttribArray is unavailable.");
        fn(index);
    }

    void DrawBuffer(DrawBufferMode mode)
    {
        ::glDrawBuffer(ToEnum(mode));
    }

    void DrawElements(PrimitiveType mode, std::int32_t count, DrawElementsType type, const void* indices)
    {
        ::glDrawElements(ToEnum(mode), static_cast<GLsizei>(count), ToEnum(type), indices);
    }

    void Enable(EnableCap cap)
    {
        ::glEnable(ToEnum(cap));
    }

    void EnableVertexAttribArray(std::uint32_t index)
    {
        const auto fn = GetEnableVertexAttribArray();
        if (!fn) throw std::runtime_error("glEnableVertexAttribArray is unavailable.");
        fn(index);
    }

    void FramebufferRenderbuffer(FramebufferTarget target, FramebufferAttachment attachment,
        RenderbufferTarget renderbuffertarget, std::int32_t renderbuffer)
    {
        if (const auto fn = GetFramebufferRenderbuffer())
        {
            fn(ToEnum(target), ToEnum(attachment), ToEnum(renderbuffertarget),
                static_cast<GLuint>(renderbuffer));
        }
    }

    void FramebufferTexture2D(FramebufferTarget target, FramebufferAttachment attachment,
        TextureTarget textarget, std::int32_t texture, std::int32_t level)
    {
        if (const auto fn = GetFramebufferTexture2D())
        {
            fn(ToEnum(target), ToEnum(attachment), ToEnum(textarget),
                static_cast<GLuint>(texture), level);
        }
    }

    void DeleteFramebuffer(std::int32_t framebuffer)
    {
        const GLuint name = static_cast<GLuint>(framebuffer);
        if (const auto fn = GetDeleteFramebuffers())
        {
            fn(1, &name);
        }
    }

    void* FenceSync()
    {
        const auto fn = GetFenceSync();
        return fn != nullptr ? fn(0x9117 /* GL_SYNC_GPU_COMMANDS_COMPLETE */, 0) : nullptr;
    }

    bool ClientWaitSync(void* sync, std::uint64_t timeoutNanoseconds)
    {
        const auto fn = GetClientWaitSync();
        if (sync == nullptr || fn == nullptr)
        {
            return true;
        }
        const GLenum status = fn(sync, 0x00000001 /* GL_SYNC_FLUSH_COMMANDS_BIT */, timeoutNanoseconds);
        return status == 0x911A /* GL_ALREADY_SIGNALED */ || status == 0x911C /* GL_CONDITION_SATISFIED */;
    }

    void DeleteSync(void* sync)
    {
        if (const auto fn = GetDeleteSync(); fn != nullptr && sync != nullptr)
        {
            fn(sync);
        }
    }

    void Finish()
    {
        ::glFinish();
    }

    std::int32_t GenBuffer()
    {
        const auto fn = GetGenBuffers();
        if (fn == nullptr)
        {
            throw std::runtime_error("glGenBuffers is unavailable.");
        }
        GLuint value = 0;
        fn(1, &value);
        return static_cast<std::int32_t>(value);
    }

    std::int32_t GenFramebuffer()
    {
        GLuint name = 0;
        if (const auto fn = GetGenFramebuffers())
        {
            fn(1, &name);
        }
        return static_cast<std::int32_t>(name);
    }

    std::int32_t GenRenderbuffer()
    {
        GLuint name = 0;
        if (const auto fn = GetGenRenderbuffers())
        {
            fn(1, &name);
        }
        return static_cast<std::int32_t>(name);
    }

    std::int32_t GenTexture()
    {
        GLuint name = 0;
        ::glGenTextures(1, &name);
        return static_cast<std::int32_t>(name);
    }

    std::int32_t GetInteger(std::int32_t pname)
    {
        GLint value = 0;
        ::glGetIntegerv(static_cast<GLenum>(pname), &value);
        return static_cast<std::int32_t>(value);
    }

    void GetIntegers(std::int32_t pname, std::int32_t* values)
    {
        static_assert(sizeof(GLint) == sizeof(std::int32_t));
        ::glGetIntegerv(static_cast<GLenum>(pname), reinterpret_cast<GLint*>(values));
    }

    bool IsEnabled(EnableCap cap)
    {
        return ::glIsEnabled(ToEnum(cap)) == GL_TRUE;
    }

    void DebugMessageCallback(void* callback, const void* userParam)
    {
        if (const auto fn = GetDebugMessageCallback())
        {
            fn(callback, userParam);
        }
    }

    ErrorCode GetError()
    {
        return static_cast<ErrorCode>(static_cast<std::int32_t>(::glGetError()));
    }

    void GetFloat(GetPName pname, float* values)
    {
        if (pname == GetPName::CurrentColor || pname == GetPName::CurrentNormal
            || static_cast<std::int32_t>(pname) == 0x0B03 /* current texcoord */)
        {
            const auto semantic = pname == GetPName::CurrentColor ? VertexSemantic::Color
                : pname == GetPName::CurrentNormal ? VertexSemantic::Normal : VertexSemantic::TexCoord;
            const auto value = RhiGL::CurrentAttribute(semantic);
            std::copy_n(value.begin(), semantic == VertexSemantic::Normal ? 3 : 4, values);
        }
        else ::glGetFloatv(ToEnum(pname), values);
    }

    void GetFramebufferAttachmentParameter(FramebufferTarget target,
        FramebufferAttachment attachment, FramebufferParameterName pname, std::int32_t& params)
    {
        GLint value = 0;
        if (const auto fn = GetGetFramebufferAttachmentParameteriv())
        {
            fn(ToEnum(target), ToEnum(attachment), ToEnum(pname), &value);
        }
        params = static_cast<std::int32_t>(value);
    }

    void GetProgram(std::int32_t program, GetProgramParameterName pname, std::int32_t& params)
    {
        GLint value = 0;
        if (const auto fn = GetGetProgramiv())
        {
            fn(static_cast<GLuint>(program), ToEnum(pname), &value);
        }
        params = static_cast<std::int32_t>(value);
    }

    std::string GetProgramInfoLog(std::int32_t program)
    {
        const auto lengthFn = GetGetProgramiv();
        const auto logFn = GetGetProgramInfoLog();
        if (lengthFn == nullptr || logFn == nullptr)
        {
            return std::string();
        }
        GLint length = 0;
        lengthFn(static_cast<GLuint>(program), 0x8B84, &length);
        if (length <= 1)
        {
            return std::string();
        }
        std::vector<char> buffer(static_cast<std::size_t>(length));
        GLsizei written = 0;
        logFn(static_cast<GLuint>(program), length, &written, buffer.data());
        return std::string(buffer.data(), static_cast<std::size_t>(written));
    }

    void GetShader(std::int32_t shader, ShaderParameter pname, std::int32_t& params)
    {
        GLint value = 0;
        if (const auto fn = GetGetShaderiv())
        {
            fn(static_cast<GLuint>(shader), ToEnum(pname), &value);
        }
        params = static_cast<std::int32_t>(value);
    }

    std::string GetShaderInfoLog(std::int32_t shader)
    {
        const auto lengthFn = GetGetShaderiv();
        const auto logFn = GetGetShaderInfoLog();
        if (lengthFn == nullptr || logFn == nullptr)
        {
            return std::string();
        }
        GLint length = 0;
        // GL_INFO_LOG_LENGTH counts the terminator; the managed string does not.
        lengthFn(static_cast<GLuint>(shader), 0x8B84, &length);
        if (length <= 1)
        {
            return std::string();
        }
        std::vector<char> buffer(static_cast<std::size_t>(length));
        GLsizei written = 0;
        logFn(static_cast<GLuint>(shader), length, &written, buffer.data());
        return std::string(buffer.data(), static_cast<std::size_t>(written));
    }

    std::string GetString(StringName name)
    {
        const GLubyte* const value = ::glGetString(ToEnum(name));
        if (value == nullptr)
        {
            return std::string();
        }
        return std::string(reinterpret_cast<const char*>(value));
    }

    std::int32_t GetUniformLocation(std::int32_t program, const std::string& name)
    {
        if (const auto fn = GetGetUniformLocation())
        {
            return static_cast<std::int32_t>(fn(static_cast<GLuint>(program), name.c_str()));
        }
        return -1;
    }

    void LinkProgram(std::int32_t program)
    {
        // Every desktop program reads its vertex inputs by the semantic names
        // and is bound to the semantic locations here, so no shader and no
        // call site states a number. Binding a name the program does not
        // declare is not an error.
        if (const auto bind = GetBindAttribLocation())
        {
            for (std::size_t i = 0; i < ::MphRead::NativeRuntime::Rhi::VertexSemanticCount; ++i)
            {
                const std::string name(::MphRead::NativeRuntime::Rhi::VertexSemanticNames[i]);
                bind(static_cast<GLuint>(program),
                    static_cast<GLuint>(VertexInput::Locations[i]), name.c_str());
            }
        }
        if (const auto fn = GetLinkProgram())
        {
            fn(static_cast<GLuint>(program));
        }
        SyncGenericCurrentValues();
    }

    void DeleteRenderbuffer(std::int32_t renderbuffer)
    {
        const GLuint name = static_cast<GLuint>(renderbuffer);
        if (const auto fn = GetDeleteRenderbuffers())
        {
            fn(1, &name);
        }
    }

    void LoadIdentity()
    {
        ::glLoadIdentity();
    }

    void MatrixMode(enum MatrixMode mode)
    {
        ::glMatrixMode(ToEnum(mode));
    }

    void MultiTexCoord2(TextureUnit texture, float s, float t)
    {
        SetCurrent(texture == TextureUnit::Texture1 ? VertexSemantic::TexCoord1
            : VertexSemantic::TexCoord, s, t, 0.0F, 1.0F);
    }

    void Normal3(float nx, float ny, float nz)
    {
        SetCurrent(VertexSemantic::Normal, nx, ny, nz, 1.0F);
    }

    void PixelStore(PixelStoreParameter pname, std::int32_t param)
    {
        ::glPixelStorei(ToEnum(pname), param);
    }

    void LineWidth(float width)
    {
        ::glLineWidth(width);
    }

    void PolygonMode(TriangleFace face, enum PolygonMode mode)
    {
        ::glPolygonMode(ToEnum(face), ToEnum(mode));
    }

    void PolygonOffset(float factor, float units)
    {
        ::glPolygonOffset(factor, units);
    }

    void ReadBuffer(ReadBufferMode src)
    {
        ::glReadBuffer(ToEnum(src));
    }

    void ReadPixels(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
        PixelFormat format, PixelType type, void* pixels)
    {
        ::glReadPixels(x, y, width, height, ToEnum(format), ToEnum(type), pixels);
    }

    void RenderbufferStorage(RenderbufferTarget target, enum RenderbufferStorage internalformat,
        std::int32_t width, std::int32_t height)
    {
        if (const auto fn = GetRenderbufferStorage())
        {
            fn(ToEnum(target), ToEnum(internalformat), width, height);
        }
    }

    void ShaderSource(std::int32_t shader, const std::string& source)
    {
        if (const auto fn = GetShaderSource())
        {
            const char* const text = source.c_str();
            const GLint length = static_cast<GLint>(source.size());
            fn(static_cast<GLuint>(shader), 1, &text, &length);
        }
    }

    void StencilFunc(StencilFunction func, std::int32_t ref, std::int32_t mask)
    {
        ::glStencilFunc(ToEnum(func), ref, static_cast<GLuint>(mask));
    }

    void StencilMask(std::int32_t mask)
    {
        ::glStencilMask(static_cast<GLuint>(mask));
    }

    void StencilOp(enum StencilOp sfail, enum StencilOp dpfail, enum StencilOp dppass)
    {
        ::glStencilOp(ToEnum(sfail), ToEnum(dpfail), ToEnum(dppass));
    }

    void TexEnv(TextureEnvTarget target, TextureEnvParameter pname, std::int32_t param)
    {
        ::glTexEnvi(ToEnum(target), ToEnum(pname), param);
    }

    void TexCoord2(float s, float t)
    {
        SetCurrent(VertexSemantic::TexCoord, s, t, 0.0F, 1.0F);
    }

    void TexCoord3(float s, float t, float r)
    {
        SetCurrent(VertexSemantic::TexCoord, s, t, r, 1.0F);
    }

    void TexCoord3(::OpenTK::Mathematics::Vector3 coord)
    {
        TexCoord3(coord.X, coord.Y, coord.Z);
    }

    void TexSubImage2D(TextureTarget target, std::int32_t level, std::int32_t xoffset,
        std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format,
        PixelType type, const void* pixels)
    {
        ::glTexSubImage2D(ToEnum(target), level, xoffset, yoffset, width, height,
            ToEnum(format), ToEnum(type), pixels);
    }

    void TexImage2D(TextureTarget target, std::int32_t level,
        PixelInternalFormat internalformat, std::int32_t width, std::int32_t height,
        std::int32_t border, PixelFormat format, PixelType type, const void* pixels)
    {
        ::glTexImage2D(ToEnum(target), level, static_cast<GLint>(internalformat),
            width, height, border, ToEnum(format), ToEnum(type), pixels);
    }

    void TexParameter(TextureTarget target, TextureParameterName pname, std::int32_t param)
    {
        ::glTexParameteri(ToEnum(target), ToEnum(pname), param);
    }

    void Uniform1(std::int32_t location, float v0)
    {
        if (const auto fn = GetUniform1f())
        {
            fn(location, v0);
        }
    }

    void Uniform1(std::int32_t location, std::int32_t v0)
    {
        if (const auto fn = GetUniform1i())
        {
            fn(location, v0);
        }
    }

    void Uniform1(std::int32_t location, std::int32_t count, const float* value)
    {
        if (const auto fn = GetUniform1fv())
        {
            fn(location, count, value);
        }
    }

    void Uniform3(std::int32_t location, ::OpenTK::Mathematics::Vector3 data)
    {
        if (const auto fn = GetUniform3f())
        {
            fn(location, data.X, data.Y, data.Z);
        }
    }

    void Uniform3(std::int32_t location, std::int32_t count, const float* value)
    {
        if (const auto fn = GetUniform3fv())
        {
            fn(location, count, value);
        }
    }

    void Uniform4(std::int32_t location, ::OpenTK::Mathematics::Vector4 data)
    {
        if (const auto fn = GetUniform4f())
        {
            fn(location, data.X, data.Y, data.Z, data.W);
        }
    }

    void Uniform4(std::int32_t location, float v0, float v1, float v2, float v3)
    {
        if (const auto fn = GetUniform4f())
        {
            fn(location, v0, v1, v2, v3);
        }
    }

    // OpenTK's GL.Uniform4(int, int, int, int, int): glUniform4i. On a float
    // uniform that is GL_INVALID_OPERATION and sets nothing, which callers
    // passing integer literals rely on exactly as the C# build does.
    void Uniform4(std::int32_t location, std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3)
    {
        if (const auto fn = GetUniform4i())
        {
            fn(location, v0, v1, v2, v3);
        }
    }

    void UniformMatrix4(std::int32_t location, bool transpose,
        const ::OpenTK::Mathematics::Matrix4& matrix)
    {
        if (const auto fn = GetUniformMatrix4fv())
        {
            // Matrix4 is sixteen floats in row order, which is the layout
            // OpenTK hands GL.
            fn(location, 1, transpose ? GL_TRUE : GL_FALSE, &matrix.M11);
        }
    }

    void UniformMatrix4(std::int32_t location, std::int32_t count, bool transpose,
        const float* value)
    {
        if (const auto fn = GetUniformMatrix4fv())
        {
            fn(location, count, transpose ? GL_TRUE : GL_FALSE, value);
        }
    }

    void UseProgram(std::int32_t program)
    {
        if (const auto fn = GetUseProgram())
        {
            fn(static_cast<GLuint>(program));
        }
    }

    void VertexAttribPointer(std::uint32_t index, std::int32_t size, PointerType type,
        bool normalized, std::int32_t stride, const void* pointer)
    {
        const auto fn = GetVertexAttribPointer();
        if (!fn) throw std::runtime_error("glVertexAttribPointer is unavailable.");
        fn(index, size, ToEnum(type), normalized ? GL_TRUE : GL_FALSE, stride, pointer);
    }

    void PopMatrix()
    {
        ::glPopMatrix();
    }

    void PushMatrix()
    {
        ::glPushMatrix();
    }

    void Scissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
    {
        ::glScissor(x, y, width, height);
    }

    void Viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
    {
        ::glViewport(x, y, width, height);
    }
}

namespace OpenTK::Graphics::OpenGL
{
    namespace
    {
        constexpr ::MphRead::NativeRuntime::EnumNameEntry FramebufferErrorCodeNames[] = {
            {33305ULL, "FramebufferUndefined"},
            {36053ULL, "FramebufferComplete"},
            {36054ULL, "FramebufferIncompleteAttachment"},
            {36055ULL, "FramebufferIncompleteMissingAttachment"},
            {36059ULL, "FramebufferIncompleteDrawBuffer"},
            {36060ULL, "FramebufferIncompleteReadBuffer"},
            {36061ULL, "FramebufferUnsupported"},
            {36182ULL, "FramebufferIncompleteMultisample"},
            {36264ULL, "FramebufferIncompleteLayerTargets"},
        };

        constexpr ::MphRead::NativeRuntime::EnumNameEntry ErrorCodeNames[] = {
            {0ULL, "NoError"},
            {1280ULL, "InvalidEnum"},
            {1281ULL, "InvalidValue"},
            {1282ULL, "InvalidOperation"},
            {1283ULL, "StackOverflow"},
            {1284ULL, "StackUnderflow"},
            {1285ULL, "OutOfMemory"},
            {1286ULL, "InvalidFramebufferOperation"},
            {1287ULL, "ContextLost"},
            {32817ULL, "TableTooLarge"},
        };
    }

    std::string ToString(FramebufferErrorCode value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, FramebufferErrorCodeNames, std::size(FramebufferErrorCodeNames), false);
    }

    std::string ToString(ErrorCode value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, ErrorCodeNames, std::size(ErrorCodeNames), false);
    }
}
