#include "OpenGlLauncherNoise.hpp"
#include "OpenGlDevice.hpp"

#include "../../../Mods/Render/NoiseField.hpp"
#include "../../../Mods/DebugLog.hpp"
#include "../../OpenTK/GL.hpp"
#include "../../System/ExceptionText.hpp"
#include "../../System/Stopwatch.hpp"

#include <exception>
#include <string>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    using ::MphRead::Mods::DebugLog;
    using ::MphRead::Mods::Render::NoiseField;
    namespace GL = ::OpenTK::Graphics::OpenGL::GL;
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        struct LauncherNoiseState final
        {
            NoiseField Field;
            std::int32_t Texture = 0;
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            double DrawnAt = -1000.0;
            std::int64_t Clock = Runtime::StopwatchGetTimestamp();
        };

        LauncherNoiseState& State()
        {
            // C# initializes LauncherNoise's static fields on first access.
            // Keep both stopwatches' epochs at that point, and preserve their
            // declaration order: NoiseField first, upload clock second.
            static LauncherNoiseState state;
            return state;
        }
    }

    std::int32_t OpenGlLauncherNoise::Texture() noexcept
    {
        return State().Texture;
    }

    bool OpenGlLauncherNoise::Step(std::int32_t windowWidth, std::int32_t windowHeight)
    {
        LauncherNoiseState& state = State();
        if (!state.Field.Step(windowWidth, windowHeight))
        {
            return false;
        }
        const bool reshaped = state.Field.Width() != state.Width || state.Field.Height() != state.Height;
        state.Width = state.Field.Width();
        state.Height = state.Field.Height();
        const double now = Runtime::TimeSpanTotalMilliseconds(Runtime::StopwatchGetElapsedTicks(state.Clock));
        if (state.Texture != 0 && !reshaped && now - state.DrawnAt < NoiseField::Gap)
        {
            return true;
        }
        state.DrawnAt = now;
        return Upload();
    }

    bool OpenGlLauncherNoise::Upload()
    {
        LauncherNoiseState& state = State();
        try
        {
#if !defined(__ANDROID__)
            AdmitInteropTextureStorage(TextureFormat::RGB8Unorm, state.Width, state.Height);
#endif
            GL::ActiveTexture(GL::TextureUnit::Texture0);
            if (state.Texture == 0)
            {
                state.Texture = Name;
            }
            GL::BindTexture(GL::TextureTarget::Texture2D, state.Texture);
            // The unpack state belongs to the context. Other renderer paths
            // change it, so every field is set for this tightly packed RGB image.
            GL::PixelStore(GL::PixelStoreParameter::UnpackAlignment, 1);
            GL::PixelStore(GL::PixelStoreParameter::UnpackRowLength, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipPixels, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSkipRows, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackSwapBytes, 0);
            GL::PixelStore(GL::PixelStoreParameter::UnpackLsbFirst, 0);
            GL::TexImage2D(GL::TextureTarget::Texture2D, 0, GL::PixelInternalFormat::Rgb,
                state.Width, state.Height, 0, GL::PixelFormat::Rgb, GL::PixelType::UnsignedByte,
                state.Field.Pixels().data());
#if !defined(__ANDROID__)
            CheckInteropStorageResult("OpenGL launcher noise allocation");
#endif
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureMinFilter,
                static_cast<std::int32_t>(GL::TextureMinFilter::Nearest));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureMagFilter,
                static_cast<std::int32_t>(GL::TextureMagFilter::Nearest));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureWrapS,
                static_cast<std::int32_t>(GL::TextureWrapMode::ClampToEdge));
            GL::TexParameter(GL::TextureTarget::Texture2D, GL::TextureParameterName::TextureWrapT,
                static_cast<std::int32_t>(GL::TextureWrapMode::ClampToEdge));
            GL::BindTexture(GL::TextureTarget::Texture2D, 0);
            return true;
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            DebugLog::Line("ui", "the moving backdrop could not be uploaded: "
                + Runtime::ExceptionMessage(exception));
            state.Texture = 0;
            return false;
        }
    }

    void OpenGlLauncherNoise::Release()
    {
        LauncherNoiseState& state = State();
        if (state.Texture != 0)
        {
            GL::DeleteTexture(state.Texture);
            state.Texture = 0;
        }
        state.Width = 0;
        state.Height = 0;
        state.DrawnAt = -1000.0;
    }
}
