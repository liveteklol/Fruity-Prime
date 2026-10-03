#include "PreviewPass.hpp"

#include "../DebugLog.hpp"
#include "../../Scene.hpp"
#include "../../Shaders.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlShaderInterface.hpp"

#include "../EndScreen.hpp"
#include "HunterPreview.hpp"

#include <cmath>
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "../../NativeRuntime/System/Console.hpp"

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <memory>
#include "../../NativeRuntime/System/IO.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::ExceptionMessage;
using ::MphRead::NativeRuntime::RoundToEven;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;

namespace
{

}

namespace
{
    using ::OpenTK::Mathematics::Matrix4;
    using ::OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Vector4;

    constexpr float Pi = 3.14159265358979323846F;

    constexpr std::int32_t ScissorTest = 0x0C11;
    constexpr std::int32_t DepthTest = 0x0B71;
    constexpr std::int32_t StencilTest = 0x0B90;
    constexpr std::int32_t Blend = 0x0BE2;
    constexpr std::int32_t AlphaTest = 0x0BC0;
    constexpr std::uint32_t ColorBufferBit = 0x00004000U;
    constexpr std::uint32_t DepthBufferBit = 0x00000100U;
    constexpr std::int32_t Less = 0x0201;
    constexpr std::int32_t SrcAlpha = 0x0302;
    constexpr std::int32_t OneMinusSrcAlpha = 0x0303;
    constexpr std::int32_t FrontAndBack = 0x0408;
    constexpr std::int32_t Fill = 0x1B02;

    [[nodiscard]] std::int32_t RoundPixel(float value) noexcept
    {
        return ConvertToInt32Net9(RoundToEven(value));
    }

    class PreviewCollectFinally final
    {
    public:
        explicit PreviewCollectFinally(bool& collecting) noexcept
            : _collecting(collecting)
        {
        }

        PreviewCollectFinally(const PreviewCollectFinally&) = delete;
        PreviewCollectFinally& operator=(const PreviewCollectFinally&) = delete;

        ~PreviewCollectFinally()
        {
            _collecting = false;
        }

    private:
        bool& _collecting;
    };
}

namespace MphRead
{
    float Scene::_previewLeft = 0.0F;
    float Scene::_previewTop = 0.0F;
    float Scene::_previewRight = 0.0F;
    float Scene::_previewBottom = 0.0F;
    bool Scene::_previewWanted = false;

    const OpenTK::Mathematics::Vector3 Scene::_previewEye(0.0F, 1.05F, 3.15F);
    const OpenTK::Mathematics::Vector3 Scene::_previewTarget(0.0F, 0.95F, 0.0F);
    const OpenTK::Mathematics::Vector4 Scene::_previewBack(0.05F, 0.055F, 0.07F, 1.0F);

    float Scene::PreviewLeft() noexcept
    {
        return _previewLeft;
    }

    void Scene::PreviewLeft(float value) noexcept
    {
        _previewLeft = value;
    }

    float Scene::PreviewTop() noexcept
    {
        return _previewTop;
    }

    void Scene::PreviewTop(float value) noexcept
    {
        _previewTop = value;
    }

    float Scene::PreviewRight() noexcept
    {
        return _previewRight;
    }

    void Scene::PreviewRight(float value) noexcept
    {
        _previewRight = value;
    }

    float Scene::PreviewBottom() noexcept
    {
        return _previewBottom;
    }

    void Scene::PreviewBottom(float value) noexcept
    {
        _previewBottom = value;
    }

    bool Scene::PreviewWanted() noexcept
    {
        return _previewWanted;
    }

    void Scene::PreviewWanted(bool value) noexcept
    {
        _previewWanted = value;
    }

    bool Scene::PreviewAsked()
    {
        return Mods::EndScreen::Available() || LauncherPreview;
    }

    void Scene::ModStepPreview()
    {
        if (!PreviewAsked())
        {
            if (_preview)
            {
                _preview->Reset();
            }
            _previewWanted = false;
            _previewLeft = _previewRight = _previewTop = _previewBottom = 0.0F;
            return;
        }
        if (!_preview)
        {
            _preview = std::make_shared<Mods::Render::HunterPreviewEntity>(this);
        }
        const std::shared_ptr<Mods::Render::HunterPreviewEntity> preview = _preview;
        const MphRead::Hunter want = LauncherPreview ? LauncherHunter : Mods::EndScreen::Hunter();
        const std::int32_t suit = LauncherPreview ? LauncherSuit : Mods::EndScreen::Suit();
        preview->SetUp(want, suit);
        // Textures and GPU meshes are scene-owned. The launcher has no room
        // player to initialize this preview for it, so keep this idempotent
        // initialization on every step. A match scene can now tear down its
        // own GPU mesh cache without mutating the shared Model/Mesh objects
        // used by this preview scene.
        _previewInited = want;
        if (_preview->Ready())
        {
            InitEntity(_preview);
        }
        _preview->Step();
    }

    void Scene::ModCollectPreview()
    {
        _previewItems.clear();
        if (!PreviewAsked() || !_preview || !_preview->Ready())
        {
            return;
        }
        _collectingPreview = true;
        PreviewCollectFinally finally(_collectingPreview);
        try
        {
            _preview->GetDrawInfo();
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            NativeRuntime::ConsoleWriteLine(
                "[endscreen] preview draw failed: "
                + ExceptionMessage(exception));
            _previewItems.clear();
        }
    }

    bool Scene::ModPreviewDrawn() const noexcept
    {
        return !_previewItems.empty()
            && _previewRight - _previewLeft > 0.001F
            && _previewBottom - _previewTop > 0.001F;
    }

    // The same hunter, in a frame with no match behind it: the launcher's own
    // screens. Straight into the back buffer. Returns whether anything was
    // drawn, which the screens read before leaving a hole for it.
    bool Scene::ModDrawPreviewAlone(OpenTK::Mathematics::Vector2i windowSize)
    {
        if (!LauncherPreview || windowSize.X <= 0 || windowSize.Y <= 0)
        {
            return false;
        }
        // This stand-alone launcher ornament may yield to an occupied GPU
        // command slot. The match path and its simulation never enter here.
        // Screens keep their ordinary portrait/card when no preview draws.
        if (!Commands().TryPrepareOptionalWork()) return false;
        _targetSize = windowSize;
        try
        {
            ModStepPreview();
            ModCollectPreview();
            if (!ModPreviewDrawn())
            {
                return false;
            }
            BeginWindowRendering();
            _previewIntoWindow = true;
            ModDrawPreview();
            _previewIntoWindow = false;
            return true;
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            // A preview that will not draw is the launcher's boxes again, not
            // a dead launcher. Said once: this is a per-frame path.
            if (!_previewComplained)
            {
                _previewComplained = true;
                Mods::DebugLog::Line("ui", "the hunter preview could not be drawn: "
                    + ExceptionMessage(exception));
            }
            LauncherPreview = false;
            return false;
        }
    }

    void Scene::ModDrawPreview()
    {
        if (_previewItems.empty() || !_previewWanted)
        {
            _previewDrawnLastFrame = false;
            return;
        }
        // Not from inside the world's render while the deck panel is up: the
        // panel is opaque, so it would be a hunter behind a card.
        if (Mods::EndScreen::PanelUp() && !LauncherPreview)
        {
            return;
        }
        const OpenTK::Mathematics::Vector2i target = _targetSize;
        const std::int32_t x = RoundPixel(_previewLeft * static_cast<float>(target.X));
        const std::int32_t y = RoundPixel((1.0F - _previewBottom) * static_cast<float>(target.Y));
        const std::int32_t width = RoundPixel(
            (_previewRight - _previewLeft) * static_cast<float>(target.X));
        const std::int32_t height = RoundPixel(
            (_previewBottom - _previewTop) * static_cast<float>(target.Y));
        if (width < 4 || height < 4)
        {
            return;
        }

        // Its own rendering scope over the corner it draws in: colour and
        // depth cleared there, the stencil kept, and nothing outside touched.
        namespace Rhi = NativeRuntime::Rhi;
        const Rhi::ClearColor back{_previewBack.X, _previewBack.Y, _previewBack.Z, _previewBack.W};
        const Rhi::Scissor area{x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        Commands().EndRendering();
        if (_previewIntoWindow)
        {
            BeginWindowRendering(Rhi::LoadOp::Clear, Rhi::LoadOp::Clear, back, area);
        }
        else
        {
            BeginSceneRendering(Rhi::LoadOp::Clear, Rhi::LoadOp::Clear, Rhi::LoadOp::Load, back, area);
        }
        // The preview pipeline first: it binds the main program the uniforms
        // below are written into.
        BeginScenePass(ScenePass::Preview);
        Commands().SetViewport(Rhi::Viewport{static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height)});
        Matrix4 projection = Matrix4::CreatePerspectiveFieldOfView(
            DegreesToRadians(PreviewFov), width / static_cast<float>(height), 0.1F, 100.0F);
        Matrix4 view = Matrix4::LookAt(_previewEye, _previewTarget, Vector3(0.0F, 1.0F, 0.0F));
        SetFrameMatrices(view, projection);
        _shaderConstants->SetFogEnabled(false);
        for (std::size_t i = 0; i < _previewItems.size(); ++i)
        {
            RenderItem(_previewItems[i]);
        }
        Commands().EndRendering();
        if (_previewIntoWindow)
        {
            BeginWindowRendering();
        }
        else
        {
            BeginSceneRendering();
        }
        Commands().SetViewport(Rhi::Viewport{0.0F, 0.0F,
            static_cast<float>(target.X), static_cast<float>(target.Y)});
        SetFrameMatrices(_viewMatrix, _perspectiveMatrix);
        _shaderConstants->SetFogEnabled(_hasFog && FogOn());
        _previewDrawnLastFrame = true;
        _previewDrawnHunter = _preview != nullptr ? _preview->Shown() : MphRead::Hunter::Random;
        _previewDrawnSuit = _preview != nullptr ? _preview->ShownSuit() : -1;
    }
}
