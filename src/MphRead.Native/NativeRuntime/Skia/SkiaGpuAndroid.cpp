#include "Skia.hpp"

#if defined(__ANDROID__)

#include <stdexcept>
#include <utility>

namespace MphRead::NativeRuntime::Skia
{
    struct GpuSurface::Impl final
    {
        std::int32_t Width = 0;
        std::int32_t Height = 0;
    };

    GpuSurface::GpuSurface()
        : _impl(std::make_unique<Impl>())
    {
    }

    GpuSurface::~GpuSurface() = default;
    GpuSurface::GpuSurface(GpuSurface&&) noexcept = default;
    GpuSurface& GpuSurface::operator=(GpuSurface&&) noexcept = default;

    std::int32_t GpuSurface::Width() const noexcept
    {
        return _impl == nullptr ? 0 : _impl->Width;
    }

    std::int32_t GpuSurface::Height() const noexcept
    {
        return _impl == nullptr ? 0 : _impl->Height;
    }

    const ::MphRead::NativeRuntime::Rhi::Texture* GpuSurface::RhiTexture() const noexcept
    {
        return nullptr;
    }

    std::int32_t GpuSurface::TextureId() const noexcept
    {
        return 0;
    }

    void GpuSurface::Resize(std::int32_t width, std::int32_t height)
    {
        _impl->Width = width;
        _impl->Height = height;
    }

    void GpuSurface::BeginFrame()
    {
        throw std::runtime_error(
            "the Android launcher uses the CPU UI surface, not Skia Ganesh"
        );
    }

    void GpuSurface::EndFrame()
    {
    }

    void GpuSurface::Clear(Color)
    {
    }

    void GpuSurface::ClearRect(
        Color,
        std::int32_t,
        std::int32_t,
        std::int32_t,
        std::int32_t)
    {
    }

    void GpuSurface::Save()
    {
    }

    void GpuSurface::SaveLayerAlpha(double)
    {
    }

    void GpuSurface::Restore()
    {
    }

    void GpuSurface::Concat(const Matrix&)
    {
    }

    void GpuSurface::SetMatrix(const Matrix&)
    {
    }

    void GpuSurface::ClipRect(const Rect&, bool)
    {
    }

    void GpuSurface::ClipPath(const Path&, bool)
    {
    }

    Rect GpuSurface::DeviceClipBounds() const noexcept
    {
        return Rect{
            0.0,
            0.0,
            static_cast<double>(Width()),
            static_cast<double>(Height())
        };
    }

    void GpuSurface::FillPath(const Path&, const Paint&)
    {
    }

    void GpuSurface::StrokePath(
        const Path&,
        const StrokeStyle&,
        const Paint&)
    {
    }

    void GpuSurface::DrawBitmap(
        const Bitmap&,
        const Rect&,
        const Rect&,
        FilterQuality,
        double,
        BlendMode)
    {
    }

    void GpuSurface::DrawBoxShadow(
        const Path&,
        const BoxShadowSpec&,
        const Rect&,
        const std::array<Point, 4>&)
    {
    }

    void GpuSurface::DrawText(
        std::u32string_view,
        const Typeface&,
        double,
        Point,
        const Paint&)
    {
    }
}

#endif
