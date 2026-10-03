#ifndef SK_GL
#define SK_GL
#endif

#include "Skia.hpp"
#include "VulkanInterop.hpp"
#include "../Rhi/Resources.hpp"
#include "../Rhi/SceneBackend.hpp"
#include "../Rhi/OpenGL/OpenGlDevice.hpp"

#include "../OpenTK/GL.hpp"
#include "../OpenTK/GLFW.hpp"

#include <include/core/SkBlendMode.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkData.h>
#include <include/core/SkFont.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkFontStyle.h>
#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkRRect.h>
#include <include/core/SkMatrix.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkPathBuilder.h>
#include <include/core/SkPathEffect.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkSamplingOptions.h>
#include <include/core/SkSurface.h>
#include <include/core/SkTypeface.h>
#include <include/effects/SkDashPathEffect.h>
#if __has_include(<include/effects/SkGradient.h>)
#define FRUITY_SKIA_GRADIENT_V2 1
#include <include/effects/SkGradient.h>
#else
#define FRUITY_SKIA_GRADIENT_V2 0
#include <include/effects/SkGradientShader.h>
#endif
#if __has_include(<include/gpu/ganesh/GrBackendSurface.h>)
#define FRUITY_SKIA_GANESH_V2 1
#include <include/gpu/ganesh/GrBackendSurface.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/GrTypes.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/gl/GrGLAssembleInterface.h>
#include <include/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <include/gpu/ganesh/gl/GrGLDirectContext.h>
#else
#define FRUITY_SKIA_GANESH_V2 0
#include <include/gpu/GrBackendSurface.h>
#include <include/gpu/GrDirectContext.h>
#include <include/gpu/GrTypes.h>
#include <include/gpu/gl/GrGLAssembleInterface.h>
#include <include/gpu/gl/GrGLTypes.h>
#endif
#if __has_include(<include/ports/SkFontMgr_data.h>)
#define FRUITY_SKIA_FONTMGR_DATA 1
#include <include/ports/SkFontMgr_data.h>
#else
#define FRUITY_SKIA_FONTMGR_DATA 0
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead::NativeRuntime::Skia
{
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;

    namespace
    {
        constexpr std::int32_t GlFramebufferBinding = 0x8CA6;
        constexpr std::int32_t GlViewport = 0x0BA2;
        constexpr std::int32_t GlScissorBox = 0x0C10;
        constexpr std::int32_t GlCurrentProgram = 0x8B8D;
        constexpr std::int32_t GlActiveTexture = 0x84E0;
        constexpr std::int32_t GlTextureBinding2D = 0x8069;
        constexpr std::int32_t GlBlendSrcRgb = 0x80C9;
        constexpr std::int32_t GlBlendDstRgb = 0x80C8;
        constexpr std::int32_t GlDepthFunc = 0x0B74;
        constexpr std::int32_t GlDepthWriteMask = 0x0B72;
        constexpr std::int32_t GlColorWriteMask = 0x0C23;
        constexpr std::int32_t GlPackSwapBytes = 0x0D00;
        constexpr std::int32_t GlPackLsbFirst = 0x0D01;
        constexpr std::int32_t GlPackRowLength = 0x0D02;
        constexpr std::int32_t GlPackSkipRows = 0x0D03;
        constexpr std::int32_t GlPackSkipPixels = 0x0D04;
        constexpr std::int32_t GlPackAlignment = 0x0D05;
        constexpr std::int32_t GlUnpackSwapBytes = 0x0CF0;
        constexpr std::int32_t GlUnpackLsbFirst = 0x0CF1;
        constexpr std::int32_t GlUnpackRowLength = 0x0CF2;
        constexpr std::int32_t GlUnpackSkipRows = 0x0CF3;
        constexpr std::int32_t GlUnpackSkipPixels = 0x0CF4;
        constexpr std::int32_t GlUnpackAlignment = 0x0CF5;
        constexpr std::int32_t GlUnpackSkipImages = 0x806D;
        constexpr std::int32_t GlUnpackImageHeight = 0x806E;
        constexpr std::int32_t GlPixelPackBufferBinding = 0x88ED;
        constexpr std::int32_t GlPixelUnpackBufferBinding = 0x88EF;

        [[nodiscard]] ::SkRect NativeRect(const Rect& r)
        {
            return ::SkRect::MakeLTRB(static_cast<float>(r.Left), static_cast<float>(r.Top),
                static_cast<float>(r.Right), static_cast<float>(r.Bottom));
        }

        [[nodiscard]] ::SkMatrix NativeMatrix(const Matrix& m)
        {
            return ::SkMatrix::MakeAll(static_cast<float>(m.ScaleX), static_cast<float>(m.SkewX),
                static_cast<float>(m.TransX), static_cast<float>(m.SkewY), static_cast<float>(m.ScaleY),
                static_cast<float>(m.TransY), 0.0F, 0.0F, 1.0F);
        }

        [[nodiscard]] ::SkColor4f NativeColor(const Color& c)
        {
            return {c.R / 255.0F, c.G / 255.0F, c.B / 255.0F, c.A / 255.0F};
        }

        [[nodiscard]] ::SkTileMode NativeTileMode(SpreadMethod spread)
        {
            switch (spread)
            {
            case SpreadMethod::Repeat:
                return ::SkTileMode::kRepeat;
            case SpreadMethod::Reflect:
                return ::SkTileMode::kMirror;
            default:
                return ::SkTileMode::kClamp;
            }
        }

        [[nodiscard]] ::SkBlendMode NativeBlend(BlendMode blend)
        {
            return blend == BlendMode::Overlay ? ::SkBlendMode::kOverlay : ::SkBlendMode::kSrcOver;
        }

        [[nodiscard]] ::SkSamplingOptions NativeSampling(FilterQuality quality)
        {
            switch (quality)
            {
            case FilterQuality::None:
                return ::SkSamplingOptions(::SkFilterMode::kNearest);
            case FilterQuality::High:
                return ::SkSamplingOptions(::SkCubicResampler::Mitchell());
            case FilterQuality::Low:
            case FilterQuality::Medium:
            default:
                return ::SkSamplingOptions(::SkFilterMode::kLinear);
            }
        }

        [[nodiscard]] sk_sp<::SkShader> NativeGradient(const Shader& shader)
        {
            const GradientDescriptor& description = shader.Descriptor();
            if (description.Stops.empty())
            {
                return nullptr;
            }

            std::vector<::SkColor4f> colors;
            std::vector<::SkScalar> positions;
            colors.reserve(std::max<std::size_t>(description.Stops.size(), 2));
            positions.reserve(std::max<std::size_t>(description.Stops.size(), 2));
            for (const GradientStop& stop : description.Stops)
            {
                colors.push_back(NativeColor(stop.StopColor));
                positions.push_back(static_cast<::SkScalar>(std::clamp(stop.Offset, 0.0, 1.0)));
            }
            if (colors.size() == 1)
            {
                colors.push_back(colors.front());
                positions.front() = 0.0F;
                positions.push_back(1.0F);
            }

            const ::SkTileMode tile = NativeTileMode(description.Spread);
#if FRUITY_SKIA_GRADIENT_V2
            ::SkGradient::Colors colorSet(
                ::SkSpan<const ::SkColor4f>(colors.data(), colors.size()),
                ::SkSpan<const float>(positions.data(), positions.size()), tile);
            ::SkGradient::Interpolation interpolation;
            interpolation.fInPremul = ::SkGradient::Interpolation::InPremul::kYes;
            const ::SkGradient gradient(colorSet, interpolation);
#else
            ::SkGradientShader::Interpolation interpolation;
            interpolation.fInPremul = ::SkGradientShader::Interpolation::InPremul::kYes;
#endif

            if (description.Kind == GradientKind::Linear)
            {
                const ::SkPoint points[2]{
                    {static_cast<float>(description.Start.X), static_cast<float>(description.Start.Y)},
                    {static_cast<float>(description.End.X), static_cast<float>(description.End.Y)}
                };
#if FRUITY_SKIA_GRADIENT_V2
                return ::SkShaders::LinearGradient(points, gradient);
#else
                return ::SkGradientShader::MakeLinear(points, colors.data(), nullptr, positions.data(),
                    static_cast<int>(colors.size()), tile, interpolation, nullptr);
#endif
            }

            const float rx = static_cast<float>(std::max(std::abs(description.RadiusX), 1e-6));
            const float ry = static_cast<float>(std::max(std::abs(description.RadiusY), 1e-6));
            const ::SkPoint focus{
                static_cast<float>((description.Origin.X - description.Centre.X) / rx),
                static_cast<float>((description.Origin.Y - description.Centre.Y) / ry)
            };
            const ::SkMatrix ellipse = ::SkMatrix::MakeAll(rx, 0.0F, static_cast<float>(description.Centre.X),
                0.0F, ry, static_cast<float>(description.Centre.Y), 0.0F, 0.0F, 1.0F);
            if (std::abs(focus.x()) < 1e-6F && std::abs(focus.y()) < 1e-6F)
            {
#if FRUITY_SKIA_GRADIENT_V2
                return ::SkShaders::RadialGradient({0.0F, 0.0F}, 1.0F, gradient, &ellipse);
#else
                return ::SkGradientShader::MakeRadial({0.0F, 0.0F}, 1.0F, colors.data(), nullptr,
                    positions.data(), static_cast<int>(colors.size()), tile, interpolation, &ellipse);
#endif
            }
#if FRUITY_SKIA_GRADIENT_V2
            return ::SkShaders::TwoPointConicalGradient(
                focus, 0.0F, {0.0F, 0.0F}, 1.0F, gradient, &ellipse);
#else
            return ::SkGradientShader::MakeTwoPointConical(focus, 0.0F, {0.0F, 0.0F}, 1.0F,
                colors.data(), nullptr, positions.data(), static_cast<int>(colors.size()),
                tile, interpolation, &ellipse);
#endif
        }

        [[nodiscard]] ::SkPaint NativePaint(const Paint& paint, BlendMode blend = BlendMode::SrcOver)
        {
            ::SkPaint native;
            native.setAntiAlias(paint.Antialias);
            native.setBlendMode(NativeBlend(blend));
            if (paint.Gradient != nullptr)
            {
                native.setShader(NativeGradient(*paint.Gradient));
                native.setAlphaf(static_cast<float>(std::clamp(paint.Opacity, 0.0, 1.0)));
            }
            else
            {
                ::SkColor4f color = NativeColor(paint.Solid);
                color.fA *= static_cast<float>(std::clamp(paint.Opacity, 0.0, 1.0));
                native.setColor4f(color);
            }
            return native;
        }

        [[nodiscard]] GrGLFuncPtr ResolveSkiaGl(void*, const char name[])
        {
            const auto proc = ::OpenTK::Windowing::GraphicsLibraryFramework::GLFW::GetProcAddress(name);
            return reinterpret_cast<GrGLFuncPtr>(proc);
        }

        // State Ganesh binds and never unbinds that the game's fixed-function
        // renderer has no wrapper for. A sampler object left on a unit
        // overrides the game's per-texture wrap/filter (REPEAT becomes
        // CLAMP_TO_EDGE, so textures smear sideways), and a leaked VAO,
        // buffer or enabled generic attribute array breaks immediate mode.
        constexpr unsigned GlMaxCombinedTextureUnits = 0x8B4D;
        constexpr unsigned GlMaxVertexAttribs = 0x8869;
        constexpr unsigned GlTexture0 = 0x84C0;
        constexpr unsigned GlTexture2D = 0x0DE1;
        constexpr unsigned GlTextureRectangle = 0x84F5;
        constexpr unsigned GlTextureBindingRectangle = 0x84F6;
        constexpr unsigned GlTextureBinding2DRaw = 0x8069;
        constexpr unsigned GlArrayBuffer = 0x8892;
        constexpr unsigned GlArrayBufferBinding = 0x8894;
        constexpr unsigned GlElementArrayBuffer = 0x8893;
        constexpr unsigned GlFrontFace = 0x0B46;
        constexpr unsigned GlBlendEquationRgb = 0x8009;
        constexpr unsigned GlBlendEquationAlpha = 0x883D;

        struct ExtraGl
        {
            using U1 = void (GR_GL_FUNCTION_TYPE*)(unsigned);
            using U2 = void (GR_GL_FUNCTION_TYPE*)(unsigned, unsigned);
            using GetIntegervFn = void (GR_GL_FUNCTION_TYPE*)(unsigned, int*);

            U2 BindSampler = nullptr;
            U1 BindVertexArray = nullptr;
            U2 BindBuffer = nullptr;
            U1 DisableVertexAttribArray = nullptr;
            U2 VertexAttribDivisor = nullptr;
            U1 ActiveTexture = nullptr;
            U2 BindTexture = nullptr;
            GetIntegervFn GetIntegerv = nullptr;
            U1 FrontFace = nullptr;
            U2 BlendEquationSeparate = nullptr;
            bool Loaded = false;

            template <typename T>
            static T Resolve(const char* name) noexcept
            {
                return reinterpret_cast<T>(ResolveSkiaGl(nullptr, name));
            }

            void Load() noexcept
            {
                if (Loaded)
                {
                    return;
                }
                BindSampler = Resolve<U2>("glBindSampler");
                BindVertexArray = Resolve<U1>("glBindVertexArray");
                BindBuffer = Resolve<U2>("glBindBuffer");
                DisableVertexAttribArray = Resolve<U1>("glDisableVertexAttribArray");
                VertexAttribDivisor = Resolve<U2>("glVertexAttribDivisor");
                ActiveTexture = Resolve<U1>("glActiveTexture");
                BindTexture = Resolve<U2>("glBindTexture");
                GetIntegerv = Resolve<GetIntegervFn>("glGetIntegerv");
                FrontFace = Resolve<U1>("glFrontFace");
                BlendEquationSeparate = Resolve<U2>("glBlendEquationSeparate");
                Loaded = true;
            }

            [[nodiscard]] int Get(unsigned name, int fallback) const noexcept
            {
                int value = fallback;
                if (GetIntegerv != nullptr)
                {
                    GetIntegerv(name, &value);
                }
                return value;
            }
        };
    }

    struct GpuAccess final
    {
        [[nodiscard]] static ::SkPath Path(const Skia::Path& source)
        {
            ::SkPathBuilder builder;
            builder.setFillType(source.Rule == FillRule::EvenOdd
                ? ::SkPathFillType::kEvenOdd : ::SkPathFillType::kWinding);
            std::size_t point = 0;
            for (const Skia::Path::Verb verb : source._verbs)
            {
                switch (verb)
                {
                case Skia::Path::Verb::Move:
                    builder.moveTo(static_cast<float>(source._points[point].X),
                        static_cast<float>(source._points[point].Y));
                    ++point;
                    break;
                case Skia::Path::Verb::Line:
                    builder.lineTo(static_cast<float>(source._points[point].X),
                        static_cast<float>(source._points[point].Y));
                    ++point;
                    break;
                case Skia::Path::Verb::Quad:
                    builder.quadTo(static_cast<float>(source._points[point].X),
                        static_cast<float>(source._points[point].Y),
                        static_cast<float>(source._points[point + 1].X),
                        static_cast<float>(source._points[point + 1].Y));
                    point += 2;
                    break;
                case Skia::Path::Verb::Cubic:
                    builder.cubicTo(static_cast<float>(source._points[point].X),
                        static_cast<float>(source._points[point].Y),
                        static_cast<float>(source._points[point + 1].X),
                        static_cast<float>(source._points[point + 1].Y),
                        static_cast<float>(source._points[point + 2].X),
                        static_cast<float>(source._points[point + 2].Y));
                    point += 3;
                    break;
                case Skia::Path::Verb::Close:
                    builder.close();
                    break;
                }
            }
            return builder.detach();
        }
    };

    struct GpuSurface::Impl final
    {
        struct ImageEntry final
        {
            std::uint64_t Revision = 0;
            std::uint64_t LastUse = 0;
            std::size_t Bytes = 0;
            sk_sp<::SkImage> Image;
        };

        struct FontEntry final
        {
            sk_sp<::SkData> Data;
            sk_sp<::SkFontMgr> Manager;
            sk_sp<::SkTypeface> Typeface;
        };

        sk_sp<::GrDirectContext> Context;
        sk_sp<::SkSurface> Surface;
        static constexpr std::size_t ImageCacheBudget = 64 * 1024 * 1024;

        std::unordered_map<std::uint64_t, ImageEntry> Images;
        std::size_t ImageBytes = 0;
        std::uint64_t ImageClock = 0;
        std::unordered_map<const Skia::Typeface*, FontEntry> Fonts;
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        std::int32_t Texture = 0;
        bool InFrame = false;
        // Ganesh on the Vulkan window's device instead of the GL context.
        bool VulkanMode = false;
        VulkanInterop::Target VulkanTarget;
        std::int32_t PreviousFramebuffer = 0;
        std::array<std::int32_t, 4> PreviousViewport{};
        std::array<std::int32_t, 4> PreviousScissor{};
        bool PreviousScissorEnabled = false;
        std::int32_t PreviousProgram = 0;
        std::int32_t PreviousActiveTexture = static_cast<std::int32_t>(GL::TextureUnit::Texture0);
        std::array<std::int32_t, 2> PreviousTexture2D{};
        std::array<bool, 2> PreviousTexture2DEnabled{};
        std::int32_t PreviousBlendSrc = static_cast<std::int32_t>(GL::BlendingFactor::One);
        std::int32_t PreviousBlendDst = static_cast<std::int32_t>(GL::BlendingFactor::OneMinusSrcAlpha);
        std::int32_t PreviousDepthFunction = static_cast<std::int32_t>(GL::DepthFunction::Less);
        bool PreviousDepthWrite = true;
        std::array<std::int32_t, 4> PreviousColorWrite{1, 1, 1, 1};
        bool PreviousBlendEnabled = false;
        bool PreviousDepthEnabled = false;
        bool PreviousCullEnabled = false;
        bool PreviousStencilEnabled = false;
        bool PreviousAlphaEnabled = false;
        bool PreviousPolygonOffsetEnabled = false;
        std::int32_t PreviousPackSwapBytes = 0;
        std::int32_t PreviousPackLsbFirst = 0;
        std::int32_t PreviousPackRowLength = 0;
        std::int32_t PreviousPackSkipRows = 0;
        std::int32_t PreviousPackSkipPixels = 0;
        std::int32_t PreviousPackAlignment = 4;
        std::int32_t PreviousUnpackSwapBytes = 0;
        std::int32_t PreviousUnpackLsbFirst = 0;
        std::int32_t PreviousUnpackRowLength = 0;
        std::int32_t PreviousUnpackSkipRows = 0;
        std::int32_t PreviousUnpackSkipPixels = 0;
        std::int32_t PreviousUnpackAlignment = 4;
        std::int32_t PreviousUnpackSkipImages = 0;
        std::int32_t PreviousUnpackImageHeight = 0;
        std::int32_t PreviousPixelPackBuffer = 0;
        std::int32_t PreviousPixelUnpackBuffer = 0;
        int PreviousArrayBuffer = 0;
        int PreviousFrontFace = 0x0901;
        int PreviousBlendEquationRgb = 0x8006;
        int PreviousBlendEquationAlpha = 0x8006;
        std::vector<int> PreviousUnitTextures;
        std::vector<int> PreviousUnitRectangles;
        ExtraGl Extra;

        void CaptureExtraState() noexcept
        {
            Extra.Load();
            PreviousArrayBuffer = Extra.Get(GlArrayBufferBinding, 0);
            PreviousFrontFace = Extra.Get(GlFrontFace, 0x0901);
            PreviousBlendEquationRgb = Extra.Get(GlBlendEquationRgb, 0x8006);
            PreviousBlendEquationAlpha = Extra.Get(GlBlendEquationAlpha, 0x8006);
            const int units = std::clamp(Extra.Get(GlMaxCombinedTextureUnits, 8), 1, 32);
            PreviousUnitTextures.assign(static_cast<std::size_t>(units), 0);
            PreviousUnitRectangles.assign(static_cast<std::size_t>(units), 0);
            if (Extra.ActiveTexture == nullptr)
            {
                return;
            }
            for (int i = 0; i < units; ++i)
            {
                Extra.ActiveTexture(GlTexture0 + static_cast<unsigned>(i));
                PreviousUnitTextures[static_cast<std::size_t>(i)] = Extra.Get(GlTextureBinding2DRaw, 0);
                PreviousUnitRectangles[static_cast<std::size_t>(i)] = Extra.Get(GlTextureBindingRectangle, 0);
            }
            Extra.ActiveTexture(static_cast<unsigned>(PreviousActiveTexture));
        }

        void RestoreExtraState() noexcept
        {
            Extra.Load();
            // The game draws with VAO 0, client memory and immediate mode.
            if (Extra.BindVertexArray != nullptr)
            {
                Extra.BindVertexArray(0);
            }
            if (Extra.BindBuffer != nullptr)
            {
                Extra.BindBuffer(GlElementArrayBuffer, 0);
                Extra.BindBuffer(GlArrayBuffer, static_cast<unsigned>(PreviousArrayBuffer));
            }
            if (Extra.DisableVertexAttribArray != nullptr)
            {
                // Attribute 0 aliases gl_Vertex in the compatibility profile.
                // The game enables the generic arrays it draws with before
                // every draw and disables them after, so none is left on here.
                const int attribs = std::clamp(Extra.Get(GlMaxVertexAttribs, 16), 1, 32);
                for (int i = 0; i < attribs; ++i)
                {
                    Extra.DisableVertexAttribArray(static_cast<unsigned>(i));
                    // Ganesh draws instanced: a divisor left on an attribute
                    // the game's fixed-function arrays alias (2 is the normal,
                    // 3 the colour on NVIDIA) hands every vertex of a draw the
                    // first vertex's value -- flat lighting, wrong colours.
                    if (Extra.VertexAttribDivisor != nullptr)
                        Extra.VertexAttribDivisor(static_cast<unsigned>(i), 0);
                }
            }
            if (Extra.FrontFace != nullptr)
            {
                Extra.FrontFace(static_cast<unsigned>(PreviousFrontFace));
            }
            if (Extra.BlendEquationSeparate != nullptr)
            {
                Extra.BlendEquationSeparate(static_cast<unsigned>(PreviousBlendEquationRgb),
                    static_cast<unsigned>(PreviousBlendEquationAlpha));
            }
            if (Extra.ActiveTexture == nullptr)
            {
                return;
            }
            for (std::size_t i = 0; i < PreviousUnitTextures.size(); ++i)
            {
                Extra.ActiveTexture(GlTexture0 + static_cast<unsigned>(i));
                if (Extra.BindSampler != nullptr)
                {
                    Extra.BindSampler(static_cast<unsigned>(i), 0);
                }
                if (Extra.BindTexture != nullptr)
                {
                    Extra.BindTexture(GlTextureRectangle, static_cast<unsigned>(PreviousUnitRectangles[i]));
                    Extra.BindTexture(GlTexture2D, static_cast<unsigned>(PreviousUnitTextures[i]));
                }
            }
            Extra.ActiveTexture(static_cast<unsigned>(PreviousActiveTexture));
        }

        void EnsureContext()
        {
            if (Context != nullptr && !Context->abandoned())
            {
                return;
            }
            sk_sp<const ::GrGLInterface> interface = ::GrGLMakeAssembledInterface(nullptr, ResolveSkiaGl);
            if (interface == nullptr)
            {
                throw std::runtime_error("Skia could not assemble the current OpenGL interface.");
            }
#if FRUITY_SKIA_GANESH_V2
            Context = ::GrDirectContexts::MakeGL(std::move(interface));
#else
            Context = ::GrDirectContext::MakeGL(std::move(interface));
#endif
            if (Context == nullptr)
            {
                throw std::runtime_error("Skia could not create a Ganesh OpenGL context.");
            }
        }

        void EnsureSurface(std::int32_t width, std::int32_t height)
        {
            if (Surface != nullptr && Width == width && Height == height)
            {
                return;
            }
            if (!InFrame)
            {
                throw std::logic_error("Skia GPU surface resized outside a render frame.");
            }
#if defined(FRUITY_SKIA_VULKAN)
            if (VulkanMode)
            {
                Surface.reset();
                Surface = VulkanInterop::MakeSurface(*Context, VulkanTarget, width, height);
                Width = width;
                Height = height;
                Texture = 0;
                Images.clear();
                ImageBytes = 0;
                ImageClock = 0;
                Surface->getCanvas()->clear(SK_ColorTRANSPARENT);
                return;
            }
#endif
            const ::SkImageInfo info = ::SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
#if !defined(__ANDROID__)
            Rhi::OpenGL::AdmitInteropTextureStorage(Rhi::TextureFormat::RGBA8Unorm, width, height);
#endif
#if FRUITY_SKIA_GANESH_V2
            Surface = ::SkSurfaces::RenderTarget(Context.get(), skgpu::Budgeted::kYes, info, 0,
                kBottomLeft_GrSurfaceOrigin, nullptr);
#else
            Surface = ::SkSurface::MakeRenderTarget(Context.get(), SkBudgeted::kYes, info, 0,
                kBottomLeft_GrSurfaceOrigin, nullptr);
#endif
            if (Surface == nullptr)
            {
                throw std::runtime_error("Skia could not allocate the launcher Ganesh surface.");
            }
            Width = width;
            Height = height;
            Texture = 0;
            Images.clear();
            ImageBytes = 0;
            ImageClock = 0;
            Surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        }

        [[nodiscard]] ::SkCanvas& Canvas()
        {
            if (Surface == nullptr)
            {
                throw std::logic_error("Skia GPU surface has not been allocated.");
            }
            return *Surface->getCanvas();
        }

        [[nodiscard]] sk_sp<::SkImage> ImageFor(const Bitmap& bitmap, std::uint64_t revision)
        {
            const std::uint64_t identity = bitmap.Identity();
            auto found = Images.find(identity);
            if (found != Images.end() && found->second.Image != nullptr
                && found->second.Revision == revision)
            {
                found->second.LastUse = ++ImageClock;
                return found->second.Image;
            }

            if (found != Images.end())
            {
                ImageBytes -= std::min(ImageBytes, found->second.Bytes);
                Images.erase(found);
            }

            if (bitmap.Width() <= 0 || bitmap.Height() <= 0 || bitmap.Pixels() == nullptr)
            {
                return nullptr;
            }
            const ::SkImageInfo info = ::SkImageInfo::Make(bitmap.Width(), bitmap.Height(),
                kRGBA_8888_SkColorType, kPremul_SkAlphaType);
            const ::SkPixmap pixmap(info, bitmap.Pixels(), static_cast<std::size_t>(bitmap.Width()) * 4);
#if FRUITY_SKIA_GANESH_V2
            sk_sp<::SkImage> image = ::SkImages::RasterFromPixmapCopy(pixmap);
#else
            sk_sp<::SkImage> image = ::SkImage::MakeRasterCopy(pixmap);
#endif
            if (image == nullptr)
            {
                return nullptr;
            }

            const std::size_t width = static_cast<std::size_t>(bitmap.Width());
            const std::size_t height = static_cast<std::size_t>(bitmap.Height());
            const std::size_t bytes = width > ImageCacheBudget / 4 / std::max<std::size_t>(height, 1)
                ? ImageCacheBudget + 1
                : width * height * 4;
            if (bytes > ImageCacheBudget)
            {
                return image;
            }

            while (!Images.empty() && bytes > ImageCacheBudget - ImageBytes)
            {
                auto victim = std::min_element(Images.begin(), Images.end(),
                    [](const auto& left, const auto& right)
                    {
                        return left.second.LastUse < right.second.LastUse;
                    });
                if (victim == Images.end())
                {
                    break;
                }
                ImageBytes -= std::min(ImageBytes, victim->second.Bytes);
                Images.erase(victim);
            }

            ImageEntry entry;
            entry.Revision = revision;
            entry.LastUse = ++ImageClock;
            entry.Bytes = bytes;
            entry.Image = image;
            Images.emplace(identity, std::move(entry));
            ImageBytes += bytes;
            return image;
        }

        [[nodiscard]] sk_sp<::SkTypeface> FontFor(const Skia::Typeface& typeface,
            const std::uint8_t* data, std::size_t size)
        {
            auto found = Fonts.find(&typeface);
            if (found != Fonts.end())
            {
                return found->second.Typeface;
            }
            if (data == nullptr || size == 0)
            {
                return nullptr;
            }
            FontEntry entry;
            entry.Data = ::SkData::MakeWithCopy(data, size);
#if FRUITY_SKIA_FONTMGR_DATA
            std::array<sk_sp<::SkData>, 1> fontData{entry.Data};
            entry.Manager = ::SkFontMgr_New_Custom_Data(
                ::SkSpan<sk_sp<::SkData>>(fontData.data(), fontData.size()));
            if (entry.Manager != nullptr)
            {
                entry.Typeface = entry.Manager->makeFromData(entry.Data);
            }
#else
            entry.Typeface = ::SkTypeface::MakeFromData(entry.Data);
#endif
            sk_sp<::SkTypeface> result = entry.Typeface;
            Fonts.emplace(&typeface, std::move(entry));
            return result;
        }

        static void RestoreEnabled(GL::EnableCap cap, bool enabled) noexcept
        {
            if (enabled)
            {
                GL::Enable(cap);
            }
            else
            {
                GL::Disable(cap);
            }
        }

        void RestoreGlState() noexcept
        {
            RestoreExtraState();
            GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, PreviousFramebuffer);
            GL::Viewport(PreviousViewport[0], PreviousViewport[1], PreviousViewport[2], PreviousViewport[3]);
            GL::Scissor(PreviousScissor[0], PreviousScissor[1], PreviousScissor[2], PreviousScissor[3]);
            RestoreEnabled(GL::EnableCap::ScissorTest, PreviousScissorEnabled);
            RestoreEnabled(GL::EnableCap::Blend, PreviousBlendEnabled);
            RestoreEnabled(GL::EnableCap::DepthTest, PreviousDepthEnabled);
            RestoreEnabled(GL::EnableCap::CullFace, PreviousCullEnabled);
            RestoreEnabled(GL::EnableCap::StencilTest, PreviousStencilEnabled);
            RestoreEnabled(GL::EnableCap::AlphaTest, PreviousAlphaEnabled);
            RestoreEnabled(GL::EnableCap::PolygonOffsetFill, PreviousPolygonOffsetEnabled);
            GL::BlendFunc(static_cast<GL::BlendingFactor>(PreviousBlendSrc),
                static_cast<GL::BlendingFactor>(PreviousBlendDst));
            GL::DepthFunc(static_cast<GL::DepthFunction>(PreviousDepthFunction));
            GL::DepthMask(PreviousDepthWrite);
            GL::ColorMask(PreviousColorWrite[0] != 0, PreviousColorWrite[1] != 0,
                PreviousColorWrite[2] != 0, PreviousColorWrite[3] != 0);
            GL::UseProgram(PreviousProgram);

            // Ganesh changes GL pixel transfer state while uploading raster
            // images. The game uploads room/model textures with client
            // pointers and assumes the OpenGL defaults, so leaking any of
            // these values (especially UNPACK_ROW_LENGTH or a PBO binding)
            // corrupts the next texture upload.
            GL::BindBuffer(GL::BufferTarget::PixelPackBuffer, PreviousPixelPackBuffer);
            GL::BindBuffer(GL::BufferTarget::PixelUnpackBuffer, PreviousPixelUnpackBuffer);
            GL::PixelStore(GL::PixelStoreParameter::PackSwapBytes, PreviousPackSwapBytes);
            GL::PixelStore(GL::PixelStoreParameter::PackLsbFirst, PreviousPackLsbFirst);
            GL::PixelStore(GL::PixelStoreParameter::PackRowLength, PreviousPackRowLength);
            GL::PixelStore(GL::PixelStoreParameter::PackSkipRows, PreviousPackSkipRows);
            GL::PixelStore(GL::PixelStoreParameter::PackSkipPixels, PreviousPackSkipPixels);
            GL::PixelStore(GL::PixelStoreParameter::PackAlignment, PreviousPackAlignment);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSwapBytes, PreviousUnpackSwapBytes);
            GL::PixelStore(GL::PixelStoreParameter::UnpackLsbFirst, PreviousUnpackLsbFirst);
            GL::PixelStore(GL::PixelStoreParameter::UnpackRowLength, PreviousUnpackRowLength);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipRows, PreviousUnpackSkipRows);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipPixels, PreviousUnpackSkipPixels);
            GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, PreviousUnpackAlignment);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipImages, PreviousUnpackSkipImages);
            GL::PixelStore(GL::PixelStoreParameter::UnpackImageHeight, PreviousUnpackImageHeight);

            for (std::size_t i = 0; i < PreviousTexture2D.size(); ++i)
            {
                GL::ActiveTexture(static_cast<GL::TextureUnit>(
                    static_cast<std::int32_t>(GL::TextureUnit::Texture0) + static_cast<std::int32_t>(i)));
                GL::BindTexture(GL::TextureTarget::Texture2D, PreviousTexture2D[i]);
                RestoreEnabled(GL::EnableCap::Texture2D, PreviousTexture2DEnabled[i]);
            }
            GL::ActiveTexture(static_cast<GL::TextureUnit>(PreviousActiveTexture));
        }
    };

    GpuSurface::GpuSurface()
        : _impl(std::make_unique<Impl>())
    {
    }

    GpuSurface::~GpuSurface()
    {
        if (_impl != nullptr && _impl->VulkanMode)
        {
            // Skia's work is complete at every EndFrame; drop the surface
            // before the image it wraps, and the context last.
            _impl->Surface.reset();
            _impl->Images.clear();
            _impl->Fonts.clear();
            _impl->VulkanTarget.Texture.reset();
            _impl->Context.reset();
            return;
        }
        if (_impl != nullptr && _impl->Context != nullptr)
        {
            _impl->Context->abandonContext();
            _impl->Surface.reset();
            _impl->Context.reset();
        }
    }

    GpuSurface::GpuSurface(GpuSurface&&) noexcept = default;
    GpuSurface& GpuSurface::operator=(GpuSurface&&) noexcept = default;

    std::int32_t GpuSurface::Width() const noexcept
    {
        return _impl != nullptr ? _impl->Width : 0;
    }

    std::int32_t GpuSurface::Height() const noexcept
    {
        return _impl != nullptr ? _impl->Height : 0;
    }

    std::int32_t GpuSurface::TextureId() const noexcept
    {
        return _impl != nullptr ? _impl->Texture : 0;
    }

    const ::MphRead::NativeRuntime::Rhi::Texture* GpuSurface::RhiTexture() const noexcept
    {
        return _impl != nullptr && _impl->VulkanMode ? _impl->VulkanTarget.Texture.get() : nullptr;
    }

    void GpuSurface::BeginFrame()
    {
        if (_impl->InFrame)
        {
            throw std::logic_error("Skia GPU render frame is already active.");
        }
        if (::MphRead::NativeRuntime::Rhi::ScenePresentsWindow())
        {
#if defined(FRUITY_SKIA_VULKAN)
            _impl->VulkanMode = true;
            if (_impl->Context == nullptr) _impl->Context = VulkanInterop::MakeContext();
            VulkanInterop::BeginFrame(_impl->VulkanTarget);
            _impl->InFrame = true;
            if (_impl->Surface != nullptr)
            {
                ::SkCanvas* canvas = _impl->Surface->getCanvas();
                canvas->restoreToCount(1);
                canvas->resetMatrix();
            }
            return;
#else
            // No GL context exists to fall back to, and a CPU frame is not a
            // fallback this renderer takes.
            throw std::runtime_error("This build's Skia has no Vulkan backend: the launcher cannot draw "
                "into a window that presents through Vulkan. Use -rhi opengl.");
#endif
        }
        _impl->PreviousFramebuffer = GL::GetInteger(GlFramebufferBinding);
        GL::GetIntegers(GlViewport, _impl->PreviousViewport.data());
        GL::GetIntegers(GlScissorBox, _impl->PreviousScissor.data());
        _impl->PreviousScissorEnabled = GL::IsEnabled(GL::EnableCap::ScissorTest);
        _impl->PreviousProgram = GL::GetInteger(GlCurrentProgram);
        _impl->PreviousActiveTexture = GL::GetInteger(GlActiveTexture);
        for (std::size_t i = 0; i < _impl->PreviousTexture2D.size(); ++i)
        {
            GL::ActiveTexture(static_cast<GL::TextureUnit>(
                static_cast<std::int32_t>(GL::TextureUnit::Texture0) + static_cast<std::int32_t>(i)));
            _impl->PreviousTexture2D[i] = GL::GetInteger(GlTextureBinding2D);
            _impl->PreviousTexture2DEnabled[i] = GL::IsEnabled(GL::EnableCap::Texture2D);
        }
        GL::ActiveTexture(static_cast<GL::TextureUnit>(_impl->PreviousActiveTexture));
        _impl->PreviousBlendSrc = GL::GetInteger(GlBlendSrcRgb);
        _impl->PreviousBlendDst = GL::GetInteger(GlBlendDstRgb);
        _impl->PreviousDepthFunction = GL::GetInteger(GlDepthFunc);
        _impl->PreviousDepthWrite = GL::GetInteger(GlDepthWriteMask) != 0;
        GL::GetIntegers(GlColorWriteMask, _impl->PreviousColorWrite.data());
        _impl->PreviousBlendEnabled = GL::IsEnabled(GL::EnableCap::Blend);
        _impl->PreviousDepthEnabled = GL::IsEnabled(GL::EnableCap::DepthTest);
        _impl->PreviousCullEnabled = GL::IsEnabled(GL::EnableCap::CullFace);
        _impl->PreviousStencilEnabled = GL::IsEnabled(GL::EnableCap::StencilTest);
        _impl->PreviousAlphaEnabled = GL::IsEnabled(GL::EnableCap::AlphaTest);
        _impl->PreviousPolygonOffsetEnabled = GL::IsEnabled(GL::EnableCap::PolygonOffsetFill);
        _impl->PreviousPackSwapBytes = GL::GetInteger(GlPackSwapBytes);
        _impl->PreviousPackLsbFirst = GL::GetInteger(GlPackLsbFirst);
        _impl->PreviousPackRowLength = GL::GetInteger(GlPackRowLength);
        _impl->PreviousPackSkipRows = GL::GetInteger(GlPackSkipRows);
        _impl->PreviousPackSkipPixels = GL::GetInteger(GlPackSkipPixels);
        _impl->PreviousPackAlignment = GL::GetInteger(GlPackAlignment);
        _impl->PreviousUnpackSwapBytes = GL::GetInteger(GlUnpackSwapBytes);
        _impl->PreviousUnpackLsbFirst = GL::GetInteger(GlUnpackLsbFirst);
        _impl->PreviousUnpackRowLength = GL::GetInteger(GlUnpackRowLength);
        _impl->PreviousUnpackSkipRows = GL::GetInteger(GlUnpackSkipRows);
        _impl->PreviousUnpackSkipPixels = GL::GetInteger(GlUnpackSkipPixels);
        _impl->PreviousUnpackAlignment = GL::GetInteger(GlUnpackAlignment);
        _impl->PreviousUnpackSkipImages = GL::GetInteger(GlUnpackSkipImages);
        _impl->PreviousUnpackImageHeight = GL::GetInteger(GlUnpackImageHeight);
        _impl->PreviousPixelPackBuffer = GL::GetInteger(GlPixelPackBufferBinding);
        _impl->PreviousPixelUnpackBuffer = GL::GetInteger(GlPixelUnpackBufferBinding);
        _impl->CaptureExtraState();
        _impl->InFrame = true;
        try
        {
            _impl->EnsureContext();
            _impl->Context->resetContext();
            if (_impl->Surface != nullptr)
            {
                ::SkCanvas* canvas = _impl->Surface->getCanvas();
                canvas->restoreToCount(1);
                canvas->resetMatrix();
            }
        }
        catch (...)
        {
            _impl->InFrame = false;
            _impl->RestoreGlState();
            throw;
        }
    }

    void GpuSurface::EndFrame()
    {
        if (_impl == nullptr || !_impl->InFrame)
        {
            return;
        }
#if defined(FRUITY_SKIA_VULKAN)
        if (_impl->VulkanMode)
        {
            _impl->InFrame = false;
            if (_impl->Surface != nullptr && _impl->Context != nullptr)
                VulkanInterop::EndFrame(*_impl->Context, *_impl->Surface, _impl->VulkanTarget);
            return;
        }
#endif
        try
        {
            if (_impl->Surface != nullptr && _impl->Context != nullptr)
            {
                _impl->Context->flushAndSubmit(_impl->Surface.get());
                ::GrGLTextureInfo info{};
#if FRUITY_SKIA_GANESH_V2
                const ::GrBackendTexture backend = ::SkSurfaces::GetBackendTexture(
                    _impl->Surface.get(), ::SkSurface::BackendHandleAccess::kFlushRead);
                if (backend.isValid() && ::GrBackendTextures::GetGLTextureInfo(backend, &info))
#else
                const ::GrBackendTexture backend = _impl->Surface->getBackendTexture(
                    ::SkSurface::kFlushRead_BackendHandleAccess);
                if (backend.isValid() && backend.getGLTextureInfo(&info))
#endif
                {
                    _impl->Texture = static_cast<std::int32_t>(info.fID);
                }
                else
                {
                    _impl->Texture = 0;
                }
            }
        }
        catch (...)
        {
            _impl->InFrame = false;
            _impl->RestoreGlState();
            throw;
        }
        _impl->InFrame = false;
        _impl->RestoreGlState();
    }

    void GpuSurface::Resize(std::int32_t width, std::int32_t height)
    {
        _impl->EnsureSurface(std::max(width, 1), std::max(height, 1));
    }

    void GpuSurface::Clear(Color color)
    {
        _impl->Canvas().clear(NativeColor(color));
    }

    void GpuSurface::ClearRect(Color color, std::int32_t x, std::int32_t y,
        std::int32_t width, std::int32_t height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        ::SkCanvas& canvas = _impl->Canvas();
        const int saved = canvas.save();
        canvas.resetMatrix();
        canvas.clipRect(::SkRect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height)), ::SkClipOp::kIntersect, false);
        canvas.drawColor(NativeColor(color), ::SkBlendMode::kSrc);
        canvas.restoreToCount(saved);
    }

    void GpuSurface::Save()
    {
        (void)_impl->Canvas().save();
    }

    void GpuSurface::SaveLayerAlpha(double opacity)
    {
#if FRUITY_SKIA_GANESH_V2
        (void)_impl->Canvas().saveLayerAlphaf(nullptr, static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
#else
        const double clamped = std::clamp(opacity, 0.0, 1.0);
        (void)_impl->Canvas().saveLayerAlpha(nullptr,
            static_cast<U8CPU>(clamped * 255.0 + 0.5));
#endif
    }

    void GpuSurface::Restore()
    {
        _impl->Canvas().restore();
    }

    void GpuSurface::Concat(const Matrix& matrix)
    {
        _impl->Canvas().concat(NativeMatrix(matrix));
    }

    void GpuSurface::SetMatrix(const Matrix& matrix)
    {
        _impl->Canvas().setMatrix(NativeMatrix(matrix));
    }

    Rect GpuSurface::DeviceClipBounds() const noexcept
    {
        if (_impl == nullptr || _impl->Surface == nullptr)
        {
            return {};
        }
        const ::SkIRect clip = _impl->Surface->getCanvas()->getDeviceClipBounds();
        return Rect{static_cast<double>(clip.left()), static_cast<double>(clip.top()),
            static_cast<double>(clip.right()), static_cast<double>(clip.bottom())};
    }

    void GpuSurface::ClipRect(const Rect& rect, bool antialias)
    {
        _impl->Canvas().clipRect(NativeRect(rect), ::SkClipOp::kIntersect, antialias);
    }

    void GpuSurface::ClipPath(const Path& path, bool antialias)
    {
        _impl->Canvas().clipPath(GpuAccess::Path(path), ::SkClipOp::kIntersect, antialias);
    }

    void GpuSurface::FillPath(const Path& path, const Paint& paint)
    {
        if (path.IsEmpty() || paint.Opacity <= 0.0)
        {
            return;
        }
        ::SkPaint native = NativePaint(paint);
        native.setStyle(::SkPaint::kFill_Style);
        _impl->Canvas().drawPath(GpuAccess::Path(path), native);
    }

    void GpuSurface::StrokePath(const Path& path, const StrokeStyle& stroke, const Paint& paint)
    {
        if (path.IsEmpty() || paint.Opacity <= 0.0 || stroke.Width <= 0.0)
        {
            return;
        }
        ::SkPaint native = NativePaint(paint);
        native.setStyle(::SkPaint::kStroke_Style);
        native.setStrokeWidth(static_cast<float>(stroke.Width));
        native.setStrokeMiter(static_cast<float>(stroke.MiterLimit));
        native.setStrokeCap(stroke.Cap == LineCap::Round ? ::SkPaint::kRound_Cap
            : stroke.Cap == LineCap::Square ? ::SkPaint::kSquare_Cap : ::SkPaint::kButt_Cap);
        native.setStrokeJoin(stroke.Join == LineJoin::Round ? ::SkPaint::kRound_Join
            : stroke.Join == LineJoin::Bevel ? ::SkPaint::kBevel_Join : ::SkPaint::kMiter_Join);
        if (!stroke.Dashes.empty())
        {
            std::vector<::SkScalar> intervals;
            intervals.reserve(stroke.Dashes.size());
            for (const double dash : stroke.Dashes)
            {
                intervals.push_back(static_cast<::SkScalar>(dash * stroke.Width));
            }
            if ((intervals.size() & 1U) != 0U)
            {
                const std::vector<::SkScalar> copy = intervals;
                intervals.insert(intervals.end(), copy.begin(), copy.end());
            }
#if FRUITY_SKIA_GANESH_V2
            native.setPathEffect(::SkDashPathEffect::Make(
                ::SkSpan<const ::SkScalar>(intervals.data(), intervals.size()),
                static_cast<::SkScalar>(stroke.DashOffset * stroke.Width)));
#else
            native.setPathEffect(::SkDashPathEffect::Make(intervals.data(),
                static_cast<int>(intervals.size()),
                static_cast<::SkScalar>(stroke.DashOffset * stroke.Width)));
#endif
        }
        _impl->Canvas().drawPath(GpuAccess::Path(path), native);
    }

    void GpuSurface::DrawBitmap(const Bitmap& bitmap, const Rect& source, const Rect& destination,
        FilterQuality quality, double opacity, BlendMode blend)
    {
        if (bitmap.Width() <= 0 || bitmap.Height() <= 0 || source.IsEmpty()
            || destination.IsEmpty() || opacity <= 0.0)
        {
            return;
        }
        const std::uint64_t revision = bitmap.Revision();
        const sk_sp<::SkImage> image = _impl->ImageFor(bitmap, revision);
        if (image == nullptr)
        {
            return;
        }
        ::SkPaint paint;
        paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
        paint.setBlendMode(NativeBlend(blend));
        _impl->Canvas().drawImageRect(image, NativeRect(source), NativeRect(destination),
            NativeSampling(quality), &paint, ::SkCanvas::kStrict_SrcRectConstraint);
    }

    void GpuSurface::DrawBoxShadow(const Path& shape, const BoxShadowSpec& shadow, const Rect& bounds,
        const std::array<Point, 4>& radii)
    {
        if (shadow.ShadowColor.A == 0)
        {
            return;
        }
        const double spreadAmount = shadow.Inset ? -shadow.Spread : shadow.Spread;
        std::array<Point, 4> grown = radii;
        for (Point& radius : grown)
        {
            if (radius.X > 0.0 || radius.Y > 0.0)
            {
                radius.X = std::max(0.0, radius.X + spreadAmount);
                radius.Y = std::max(0.0, radius.Y + spreadAmount);
            }
        }
        Path spread;
        spread.AddRoundRect(Rect{bounds.Left - spreadAmount + shadow.OffsetX,
            bounds.Top - spreadAmount + shadow.OffsetY,
            bounds.Right + spreadAmount + shadow.OffsetX,
            bounds.Bottom + spreadAmount + shadow.OffsetY}, grown);

        ::SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor4f(NativeColor(shadow.ShadowColor));
        const float sigma = static_cast<float>(ConvertRadiusToSigma(shadow.Blur));
        if (sigma > 0.01F)
        {
            paint.setMaskFilter(::SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma, true));
        }

        ::SkCanvas& canvas = _impl->Canvas();
        const int saved = canvas.save();
        const ::SkPath nativeShape = GpuAccess::Path(shape);
        if (!shadow.Inset)
        {
            // Round rects go through clipRRect/drawRRect so Ganesh uses its
            // analytic blur and clip instead of a CPU-rasterised mask.
            ::SkRRect shapeRRect;
            ::SkRRect spreadRRect;
            const ::SkPath nativeSpread = GpuAccess::Path(spread);
            if (nativeShape.isRRect(&shapeRRect) || nativeShape.isRect(nullptr))
            {
                if (!nativeShape.isRRect(&shapeRRect))
                {
                    shapeRRect.setRect(nativeShape.getBounds());
                }
                canvas.clipRRect(shapeRRect, ::SkClipOp::kDifference, true);
            }
            else
            {
                canvas.clipPath(nativeShape, ::SkClipOp::kDifference, true);
            }
            if (nativeSpread.isRRect(&spreadRRect))
            {
                canvas.drawRRect(spreadRRect, paint);
            }
            else if (nativeSpread.isRect(nullptr))
            {
                canvas.drawRect(nativeSpread.getBounds(), paint);
            }
            else
            {
                canvas.drawPath(nativeSpread, paint);
            }
        }
        else
        {
            canvas.clipPath(nativeShape, ::SkClipOp::kIntersect, true);
            ::SkPathBuilder inverse;
            inverse.setFillType(::SkPathFillType::kEvenOdd);
            const double reach = std::ceil(std::max(1.0,
                shadow.Blur * 2.0 + std::abs(shadow.Spread) + 4.0));
            inverse.addRect(::SkRect::MakeLTRB(static_cast<float>(-reach), static_cast<float>(-reach),
                static_cast<float>(Width() + reach), static_cast<float>(Height() + reach)));
            inverse.addPath(GpuAccess::Path(spread));
            canvas.drawPath(inverse.detach(), paint);
        }
        canvas.restoreToCount(saved);
    }

    void GpuSurface::DrawText(std::u32string_view text, const Skia::Typeface& typeface,
        double size, Point origin, const Paint& paint)
    {
        if (text.empty() || size <= 0.0 || paint.Opacity <= 0.0)
        {
            return;
        }
        const sk_sp<::SkTypeface> face = _impl->FontFor(
            typeface, typeface.FontData(), typeface.FontDataSize());
        if (face == nullptr)
        {
            return;
        }
        ::SkFont font(face, static_cast<float>(size));
        ::SkPaint native = NativePaint(paint);
        double pen = 0.0;
        char32_t previous = 0;
        for (const char32_t code : text)
        {
            if (previous != 0)
            {
                pen += typeface.Kerning(previous, code, size);
            }
            _impl->Canvas().drawSimpleText(&code, sizeof(code), ::SkTextEncoding::kUTF32,
                static_cast<float>(origin.X + pen), static_cast<float>(origin.Y), font, native);
            pen += typeface.Advance(code, size);
            previous = code;
        }
    }
}
