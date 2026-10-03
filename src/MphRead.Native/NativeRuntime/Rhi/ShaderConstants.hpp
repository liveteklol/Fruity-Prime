#pragma once

#include "../OpenTK/Mathematics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// What the renderer hands a shader, grouped by how often it changes, with no
// uniform location, program id or binding number anywhere in it. A backend
// turns these into whatever it has: the OpenGL one into glUniform calls on
// the locations it looked up itself (OpenGlShaderConstants), a Vulkan one into
// uniform-buffer writes through its generated std140 packing adapter.
//
// Each structure is exactly the set of values one frontend call site sets
// together, so replacing that call site's uniform calls with one Set() changes
// no GPU state the old calls did not change.
namespace MphRead::NativeRuntime::Rhi
{
    // Per camera: set when the view changes, and swapped for the HUD's
    // orthographic pair while the HUD draws.
    struct FrameConstants final
    {
        ::OpenTK::Mathematics::Matrix4 View{};
        ::OpenTK::Mathematics::Matrix4 Projection{};
    };

    // Per room (or per preview): the DS's two directional lights.
    struct LightConstants final
    {
        ::OpenTK::Mathematics::Vector3 Vector{};
        ::OpenTK::Mathematics::Vector3 Color{};
    };

    inline constexpr std::size_t SceneLightCount = 2;

    struct SceneLightConstants final
    {
        LightConstants Lights[SceneLightCount]{};
    };

    // Per room: the fog the room metadata describes, as fractions of the depth range.
    struct SceneFogConstants final
    {
        ::OpenTK::Mathematics::Vector4 Color{};
        float MinDistance = 0.0F;
        float MaxDistance = 0.0F;
    };

    // Per material: DoMaterial's set.
    struct MaterialConstants final
    {
        bool UseLight = false;
        ::OpenTK::Mathematics::Vector3 Diffuse{};
        ::OpenTK::Mathematics::Vector3 Ambient{};
        ::OpenTK::Mathematics::Vector3 Specular{};
        ::OpenTK::Mathematics::Vector3 Emission{};
        float Alpha = 1.0F;
        std::int32_t PolygonMode = 0;
    };

    inline constexpr std::size_t MatrixStackCapacity = 32;

    // Per draw: the node matrix stack the vertex shader indexes with
    // TexCoord.z. Sixteen floats a matrix in Matrix4's own order (M11, M12,
    // ...), which the shaders read untransposed; at most MatrixStackCapacity
    // of them. The span does not own the data.
    struct DrawConstants final
    {
        std::span<const float> MatrixStack{};
    };

    // Post: the cel-shading outline pass.
    struct CelPostConstants final
    {
        float TexelWidth = 0.0F;
        float TexelHeight = 0.0F;
        float Outline = 0.0F;
        float NearPlane = 0.0F;
        float FarPlane = 0.0F;
        float DepthQuantum = 0.0F;
        bool Probe = false;
    };

    // Post: the HUD/RTT pass. The frontend changes these one at a time (a
    // flash, a layer, a mask), so the sink takes them one at a time; a Vulkan
    // backend keeps this struct and writes it as one block.
    struct HudPostConstants final
    {
        ::OpenTK::Mathematics::Vector4 FadeColor{};
        float LayerAlpha = 1.0F;
        bool UseMask = false;
        float ViewWidth = 0.0F;
        float ViewHeight = 0.0F;
    };

    // Post: the disruption / whiteout scanline shift, set together each frame
    // the effect is live.
    struct DisruptionPostConstants final
    {
        float ShiftFactor = 0.0F;
        std::int32_t ShiftIndex = 0;
        float LerpFactor = 0.0F;
        float WhiteoutFactor = 0.0F;
    };

    inline constexpr std::size_t ShiftTableLength = 64;
    inline constexpr std::size_t WhiteoutTableLength = 192;

    class ShaderConstantSink
    {
    public:
        virtual ~ShaderConstantSink() = default;

        virtual void Set(const FrameConstants& constants) = 0;
        virtual void Set(const SceneLightConstants& constants) = 0;
        // One light on its own: an entity lit by its own light rather than the room's.
        virtual void SetLight(std::size_t index, const LightConstants& constants) = 0;
        virtual void Set(const SceneFogConstants& constants) = 0;
        virtual void Set(const MaterialConstants& constants) = 0;
        virtual void Set(const DrawConstants& constants) = 0;
        virtual void Set(const CelPostConstants& constants) = 0;

        // HudPostConstants, field by field.
        virtual void SetFadeColor(const ::OpenTK::Mathematics::Vector4& color) = 0;
        virtual void SetLayerAlpha(float alpha) = 0;
        virtual void SetUseMask(bool useMask) = 0;
        virtual void SetViewSize(float width, float height) = 0;

        virtual void Set(const DisruptionPostConstants& constants) = 0;
        // ShiftTableLength and WhiteoutTableLength values respectively.
        virtual void SetShiftTable(std::span<const float> table) = 0;
        virtual void SetWhiteoutTable(std::span<const float> table) = 0;

        // FrameConstants one matrix at a time: the projection alone changes
        // with the field of view, the view alone with the camera.
        virtual void SetView(const ::OpenTK::Mathematics::Matrix4& view) = 0;
        virtual void SetProjection(const ::OpenTK::Mathematics::Matrix4& projection) = 0;

        // SceneConstants' switches, which the HUD and the preview turn off
        // and back on around themselves.
        virtual void SetFogEnabled(bool enabled) = 0;
        virtual void SetCelBands(std::int32_t bands) = 0;
        virtual void SetShowColors(bool show) = 0;

        // DrawConstants' other half: the billboard rotation an item is drawn
        // under (identity for everything that is not a billboard).
        virtual void SetBillboard(const ::OpenTK::Mathematics::Matrix4& viewInverse) = 0;

        // MaterialConstants without the alpha (the polygon mode included),
        // which the HUD model setup resets while leaving each model's alpha
        // to be set per draw.
        virtual void SetSurface(const MaterialConstants& constants) = 0;
        virtual void SetMaterialAlpha(float alpha) = 0;

        // The texture half of a material.
        virtual void SetUseTexture(bool enabled) = 0;
        virtual void SetTexgen(std::int32_t mode, const ::OpenTK::Mathematics::Matrix4& textureMatrix) = 0;
        // Null turns the override off and leaves its colour as it was.
        virtual void SetOverride(const ::OpenTK::Mathematics::Vector4* color) = 0;
        virtual void SetOverrideColor(const ::OpenTK::Mathematics::Vector4& color) = 0;
        virtual void SetPaletteOverride(const ::OpenTK::Mathematics::Vector4* color) = 0;
        // Cel shading's one-colour stand-in for the bound texture; null is off.
        virtual void SetFlatColor(const ::OpenTK::Mathematics::Vector3* color) = 0;

        // The colour and texcoord a vertex takes when its mesh carries none:
        // the DS's current vertex colour, inherited from whatever set it last.
        virtual void SetInheritedColor(const ::OpenTK::Mathematics::Vector4& color) = 0;
        virtual void SetInheritedTexCoord(const ::OpenTK::Mathematics::Vector3& texCoord) = 0;
    };

    // Takes every constant and does nothing with it: what a scene's constants
    // go to before its shaders exist (a scene loads textures and entities
    // before it is given a context to draw with).
    class NullShaderConstantSink final : public ShaderConstantSink
    {
    public:
        void Set(const FrameConstants&) override {}
        void Set(const SceneLightConstants&) override {}
        void SetLight(std::size_t, const LightConstants&) override {}
        void Set(const SceneFogConstants&) override {}
        void Set(const MaterialConstants&) override {}
        void Set(const DrawConstants&) override {}
        void Set(const CelPostConstants&) override {}
        void SetFadeColor(const ::OpenTK::Mathematics::Vector4&) override {}
        void SetLayerAlpha(float) override {}
        void SetUseMask(bool) override {}
        void SetViewSize(float, float) override {}
        void Set(const DisruptionPostConstants&) override {}
        void SetShiftTable(std::span<const float>) override {}
        void SetWhiteoutTable(std::span<const float>) override {}
        void SetView(const ::OpenTK::Mathematics::Matrix4&) override {}
        void SetProjection(const ::OpenTK::Mathematics::Matrix4&) override {}
        void SetFogEnabled(bool) override {}
        void SetCelBands(std::int32_t) override {}
        void SetShowColors(bool) override {}
        void SetBillboard(const ::OpenTK::Mathematics::Matrix4&) override {}
        void SetSurface(const MaterialConstants&) override {}
        void SetMaterialAlpha(float) override {}
        void SetUseTexture(bool) override {}
        void SetTexgen(std::int32_t, const ::OpenTK::Mathematics::Matrix4&) override {}
        void SetOverride(const ::OpenTK::Mathematics::Vector4*) override {}
        void SetOverrideColor(const ::OpenTK::Mathematics::Vector4&) override {}
        void SetPaletteOverride(const ::OpenTK::Mathematics::Vector4*) override {}
        void SetFlatColor(const ::OpenTK::Mathematics::Vector3*) override {}
        void SetInheritedColor(const ::OpenTK::Mathematics::Vector4&) override {}
        void SetInheritedTexCoord(const ::OpenTK::Mathematics::Vector3&) override {}
    };
}
