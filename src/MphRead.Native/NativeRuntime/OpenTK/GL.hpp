#pragma once

#include "../Rhi/VertexSemantics.hpp"

// OpenTK.Graphics.OpenGL as the game calls it (OpenTK 4.9, compatibility
// profile). Enum values are the OpenGL constants; each function is the GL entry
// point of the same name, resolved by GL.cpp.

#include "../../Formats/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace OpenTK::Graphics::OpenGL
{
    // A GL entry point from the context current on this thread, found
    // without asking the window toolkit (GLFW or Qt) that made it.
    [[nodiscard]] void* GetEntryPoint(const char* name);

    enum class FramebufferErrorCode : std::int32_t
    {
        FramebufferUndefined = 0x8219,
        FramebufferComplete = 0x8CD5,
        FramebufferIncompleteAttachment = 0x8CD6,
        FramebufferIncompleteMissingAttachment = 0x8CD7,
        FramebufferIncompleteDrawBuffer = 0x8CDB,
        FramebufferIncompleteReadBuffer = 0x8CDC,
        FramebufferUnsupported = 0x8CDD,
        FramebufferIncompleteMultisample = 0x8D56,
        FramebufferIncompleteLayerTargets = 0x8DA8
    };

    enum class ErrorCode : std::int32_t
    {
        NoError = 0,
        InvalidEnum = 0x0500,
        InvalidValue = 0x0501,
        InvalidOperation = 0x0502,
        StackOverflow = 0x0503,
        StackUnderflow = 0x0504,
        OutOfMemory = 0x0505,
        InvalidFramebufferOperation = 0x0506,
        ContextLost = 0x0507,
        TableTooLarge = 0x8031
    };

    // FramebufferErrorCode.ToString() / ErrorCode.ToString().
    [[nodiscard]] std::string ToString(FramebufferErrorCode value);
    [[nodiscard]] std::string ToString(ErrorCode value);

    // OpenTK.Graphics.OpenGL.GL (OpenTK 4.9, compatibility profile) as the
    // renderer calls it. Enum values are the OpenGL constants; the functions are
    // the platform provider's GL entry points and are declared, not emulated.
    namespace GL
    {
        using ::OpenTK::Graphics::OpenGL::ErrorCode;
        using ::OpenTK::Graphics::OpenGL::FramebufferErrorCode;

        enum class AlphaFunction : std::int32_t { Less = 0x0201, Equal = 0x0202 };
        enum class BlendingFactor : std::int32_t { One = 1, SrcAlpha = 0x0302, OneMinusSrcAlpha = 0x0303 };
        enum class ClearBufferMask : std::int32_t
        {
            DepthBufferBit = 0x00000100,
            StencilBufferBit = 0x00000400,
            ColorBufferBit = 0x00004000
        };
        [[nodiscard]] constexpr ClearBufferMask operator|(ClearBufferMask left, ClearBufferMask right) noexcept
        {
            return static_cast<ClearBufferMask>(static_cast<std::int32_t>(left) | static_cast<std::int32_t>(right));
        }
        enum class BufferTarget : std::int32_t
        {
            ArrayBuffer = 0x8892,
            ElementArrayBuffer = 0x8893,
            PixelPackBuffer = 0x88EB,
            PixelUnpackBuffer = 0x88EC
        };
        enum class BufferUsageHint : std::int32_t
        {
            StreamDraw = 0x88E0,
            StaticDraw = 0x88E4
        };
        enum class ClientState : std::int32_t
        {
            VertexArray = 0x8074,
            NormalArray = 0x8075,
            ColorArray = 0x8076,
            TextureCoordArray = 0x8078
        };
        enum class PointerType : std::int32_t { Float = 0x1406 };
        enum class DrawElementsType : std::int32_t { UnsignedInt = 0x1405 };
        enum class GetPName : std::int32_t
        {
            CurrentColor = 0x0B00,
            CurrentNormal = 0x0B02
        };
        enum class DrawBufferMode : std::int32_t
        {
            None = 0,
            FrontLeft = 0x0400,
            FrontRight = 0x0401,
            BackLeft = 0x0402,
            BackRight = 0x0403,
            Front = 0x0404,
            Back = 0x0405,
            Left = 0x0406,
            Right = 0x0407,
            FrontAndBack = 0x0408,
            ColorAttachment0 = 0x8CE0
        };
        enum class DepthFunction : std::int32_t { Less = 0x0201, Lequal = 0x0203 };
        enum class EnableCap : std::int32_t
        {
            CullFace = 0x0B44,
            DepthTest = 0x0B71,
            StencilTest = 0x0B90,
            AlphaTest = 0x0BC0,
            ScissorTest = 0x0C11,
            Blend = 0x0BE2,
            Texture2D = 0x0DE1,
            PolygonOffsetFill = 0x8037
        };
        enum class FramebufferAttachment : std::int32_t
        {
            DepthStencilAttachment = 0x821A,
            ColorAttachment0 = 0x8CE0,
            DepthAttachment = 0x8D00
        };
        enum class FramebufferParameterName : std::int32_t { FramebufferAttachmentDepthSize = 0x8216 };
        enum class FramebufferTarget : std::int32_t { ReadFramebuffer = 0x8CA8, Framebuffer = 0x8D40 };
        enum class GetProgramParameterName : std::int32_t { LinkStatus = 0x8B82 };
        enum class MatrixMode : std::int32_t { Modelview = 0x1700, Projection = 0x1701 };
        enum class PixelFormat : std::int32_t
        {
            Alpha = 0x1906, Red = 0x1903, Rgb = 0x1907, Rgba = 0x1908, DepthStencil = 0x84F9
        };
        enum class PixelInternalFormat : std::int32_t
        {
            Alpha = 0x1906, Red = 0x1903, Rgb = 0x1907, Rgba = 0x1908,
            Rgba8 = 0x8058, Depth24Stencil8 = 0x88F0
        };
        enum class PixelStoreParameter : std::int32_t
        {
            PackSwapBytes = 0x0D00,
            PackLsbFirst = 0x0D01,
            PackRowLength = 0x0D02,
            PackSkipRows = 0x0D03,
            PackSkipPixels = 0x0D04,
            PackAlignment = 0x0D05,
            UnpackSwapBytes = 0x0CF0,
            UnpackLsbFirst = 0x0CF1,
            UnpackRowLength = 0x0CF2,
            UnpackSkipRows = 0x0CF3,
            UnpackSkipPixels = 0x0CF4,
            UnpackAlignment = 0x0CF5,
            UnpackSkipImages = 0x806D,
            UnpackImageHeight = 0x806E
        };
        enum class PixelType : std::int32_t { UnsignedByte = 0x1401, UnsignedInt248 = 0x84FA };
        enum class PolygonMode : std::int32_t { Line = 0x1B01, Fill = 0x1B02 };
        enum class PrimitiveType : std::int32_t
        {
            LineLoop = 0x0002,
            Triangles = 0x0004,
            TriangleStrip = 0x0005,
            TriangleFan = 0x0006,
            Quads = 0x0007,
            QuadStrip = 0x0008
        };
        enum class ReadBufferMode : std::int32_t { Back = 0x0405, ColorAttachment0 = 0x8CE0 };
        enum class RenderbufferStorage : std::int32_t { Depth24Stencil8 = 0x88F0 };
        enum class RenderbufferTarget : std::int32_t { Renderbuffer = 0x8D41 };
        enum class ShaderParameter : std::int32_t { CompileStatus = 0x8B81 };
        enum class ShaderType : std::int32_t { FragmentShader = 0x8B30, VertexShader = 0x8B31 };
        enum class StencilFunction : std::int32_t
        {
            Equal = 0x0202,
            Greater = 0x0204,
            Notequal = 0x0205,
            Always = 0x0207
        };
        enum class StencilOp : std::int32_t { Zero = 0x0000, Keep = 0x1E00, Replace = 0x1E01 };
        enum class StringName : std::int32_t
        {
            Vendor = 0x1F00,
            Renderer = 0x1F01,
            Version = 0x1F02,
            ShadingLanguageVersion = 0x8B8C
        };
        enum class TextureMagFilter : std::int32_t { Nearest = 0x2600, Linear = 0x2601 };
        enum class TextureMinFilter : std::int32_t { Nearest = 0x2600, Linear = 0x2601 };
        enum class TextureParameterName : std::int32_t
        {
            TextureMagFilter = 0x2800,
            TextureMinFilter = 0x2801,
            TextureWrapS = 0x2802,
            TextureWrapT = 0x2803,
            TextureBaseLevel = 0x813C,
            TextureMaxLevel = 0x813D
        };
        enum class TextureEnvMode : std::int32_t { Modulate = 0x2100, Replace = 0x1E01 };
        enum class TextureEnvParameter : std::int32_t { TextureEnvMode = 0x2200 };
        enum class TextureEnvTarget : std::int32_t { TextureEnv = 0x2300 };
        enum class TextureTarget : std::int32_t { Texture2D = 0x0DE1 };
        enum class TextureUnit : std::int32_t { Texture0 = 0x84C0, Texture1 = 0x84C1 };
        enum class TextureWrapMode : std::int32_t { Repeat = 0x2901, ClampToEdge = 0x812F, MirroredRepeat = 0x8370 };
        enum class TriangleFace : std::int32_t { Front = 0x0404, Back = 0x0405, FrontAndBack = 0x0408 };

#if defined(__ANDROID__)
        // Reset CPU-side compatibility state when GameView creates a fresh EGL context.
        void ResetAndroidState();
#endif

        // The generic locations the renderer's geometry is submitted at, read
        // off the one semantic table (Rhi/VertexSemantics.hpp).
        namespace VertexInput
        {
#if defined(__ANDROID__)
            inline constexpr const auto& Locations = ::MphRead::NativeRuntime::Rhi::OpenGlEsLocations;
#else
            inline constexpr const auto& Locations = ::MphRead::NativeRuntime::Rhi::OpenGlDesktopLocations;
#endif
            inline constexpr std::uint32_t Position = ::MphRead::NativeRuntime::Rhi::Location(
                Locations, ::MphRead::NativeRuntime::Rhi::VertexSemantic::Position);
            inline constexpr std::uint32_t Normal = ::MphRead::NativeRuntime::Rhi::Location(
                Locations, ::MphRead::NativeRuntime::Rhi::VertexSemantic::Normal);
            inline constexpr std::uint32_t Color = ::MphRead::NativeRuntime::Rhi::Location(
                Locations, ::MphRead::NativeRuntime::Rhi::VertexSemantic::Color);
            inline constexpr std::uint32_t TexCoord = ::MphRead::NativeRuntime::Rhi::Location(
                Locations, ::MphRead::NativeRuntime::Rhi::VertexSemantic::TexCoord);
            inline constexpr std::uint32_t TexCoord1 = ::MphRead::NativeRuntime::Rhi::Location(
                Locations, ::MphRead::NativeRuntime::Rhi::VertexSemantic::TexCoord1);
        }

        void ActiveTexture(TextureUnit texture);
        void AlphaFunc(AlphaFunction func, float reference);
        void AttachShader(std::int32_t program, std::int32_t shader);
        void BindBuffer(BufferTarget target, std::int32_t buffer);
        void BufferData(BufferTarget target, std::size_t size, const void* data, BufferUsageHint usage);
        void BindFramebuffer(FramebufferTarget target, std::int32_t framebuffer);
        void BindRenderbuffer(RenderbufferTarget target, std::int32_t renderbuffer);
        void BindTexture(TextureTarget target, std::int32_t texture);
        void BlendFunc(BlendingFactor sfactor, BlendingFactor dfactor);
        [[nodiscard]] FramebufferErrorCode CheckFramebufferStatus(FramebufferTarget target);
        void Clear(ClearBufferMask mask);
        void ClearColor(::OpenTK::Mathematics::Vector4 color);
        void ClearColor(float red, float green, float blue, float alpha);
        void ClearStencil(std::int32_t s);
#if defined(__ANDROID__)
        void ClientActiveTexture(TextureUnit texture);
#endif
        void Color3(float red, float green, float blue);
        void Color3(::OpenTK::Mathematics::Vector3 color);
        void Color4(float red, float green, float blue, float alpha);
        void ColorMask(bool red, bool green, bool blue, bool alpha);
        void CompileShader(std::int32_t shader);
        void CopyTexSubImage2D(TextureTarget target, std::int32_t level, std::int32_t xoffset,
            std::int32_t yoffset, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
        [[nodiscard]] std::int32_t CreateProgram();
        [[nodiscard]] std::int32_t CreateShader(ShaderType type);
        void CullFace(TriangleFace mode);
        void DeleteBuffer(std::int32_t buffer);
        void DeleteProgram(std::int32_t program);
        void DeleteShader(std::int32_t shader);
        void DeleteTexture(std::int32_t texture);
        void DepthFunc(DepthFunction func);
        void DepthMask(bool flag);
        void DetachShader(std::int32_t program, std::int32_t shader);
        void Disable(EnableCap cap);
#if defined(__ANDROID__)
        void DisableClientState(ClientState array);
#endif
        void DisableVertexAttribArray(std::uint32_t index);
        void DrawBuffer(DrawBufferMode mode);
        void DrawElements(PrimitiveType mode, std::int32_t count, DrawElementsType type, const void* indices);
        void Enable(EnableCap cap);
#if defined(__ANDROID__)
        void EnableClientState(ClientState array);
#endif
        void EnableVertexAttribArray(std::uint32_t index);
        void FramebufferRenderbuffer(FramebufferTarget target, FramebufferAttachment attachment,
            RenderbufferTarget renderbuffertarget, std::int32_t renderbuffer);
        void FramebufferTexture2D(FramebufferTarget target, FramebufferAttachment attachment,
            TextureTarget textarget, std::int32_t texture, std::int32_t level);
        void DeleteFramebuffer(std::int32_t framebuffer);
        // Fence sync (GL 3.2 / ES 3.0). The sync object is opaque; null means
        // none (or that the context cannot make one).
        [[nodiscard]] void* FenceSync();
        // True once the fence has signalled; waits at most timeoutNanoseconds.
        [[nodiscard]] bool ClientWaitSync(void* sync, std::uint64_t timeoutNanoseconds);
        void DeleteSync(void* sync);
        void Finish();
        void DeleteRenderbuffer(std::int32_t renderbuffer);
        [[nodiscard]] std::int32_t GenBuffer();
        [[nodiscard]] std::int32_t GenFramebuffer();
        [[nodiscard]] std::int32_t GenRenderbuffer();
        [[nodiscard]] std::int32_t GenTexture();
        [[nodiscard]] ErrorCode GetError();
        // GL.GetInteger(GetPName) and GL.DebugMessageCallback, which the
        // capture path uses for the debug-output extension.
        [[nodiscard]] std::int32_t GetInteger(std::int32_t pname);
        void GetIntegers(std::int32_t pname, std::int32_t* values);
        void GetFloat(GetPName pname, float* values);
        [[nodiscard]] bool IsEnabled(EnableCap cap);
        void DebugMessageCallback(void* callback, const void* userParam);
        void GetFramebufferAttachmentParameter(FramebufferTarget target, FramebufferAttachment attachment,
            FramebufferParameterName pname, std::int32_t& params);
        void GetProgram(std::int32_t program, GetProgramParameterName pname, std::int32_t& params);
        [[nodiscard]] std::string GetProgramInfoLog(std::int32_t program);
        void GetShader(std::int32_t shader, ShaderParameter pname, std::int32_t& params);
        [[nodiscard]] std::string GetShaderInfoLog(std::int32_t shader);
        [[nodiscard]] std::string GetString(StringName name);
        [[nodiscard]] std::int32_t GetUniformLocation(std::int32_t program, const std::string& name);
        void LinkProgram(std::int32_t program);
        void LoadIdentity();
        void MatrixMode(enum MatrixMode mode);
        void MultiTexCoord2(TextureUnit texture, float s, float t);
        void Normal3(float nx, float ny, float nz);
#if defined(__ANDROID__)
        void NormalPointer(PointerType type, std::int32_t stride, const void* pointer);
#endif
#if defined(__ANDROID__)
        void ColorPointer(std::int32_t size, PointerType type, std::int32_t stride, const void* pointer);
#endif
        void PixelStore(PixelStoreParameter pname, std::int32_t param);
        void LineWidth(float width);
        void PolygonMode(TriangleFace face, PolygonMode mode);
        void PolygonOffset(float factor, float units);
        void ReadBuffer(ReadBufferMode src);
        void ReadPixels(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
            PixelFormat format, PixelType type, void* pixels);
        void RenderbufferStorage(RenderbufferTarget target, RenderbufferStorage internalformat,
            std::int32_t width, std::int32_t height);
        void ShaderSource(std::int32_t shader, const std::string& source);
        void StencilFunc(StencilFunction func, std::int32_t ref, std::int32_t mask);
        void StencilMask(std::int32_t mask);
        void StencilOp(StencilOp sfail, StencilOp dpfail, StencilOp dppass);
        void TexEnv(TextureEnvTarget target, TextureEnvParameter pname, std::int32_t param);
#if defined(__ANDROID__)
        void TexCoordPointer(std::int32_t size, PointerType type, std::int32_t stride, const void* pointer);
#endif
        void TexCoord2(float s, float t);
        void TexCoord3(float s, float t, float r);
        void TexCoord3(::OpenTK::Mathematics::Vector3 coord);
        void TexSubImage2D(TextureTarget target, std::int32_t level, std::int32_t xoffset,
            std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format,
            PixelType type, const void* pixels);
        void TexImage2D(TextureTarget target, std::int32_t level, PixelInternalFormat internalformat,
            std::int32_t width, std::int32_t height, std::int32_t border, PixelFormat format, PixelType type,
            const void* pixels);
        void TexParameter(TextureTarget target, TextureParameterName pname, std::int32_t param);
        void Uniform1(std::int32_t location, float v0);
        void Uniform1(std::int32_t location, std::int32_t v0);
        void Uniform1(std::int32_t location, std::int32_t count, const float* value);
        void Uniform3(std::int32_t location, ::OpenTK::Mathematics::Vector3 data);
        void Uniform3(std::int32_t location, std::int32_t count, const float* value);
        void Uniform4(std::int32_t location, ::OpenTK::Mathematics::Vector4 data);
        void Uniform4(std::int32_t location, float v0, float v1, float v2, float v3);
        void Uniform4(std::int32_t location, std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3);
        void UniformMatrix4(std::int32_t location, bool transpose, const ::OpenTK::Mathematics::Matrix4& matrix);
        void UniformMatrix4(std::int32_t location, std::int32_t count, bool transpose, const float* value);
        void UseProgram(std::int32_t program);
        void VertexAttribPointer(std::uint32_t index, std::int32_t size, PointerType type,
            bool normalized, std::int32_t stride, const void* pointer);
#if defined(__ANDROID__)
        void VertexPointer(std::int32_t size, PointerType type, std::int32_t stride, const void* pointer);
#endif
        void PopMatrix();
        void PushMatrix();
        void Scissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
        void Viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    }
}
