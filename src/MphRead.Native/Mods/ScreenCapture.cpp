#include "ScreenCapture.hpp"
#include "../NativeRuntime/Rhi/OpenGL/OpenGlDiagnostics.hpp"
#include "../NativeRuntime/Rhi/SceneBackend.hpp"

#include "../NativeRuntime/OpenTK/GLFW.hpp"
#include "../NativeRuntime/System/Console.hpp"
#include "../NativeRuntime/System/Globalization.hpp"

#include <typeinfo>

#include "ThumbnailLog.hpp"
#include "../Formats/Types.hpp"
#include "../Scene.hpp"
#include "../NativeRuntime/System/ExceptionText.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ExceptionTypeName;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;

namespace MphRead::Export::ImagesInterop
{
    void SetFlipVerticallyOnSave(bool value);
    void WritePngRgb(
        std::span<const std::uint8_t> buffer,
        std::int32_t width,
        std::int32_t height,
        std::ostream& stream);
}

namespace
{
    constexpr std::int32_t GlDebugOutput = 0x92E0;
    constexpr std::int32_t GlDebugOutputSynchronous = 0x8242;
    constexpr std::int32_t GlDebugSeverityNotification = 0x826B;
    constexpr std::int32_t GlVendor = 0x1F00;
    constexpr std::int32_t GlRenderer = 0x1F01;
    constexpr std::int32_t GlVersion = 0x1F02;
    constexpr std::int32_t GlContextFlags = 0x821E;
    constexpr std::int32_t GlContextProfileMask = 0x9126;
    constexpr std::int32_t GlContextFlagForwardCompatibleBit = 0x00000001;
    constexpr std::int32_t GlContextCoreProfileBit = 0x00000001;
    constexpr std::int32_t GlContextCompatibilityProfileBit = 0x00000002;

    [[nodiscard]] std::ofstream CreateFile(const std::string& path)
    {
        std::ofstream stream(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        return stream;
    }

    [[nodiscard]] std::string DebugSourceName(std::int32_t value)
    {
        switch (value)
        {
        case 0x8246: return "DebugSourceApi";
        case 0x8247: return "DebugSourceWindowSystem";
        case 0x8248: return "DebugSourceShaderCompiler";
        case 0x8249: return "DebugSourceThirdParty";
        case 0x824A: return "DebugSourceApplication";
        case 0x824B: return "DebugSourceOther";
        default: return std::to_string(value);
        }
    }

    [[nodiscard]] std::string DebugTypeName(std::int32_t value)
    {
        switch (value)
        {
        case 0x824C: return "DebugTypeError";
        case 0x824D: return "DebugTypeDeprecatedBehavior";
        case 0x824E: return "DebugTypeUndefinedBehavior";
        case 0x824F: return "DebugTypePortability";
        case 0x8250: return "DebugTypePerformance";
        case 0x8251: return "DebugTypeOther";
        case 0x8268: return "DebugTypeMarker";
        case 0x8269: return "DebugTypePushGroup";
        case 0x826A: return "DebugTypePopGroup";
        default: return std::to_string(value);
        }
    }

    [[nodiscard]] std::string DebugSeverityName(std::int32_t value)
    {
        switch (value)
        {
        case 0x9146: return "DebugSeverityHigh";
        case 0x9147: return "DebugSeverityMedium";
        case 0x9148: return "DebugSeverityLow";
        case 0x826B: return "DebugSeverityNotification";
        default: return std::to_string(value);
        }
    }

    void InvokeReport(
        const std::function<void(const std::string&)>& report,
        const std::string& line)
    {
        if (!report)
        {
            throw System::NullReferenceException();
        }
        report(line);
    }
}

namespace
{
    namespace GlDiag = ::MphRead::NativeRuntime::Rhi::OpenGL;
    using GlDebugProc = GlDiag::DebugProc;

    void GlEnable(std::int32_t capability) { GlDiag::EnableCapability(capability); }

    void GlDebugMessageCallback(GlDebugProc callback, const void* userParam)
    {
        GlDiag::DebugMessageCallback(callback, userParam);
    }

    [[nodiscard]] std::optional<std::string> GlGetString(std::int32_t name)
    {
        std::string value = GlDiag::ContextString(name);
        if (value.empty())
        {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::int32_t GlGetInteger(std::int32_t pname) { return GlDiag::ContextInteger(pname); }

    [[nodiscard]] std::pair<std::int32_t, std::int32_t> ContextVersion()
    {
        const std::optional<std::string> version = GlGetString(GlVersion);
        if (!version)
        {
            return {0, 0};
        }

        const std::string_view text(*version);
        const std::string_view first = text.substr(0, text.find(' '));
        std::array<std::int32_t, 4> components{};
        std::size_t count = 0;
        std::size_t start = 0;
        while (start <= first.size())
        {
            if (count == components.size())
            {
                return {0, 0};
            }
            const std::size_t end = first.find('.', start);
            const std::string_view component = first.substr(
                start, end == std::string_view::npos ? end : end - start);
            if (component.empty())
            {
                return {0, 0};
            }
            const auto [parsedEnd, error] = std::from_chars(
                component.data(), component.data() + component.size(), components[count]);
            if (error != std::errc{}
                || parsedEnd != component.data() + component.size()
                || components[count] < 0)
            {
                return {0, 0};
            }
            ++count;
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
        }
        if (count < 2)
        {
            return {0, 0};
        }
        return {components[0], components[1]};
    }

    [[nodiscard]] bool ContextAtLeast(std::int32_t major, std::int32_t minor)
    {
        const auto [contextMajor, contextMinor] = ContextVersion();
        return contextMajor > major
            || (contextMajor == major && contextMinor >= minor);
    }
}

namespace MphRead::Mods
{
    ScreenCapture::PngWriterAction ScreenCapture::_pngWriter{};
    ScreenCapture::DebugProc ScreenCapture::_debugCallback{};
    std::int32_t ScreenCapture::_messagesLogged = 0;

    ScreenCapture::PngWriterAction ScreenCapture::PngWriter()
    {
        return _pngWriter;
    }

    void ScreenCapture::PngWriter(PngWriterAction value)
    {
        _pngWriter = std::move(value);
    }

    bool ScreenCapture::SaveWindow(Scene* scene, const std::string& path)
    {
        if (scene == nullptr)
        {
            throw System::NullReferenceException();
        }
        ReadPixels read = [scene](std::int32_t& width, std::int32_t& height)
        {
            return scene->ReadWindowBuffer(width, height);
        };
        return Save(scene, path, read);
    }

    bool ScreenCapture::SaveWindow(
        std::int32_t width, std::int32_t height, const std::string& path)
    {
        ReadPixels read = [width, height](std::int32_t& outWidth, std::int32_t& outHeight)
            -> PixelBuffer
        {
            outWidth = width;
            outHeight = height;
            if (width <= 0 || height <= 0)
            {
                return std::nullopt;
            }
            const std::int32_t byteCount = UncheckedMultiply(
                UncheckedMultiply(width, height), 3);
            std::vector<std::uint8_t> buffer(static_cast<std::size_t>(byteCount));
            // The window as the device that draws it holds it: OpenGL's back
            // buffer, or the Vulkan window target the swapchain is fed from.
            namespace Rhi = ::MphRead::NativeRuntime::Rhi;
            auto commands = Rhi::SceneDevice().CreateCommandList();
            Rhi::RenderingInfo info{};
            info.width = static_cast<std::uint32_t>(width);
            info.height = static_cast<std::uint32_t>(height);
            info.swapchain = true;
            commands->ReadColor(info, 0, 0, info.width, info.height, Rhi::TextureFormat::RGB8Unorm, buffer.data());
            return buffer;
        };
        return Save(nullptr, path, read);
    }

    bool ScreenCapture::Save(Scene* scene, const std::string& path)
    {
        if (scene == nullptr)
        {
            throw System::NullReferenceException();
        }
        ReadPixels read = [scene](std::int32_t& width, std::int32_t& height)
        {
            return scene->ReadSceneTarget(width, height);
        };
        return Save(scene, path, read);
    }

    bool ScreenCapture::Save(
        Scene* scene,
        const std::string& path,
        const ReadPixels& read)
    {
        (void)scene;
        try
        {
            std::int32_t width = 0;
            std::int32_t height = 0;
            PixelBuffer pixels = read(width, height);
            if (!pixels || width <= 0 || height <= 0)
            {
                return false;
            }

            if (LitFraction(*pixels) < MinLitFraction)
            {
                std::string first = PathToUtf8(PathFromUtf8(path).filename());
                first += " came out black ";

                std::string second = "(";
                second += ::MphRead::NativeRuntime::ToString(LitFraction(*pixels) * 100.0, "0.00");
                second += "% lit, ";
                second += std::to_string(width);
                second += "x";
                second += std::to_string(height);
                second += "); not saving it. ";

                std::string third = "The scene rendered nothing -- ";
                third += DescribeContext();

                const std::string why = first + second + third;
                NativeRuntime::ConsoleWriteLine("[capture] " + why);
                ThumbnailLog::Write(why);
                return false;
            }

            const std::filesystem::path pathValue = PathFromUtf8(path);
            const std::filesystem::path directory
                = pathValue == pathValue.root_path()
                    ? std::filesystem::path{}
                    : pathValue.parent_path();
            if (!directory.empty())
            {
                std::filesystem::create_directories(directory);
            }

            PngWriterAction writer = PngWriter();
            if (writer)
            {
                writer(*pixels, width, height, path);
                return true;
            }

            std::ofstream stream = CreateFile(path);
            try
            {
                Export::ImagesInterop::SetFlipVerticallyOnSave(true);
                Export::ImagesInterop::WritePngRgb(
                    std::span<const std::uint8_t>(*pixels), width, height, stream);
            }
            catch (...)
            {
                try
                {
                    stream.close();
                }
                catch (...)
                {
                    throw;
                }
                throw;
            }
            stream.close();
            return true;
        }
        catch (const std::exception& exception)
        {
            NativeRuntime::ConsoleWriteLine(
                "[capture] could not save " + path + ": "
                    + std::string(exception.what()));
            return false;
        }
    }

    double ScreenCapture::LitFraction(const std::vector<std::uint8_t>& pixels)
    {
        const std::int32_t length = static_cast<std::int32_t>(pixels.size());
        const auto channel = [&](std::int32_t index) -> std::uint8_t
        {
            if (index < 0 || index >= length)
            {
                throw SceneDetail::IndexOutOfRangeException();
            }
            return pixels[static_cast<std::size_t>(index)];
        };

        std::int32_t lit = 0;
        std::int32_t total = 0;
        for (std::int32_t i = 0; UncheckedAdd(i, 2) < length; i = UncheckedAdd(i, 3))
        {
            total = UncheckedAdd(total, 1);
            if (channel(i) > 8
                || channel(UncheckedAdd(i, 1)) > 8
                || channel(UncheckedAdd(i, 2)) > 8)
            {
                lit = UncheckedAdd(lit, 1);
            }
        }
        return total == 0 ? 0.0 : static_cast<double>(lit) / static_cast<double>(total);
    }

    void ScreenCapture::DebugThunk(
        std::int32_t source,
        std::int32_t type,
        std::int32_t id,
        std::int32_t severity,
        std::int32_t length,
        const char* message,
        const void* param)
    {
        if (_debugCallback)
        {
            try
            {
                _debugCallback(source, type, id, severity, length, message, param);
            }
            catch (...)
            {
                // A managed diagnostic callback never throws through the GL driver.
            }
        }
    }

    void ScreenCapture::EnableDebugOutput(ReportAction report)
    {
        try
        {
#if defined(__ANDROID__)
            InvokeReport(
                report,
                "GL debug output unavailable; continuing without optional diagnostics.");
            return;
#else
            if ((!ContextAtLeast(4, 3)
                    && !::OpenTK::Windowing::GraphicsLibraryFramework::GLFW::ExtensionSupported(
                        "GL_KHR_debug"))
                || ::OpenTK::Windowing::GraphicsLibraryFramework::GLFW::GetProcAddress(
                    "glDebugMessageCallback") == nullptr)
            {
                InvokeReport(
                    report,
                    "GL debug output unavailable; continuing without optional diagnostics.");
                return;
            }
#endif
            _messagesLogged = 0;
            _debugCallback = [report](
                std::int32_t source,
                std::int32_t type,
                std::int32_t id,
                std::int32_t severity,
                std::int32_t length,
                const char* message,
                const void* param)
            {
                (void)id;
                (void)param;
                if (severity == GlDebugSeverityNotification || _messagesLogged >= 12)
                {
                    return;
                }
                _messagesLogged = UncheckedAdd(_messagesLogged, 1);
                try
                {
                    if (length < 0)
                    {
                        throw std::out_of_range("length");
                    }
                    const std::string text = message == nullptr
                        ? std::string{}
                        : std::string(message, static_cast<std::size_t>(length));
                    const std::string severityText = DebugSeverityName(severity);
                    const std::string typeText = DebugTypeName(type);
                    const std::string sourceText = DebugSourceName(source);
                    InvokeReport(
                        report,
                        "GL says: [" + severityText + "] " + typeText
                            + " from " + sourceText + ": " + text);
                }
                catch (...)
                {
                }
            };
            GlEnable(GlDebugOutput);
            GlEnable(GlDebugOutputSynchronous);
            GlDebugMessageCallback(&ScreenCapture::DebugThunk, nullptr);
        }
        catch (const std::exception& exception)
        {
            InvokeReport(
                report,
                "could not turn on GL debug output ("
                    + ExceptionTypeName(exception)
                    + "); this driver may not have KHR_debug");
        }
    }

    std::string ScreenCapture::DescribeContext()
    {
        try
        {
            const std::string vendor = GlGetString(GlVendor).value_or("?");
            const std::string renderer = GlGetString(GlRenderer).value_or("?");
            const std::string version = GlGetString(GlVersion).value_or("?");
            const auto [major, minor] = ContextVersion();
            const auto atLeast = [major, minor](std::int32_t requiredMajor,
                                                std::int32_t requiredMinor)
            {
                return major > requiredMajor
                    || (major == requiredMajor && minor >= requiredMinor);
            };
            const std::int32_t flags = atLeast(3, 0)
                ? GlGetInteger(GlContextFlags) : 0;
            const std::string forward = (flags & GlContextFlagForwardCompatibleBit) != 0
                ? ", FORWARD-COMPATIBLE (deprecated entry points removed, which is all of immediate mode)"
                : "";
            const std::int32_t mask = atLeast(3, 2)
                ? GlGetInteger(GlContextProfileMask) : 0;
            const std::string profile = !atLeast(3, 2)
                ? "legacy"
                : (mask & GlContextCoreProfileBit) != 0
                    ? "CORE (immediate mode is unavailable, which renders everything black)"
                    : (mask & GlContextCompatibilityProfileBit) != 0
                        ? "compatibility"
                        : "unreported";
            return "GL " + version + ", profile " + profile + forward
                + ", " + vendor + " / " + renderer;
        }
        catch (const std::exception& exception)
        {
            return "could not query the GL context: "
                + std::string(exception.what());
        }
    }

    double ScreenCapture::NonBlackFraction(Scene* scene)
    {
        if (scene == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::int32_t width = 0;
        std::int32_t height = 0;
        PixelBuffer pixels = scene->ReadSceneTarget(width, height);
        if (!pixels || width <= 0)
        {
            return 0.0;
        }
        const std::int32_t length = static_cast<std::int32_t>(pixels->size());
        const auto channel = [&](std::int32_t index) -> std::uint8_t
        {
            if (index < 0 || index >= length)
            {
                throw SceneDetail::IndexOutOfRangeException();
            }
            return (*pixels)[static_cast<std::size_t>(index)];
        };

        std::int32_t lit = 0;
        for (std::int32_t i = 0; i < length; i = UncheckedAdd(i, 3))
        {
            if (channel(i) > 8
                || channel(UncheckedAdd(i, 1)) > 8
                || channel(UncheckedAdd(i, 2)) > 8)
            {
                lit = UncheckedAdd(lit, 1);
            }
        }
        const std::int32_t area = UncheckedMultiply(width, height);
        return static_cast<double>(lit) / static_cast<double>(area);
    }
}
