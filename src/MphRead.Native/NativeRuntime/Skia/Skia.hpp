#pragma once

// The subset of SkiaSharp that the launcher's Avalonia controls use.
//
// The desktop top level is backed by real Skia Ganesh on the game's current
// OpenGL context. Bitmap-backed canvases remain available for RenderTargetBitmap
// and other off-screen compatibility paths that genuinely need CPU pixels. The
// public facade stays the same for the native Avalonia port, so choosing GPU or
// CPU rendering does not leak into individual controls.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MphRead::NativeRuntime::Rhi
{
    class Texture;
}

namespace MphRead::NativeRuntime::Skia
{
    struct Point final
    {
        double X = 0.0;
        double Y = 0.0;
    };

    struct Rect final
    {
        double Left = 0.0;
        double Top = 0.0;
        double Right = 0.0;
        double Bottom = 0.0;

        [[nodiscard]] static constexpr Rect FromXYWH(double x, double y, double width, double height) noexcept
        {
            return Rect{x, y, x + width, y + height};
        }
        [[nodiscard]] constexpr double Width() const noexcept { return Right - Left; }
        [[nodiscard]] constexpr double Height() const noexcept { return Bottom - Top; }
        [[nodiscard]] constexpr bool IsEmpty() const noexcept { return !(Right > Left && Bottom > Top); }
        [[nodiscard]] Rect Intersect(const Rect& other) const noexcept;
        [[nodiscard]] Rect Union(const Rect& other) const noexcept;
    };

    // Premultiplied is what is stored; this is straight colour, as a brush has it.
    struct Color final
    {
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
        std::uint8_t A = 0;
    };

    // An affine transform: x' = ScaleX*x + SkewX*y + TransX, y' = SkewY*x + ScaleY*y + TransY.
    struct Matrix final
    {
        double ScaleX = 1.0;
        double SkewX = 0.0;
        double TransX = 0.0;
        double SkewY = 0.0;
        double ScaleY = 1.0;
        double TransY = 0.0;

        [[nodiscard]] static Matrix Translation(double x, double y) noexcept;
        [[nodiscard]] static Matrix Scale(double x, double y) noexcept;
        [[nodiscard]] static Matrix Rotation(double radians) noexcept;
        // this, then other: a point goes through this first.
        [[nodiscard]] Matrix Then(const Matrix& other) const noexcept;
        [[nodiscard]] std::optional<Matrix> Invert() const noexcept;
        [[nodiscard]] Point Map(Point p) const noexcept;
        [[nodiscard]] Rect MapRect(const Rect& rect) const noexcept;
        [[nodiscard]] bool IsIdentity() const noexcept;
        [[nodiscard]] bool IsScaleTranslate() const noexcept;
    };

    enum class FillRule : std::uint8_t { NonZero, EvenOdd };

    class Path final
    {
    public:
        void MoveTo(Point p);
        void LineTo(Point p);
        void QuadTo(Point control, Point end);
        void CubicTo(Point c1, Point c2, Point end);
        // SVG's elliptical arc, which is what a StreamGeometry's ArcTo is.
        void ArcTo(Point end, double radiusX, double radiusY, double rotationDegrees, bool largeArc, bool clockwise);
        void Close();

        void AddRect(const Rect& rect);
        // Radii per corner, clockwise from the top left, each (x, y).
        void AddRoundRect(const Rect& rect, std::array<Point, 4> radii);
        void AddEllipse(Point centre, double radiusX, double radiusY);
        void AddPath(const Path& other);

        [[nodiscard]] bool IsEmpty() const noexcept { return _verbs.empty(); }
        [[nodiscard]] Rect Bounds() const noexcept;

        FillRule Rule = FillRule::NonZero;

        // The outline as polylines in device space, each marked closed or open.
        struct Contour final
        {
            std::vector<Point> Points;
            bool Closed = false;
        };
        [[nodiscard]] std::vector<Contour> Flatten(const Matrix& matrix, double tolerance = 0.2) const;

    private:
        friend struct GpuAccess;
        enum class Verb : std::uint8_t { Move, Line, Quad, Cubic, Close };
        std::vector<Verb> _verbs;
        std::vector<Point> _points;
        Point _last{};
        Point _start{};
    };

    enum class LineCap : std::uint8_t { Butt, Round, Square };
    enum class LineJoin : std::uint8_t { Miter, Round, Bevel };

    struct GradientStop final
    {
        Color StopColor{};
        double Offset = 0.0;
    };

    enum class SpreadMethod : std::uint8_t { Pad, Reflect, Repeat };

    enum class GradientKind : std::uint8_t { Linear, Radial };

    struct GradientDescriptor final
    {
        GradientKind Kind = GradientKind::Linear;
        Point Start{};
        Point End{};
        Point Centre{};
        Point Origin{};
        double RadiusX = 0.0;
        double RadiusY = 0.0;
        std::vector<GradientStop> Stops{};
        SpreadMethod Spread = SpreadMethod::Pad;
        Matrix LocalToDevice{};
    };

    class Shader
    {
    public:
        virtual ~Shader() = default;
        virtual void Shade(double x, double y, float out[4]) const = 0;
        [[nodiscard]] virtual const GradientDescriptor& Descriptor() const noexcept = 0;
    };

    [[nodiscard]] std::shared_ptr<Shader> LinearGradient(Point start, Point end, std::vector<GradientStop> stops,
        SpreadMethod spread, const Matrix& localToDevice);
    [[nodiscard]] std::shared_ptr<Shader> RadialGradient(Point centre, Point origin, double radiusX, double radiusY,
        std::vector<GradientStop> stops, SpreadMethod spread, const Matrix& localToDevice);

    struct Paint final
    {
        Color Solid{0, 0, 0, 255};
        std::shared_ptr<Shader> Gradient{};
        double Opacity = 1.0;
        // SKPaint.IsAntialias: false draws hard, whole-pixel edges.
        bool Antialias = true;
    };

    struct StrokeStyle final
    {
        double Width = 1.0;
        LineCap Cap = LineCap::Butt;
        LineJoin Join = LineJoin::Miter;
        double MiterLimit = 10.0;
        std::vector<double> Dashes{};
        double DashOffset = 0.0;
    };

    class GpuSurface;

    // Premultiplied RGBA, rows top first, tightly packed.
    class Bitmap final
    {
    public:
        Bitmap();
        Bitmap(std::int32_t width, std::int32_t height);
        Bitmap(const Bitmap& other);
        Bitmap& operator=(const Bitmap& other);
        Bitmap(Bitmap&& other) noexcept;
        Bitmap& operator=(Bitmap&& other) noexcept;

        [[nodiscard]] std::int32_t Width() const noexcept { return _width; }
        [[nodiscard]] std::int32_t Height() const noexcept { return _height; }
        [[nodiscard]] std::uint8_t* Pixels() noexcept { ++_revision; return _pixels.data(); }
        [[nodiscard]] const std::uint8_t* Pixels() const noexcept { return _pixels.data(); }
        void Clear(Color color = {});
        void ClearRect(Color color, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
        void Resize(std::int32_t width, std::int32_t height);

        // Straight RGBA in, as a decoded file is.
        [[nodiscard]] static std::shared_ptr<Bitmap> FromStraightRgba(
            std::int32_t width, std::int32_t height, const std::uint8_t* rgba);
        // Premultiplied RGBA in, as a renderer hands it over.
        [[nodiscard]] static std::shared_ptr<Bitmap> FromPremultipliedRgba(
            std::int32_t width, std::int32_t height, const std::uint8_t* rgba, std::int32_t stride);
        // PNG, JPEG, BMP: whatever stb_image reads. Null when it cannot.
        [[nodiscard]] static std::shared_ptr<Bitmap> Decode(const std::uint8_t* data, std::size_t length);

    private:
        friend class GpuSurface;
        [[nodiscard]] std::uint64_t Revision() const noexcept { return _revision; }
        [[nodiscard]] std::uint64_t Identity() const noexcept { return _identity; }

        std::int32_t _width = 0;
        std::int32_t _height = 0;
        std::vector<std::uint8_t> _pixels;
        std::uint64_t _revision = 1;
        std::uint64_t _identity = 0;
    };

    enum class FilterQuality : std::uint8_t { None, Low, Medium, High };
    // SKBlendMode, the two the launcher draws with.
    enum class BlendMode : std::uint8_t { SrcOver, Overlay };

    // A font file loaded once and kept for the process.
    class Typeface final
    {
    public:
        [[nodiscard]] static std::shared_ptr<Typeface> FromFile(const std::string& path);
        [[nodiscard]] static std::shared_ptr<Typeface> FromData(std::vector<std::uint8_t> data);
        // The platform's default sans serif, at a weight.
        [[nodiscard]] static std::shared_ptr<Typeface> Default(std::int32_t weight);
        ~Typeface();

        struct Metrics final
        {
            double Ascent = 0.0;
            double Descent = 0.0;
            double LineGap = 0.0;
        };
        // In pixels at this size, ascent and descent both positive.
        [[nodiscard]] Metrics MetricsAt(double size) const;
        [[nodiscard]] double Advance(char32_t code, double size) const;
        [[nodiscard]] double Kerning(char32_t left, char32_t right, double size) const;
        [[nodiscard]] bool HasGlyph(char32_t code) const;

        struct GlyphImage final
        {
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            std::int32_t Left = 0;
            std::int32_t Top = 0;
            std::vector<std::uint8_t> Coverage;
        };
        [[nodiscard]] const GlyphImage* Rasterize(char32_t code, double size) const;

    private:
        friend class GpuSurface;
        Typeface() = default;
        [[nodiscard]] const std::uint8_t* FontData() const noexcept;
        [[nodiscard]] std::size_t FontDataSize() const noexcept;
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    struct BoxShadowSpec final
    {
        double OffsetX = 0.0;
        double OffsetY = 0.0;
        double Blur = 0.0;
        double Spread = 0.0;
        Color ShadowColor{};
        bool Inset = false;
    };

    class GpuSurface final
    {
    public:
        GpuSurface();
        ~GpuSurface();
        GpuSurface(const GpuSurface&) = delete;
        GpuSurface& operator=(const GpuSurface&) = delete;
        GpuSurface(GpuSurface&&) noexcept;
        GpuSurface& operator=(GpuSurface&&) noexcept;

        void Resize(std::int32_t width, std::int32_t height);
        [[nodiscard]] std::int32_t Width() const noexcept;
        [[nodiscard]] std::int32_t Height() const noexcept;
        [[nodiscard]] std::int32_t TextureId() const noexcept;
        // The Vulkan window's UI target, when Ganesh draws through Vulkan;
        // null under OpenGL, where TextureId() names the texture instead.
        [[nodiscard]] const ::MphRead::NativeRuntime::Rhi::Texture* RhiTexture() const noexcept;
        void BeginFrame();
        void EndFrame();
        void Clear(Color color);
        void ClearRect(Color color, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

    private:
        friend class Canvas;
        struct Impl;
        std::unique_ptr<Impl> _impl;

        void Save();
        void SaveLayerAlpha(double opacity);
        void Restore();
        void Concat(const Matrix& matrix);
        void SetMatrix(const Matrix& matrix);
        void ClipRect(const Rect& rect, bool antialias);
        void ClipPath(const Path& path, bool antialias);
        [[nodiscard]] Rect DeviceClipBounds() const noexcept;
        void FillPath(const Path& path, const Paint& paint);
        void StrokePath(const Path& path, const StrokeStyle& stroke, const Paint& paint);
        void DrawBitmap(const Bitmap& bitmap, const Rect& source, const Rect& destination,
            FilterQuality quality, double opacity, BlendMode blend);
        void DrawBoxShadow(const Path& shape, const BoxShadowSpec& shadow, const Rect& shapeBounds,
            const std::array<Point, 4>& radii);
        void DrawText(std::u32string_view text, const Typeface& typeface, double size, Point origin,
            const Paint& paint);
    };

    class Canvas final
    {
    public:
        explicit Canvas(Bitmap& target);
        explicit Canvas(GpuSurface& target);

        [[nodiscard]] std::int32_t Width() const noexcept { return _gpu != nullptr ? _gpu->Width() : _base.Width(); }
        [[nodiscard]] std::int32_t Height() const noexcept { return _gpu != nullptr ? _gpu->Height() : _base.Height(); }

        void Clear(Color color);

        void Save();
        void Restore();
        [[nodiscard]] std::size_t SaveCount() const noexcept { return _states.size(); }

        void Concat(const Matrix& matrix);
        void SetMatrix(const Matrix& matrix);
        [[nodiscard]] const Matrix& TotalMatrix() const noexcept;

        void ClipRect(const Rect& rect, bool antialias = true);
        void ClipPath(const Path& path, bool antialias = true);
        // The device rectangle drawing is limited to, for a caller that can skip work.
        [[nodiscard]] Rect DeviceClipBounds() const noexcept;

        // Everything until the matching Restore is drawn into a layer and
        // composited with this opacity.
        void SaveLayerAlpha(double opacity);

        void FillPath(const Path& path, const Paint& paint);
        void StrokePath(const Path& path, const StrokeStyle& stroke, const Paint& paint);
        void DrawBitmap(const Bitmap& bitmap, const Rect& source, const Rect& destination, FilterQuality quality,
            double opacity, BlendMode blend = BlendMode::SrcOver);
        void DrawBoxShadow(const Path& shape, const BoxShadowSpec& shadow, const Rect& shapeBounds,
            const std::array<Point, 4>& radii);
        // One run of text with its baseline starting at the origin, in local
        // coordinates. Kerning applied; no shaping beyond that.
        void DrawText(std::u32string_view text, const Typeface& typeface, double size, Point origin,
            const Paint& paint);

    private:
        struct Mask final
        {
            // Device rectangle the mask covers; outside it coverage is zero.
            std::int32_t X = 0;
            std::int32_t Y = 0;
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            std::vector<float> Coverage;
        };

        struct State final
        {
            Matrix Transform{};
            // Integer device rectangle every draw is limited to.
            std::int32_t ClipLeft = 0;
            std::int32_t ClipTop = 0;
            std::int32_t ClipRight = 0;
            std::int32_t ClipBottom = 0;
            std::shared_ptr<const Mask> ClipMask{};
            // Set when this save opened a layer.
            std::shared_ptr<Bitmap> Layer{};
            // Layers use only the active device-space clip rectangle, while
            // draw coordinates remain relative to the base surface.
            std::int32_t LayerLeft = 0;
            std::int32_t LayerTop = 0;
            double LayerOpacity = 1.0;
        };

        [[nodiscard]] const State* ActiveLayer() const noexcept;
        [[nodiscard]] Bitmap& Target() noexcept;
        [[nodiscard]] State& Current() noexcept { return _states.back(); }
        [[nodiscard]] const State& Current() const noexcept { return _states.back(); }

        // Coverage of a set of device-space contours within the clip bounds.
        [[nodiscard]] Mask Rasterize(const std::vector<Path::Contour>& contours, FillRule rule, bool antialias) const;
        void Blend(const Mask& coverage, const Paint& paint);
        void BlendSpan(std::int32_t y, std::int32_t x0, std::int32_t x1, const float* coverage, const Paint& paint,
            float solid[4]);
        [[nodiscard]] float ClipAt(std::int32_t x, std::int32_t y) const noexcept;

        Bitmap& _base;
        GpuSurface* _gpu = nullptr;
        std::vector<State> _states;
    };

    // The outline of a stroke as closed polygons, all wound the same way, so
    // a non-zero fill of the result is the union.
    [[nodiscard]] std::vector<Path::Contour> StrokeContours(
        const std::vector<Path::Contour>& contours, const StrokeStyle& stroke, double scale);

    // SkiaSharpExtensions.ConvertRadiusToSigma, which is how Avalonia turns a
    // BoxShadow's blur radius into a Gaussian.
    [[nodiscard]] constexpr double ConvertRadiusToSigma(double radius) noexcept
    {
        return radius > 0 ? 0.288675 * radius + 0.5 : 0.0;
    }
}
