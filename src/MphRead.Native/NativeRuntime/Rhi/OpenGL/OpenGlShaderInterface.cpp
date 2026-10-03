#include "OpenGlShaderInterface.hpp"
#include "../SceneShaderAbi.hpp"

#include "OpenGlDevice.hpp"
#include "../../OpenTK/GL.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace
    {
        namespace GL = ::OpenTK::Graphics::OpenGL::GL;
        using ::OpenTK::Mathematics::Matrix4;
        using ::OpenTK::Mathematics::Vector3;
        using ::OpenTK::Mathematics::Vector4;

        // The four programs' uniform locations, looked up once after linking.
        // The frontend never sees one: this is the OpenGL backend's cache.
        class ShaderLocations final
        {
        public:
            std::int32_t UseLight = 0;
            std::int32_t ShowColors = 0;
            std::int32_t UseTexture = 0;
            std::int32_t Light1Color = 0;
            std::int32_t Light1Vector = 0;
            std::int32_t Light2Color = 0;
            std::int32_t Light2Vector = 0;
            std::int32_t Diffuse = 0;
            std::int32_t Ambient = 0;
            std::int32_t Specular = 0;
            std::int32_t Emission = 0;
            std::int32_t UseFog = 0;
            std::int32_t CelBands = 0;
            std::int32_t UseFlat = 0;
            std::int32_t FlatColor = 0;
            std::int32_t CelOutline = 0;
            std::int32_t CelTexelWidth = 0;
            std::int32_t CelTexelHeight = 0;
            std::int32_t CelNearPlane = 0;
            std::int32_t CelFarPlane = 0;
            std::int32_t CelDepthQuantum = 0;
            std::int32_t CelProbe = 0;
            std::int32_t FogColor = 0;
            std::int32_t FogMinDistance = 0;
            std::int32_t FogMaxDistance = 0;
            std::int32_t UseOverride = 0;
            std::int32_t OverrideColor = 0;
            std::int32_t UsePaletteOverride = 0;
            std::int32_t PaletteOverrideColor = 0;
            std::int32_t MaterialAlpha = 0;
            std::int32_t MaterialMode = 0;
            std::int32_t ViewMatrix = 0;
            std::int32_t ViewInvMatrix = 0;
            std::int32_t ProjectionMatrix = 0;
            std::int32_t TextureMatrix = 0;
            std::int32_t TexgenMode = 0;
            std::int32_t MatrixStack = 0;
            std::int32_t ToonTable = 0;
            std::int32_t FadeColor = 0;
            std::int32_t LayerAlpha = 0;
            std::int32_t UseMask = 0;
            std::int32_t ViewWidth = 0;
            std::int32_t ViewHeight = 0;
            std::int32_t ShiftTable = 0;
            std::int32_t ShiftIndex = 0;
            std::int32_t ShiftFactor = 0;
            std::int32_t LerpFactor = 0;
            std::int32_t WhiteoutTable = 0;
            std::int32_t WhiteoutFactor = 0;
        };


        class OpenGlShaderConstants final : public ShaderConstantSink
        {
        public:
            explicit OpenGlShaderConstants(const ShaderLocations& locations) : _l(locations) {}

            void Set(const FrameConstants& constants) override
            {
                GL::UniformMatrix4(_l.ViewMatrix, false, constants.View);
                GL::UniformMatrix4(_l.ProjectionMatrix, false, constants.Projection);
            }

            void Set(const SceneLightConstants& constants) override
            {
                for (std::size_t i = 0; i < SceneLightCount; ++i)
                {
                    SetLight(i, constants.Lights[i]);
                }
            }

            void SetLight(std::size_t index, const LightConstants& constants) override
            {
                if (index == 0)
                {
                    GL::Uniform3(_l.Light1Vector, constants.Vector);
                    GL::Uniform3(_l.Light1Color, constants.Color);
                }
                else if (index == 1)
                {
                    GL::Uniform3(_l.Light2Vector, constants.Vector);
                    GL::Uniform3(_l.Light2Color, constants.Color);
                }
            }

            void Set(const SceneFogConstants& constants) override
            {
                GL::Uniform4(_l.FogColor, constants.Color);
                GL::Uniform1(_l.FogMinDistance, constants.MinDistance);
                GL::Uniform1(_l.FogMaxDistance, constants.MaxDistance);
            }

            void Set(const MaterialConstants& constants) override
            {
                SetSurface(constants);
                GL::Uniform1(_l.MaterialAlpha, constants.Alpha);
            }

            void Set(const DrawConstants& constants) override
            {
                const std::size_t count = std::min(constants.MatrixStack.size() / 16U, MatrixStackCapacity);
                if (count == 0)
                {
                    return;
                }
                GL::UniformMatrix4(_l.MatrixStack, static_cast<std::int32_t>(count), false,
                    constants.MatrixStack.data());
            }

            void Set(const CelPostConstants& constants) override
            {
                GL::Uniform1(_l.CelTexelWidth, constants.TexelWidth);
                GL::Uniform1(_l.CelTexelHeight, constants.TexelHeight);
                GL::Uniform1(_l.CelOutline, constants.Outline);
                GL::Uniform1(_l.CelNearPlane, constants.NearPlane);
                GL::Uniform1(_l.CelFarPlane, constants.FarPlane);
                GL::Uniform1(_l.CelDepthQuantum, constants.DepthQuantum);
                GL::Uniform1(_l.CelProbe, constants.Probe ? 1 : 0);
            }

            void SetFadeColor(const Vector4& color) override { GL::Uniform4(_l.FadeColor, color); }
            void SetLayerAlpha(float alpha) override { GL::Uniform1(_l.LayerAlpha, alpha); }
            void SetUseMask(bool useMask) override { GL::Uniform1(_l.UseMask, useMask ? 1 : 0); }

            void SetViewSize(float width, float height) override
            {
                GL::Uniform1(_l.ViewWidth, width);
                GL::Uniform1(_l.ViewHeight, height);
            }

            void Set(const DisruptionPostConstants& constants) override
            {
                GL::Uniform1(_l.ShiftFactor, constants.ShiftFactor);
                GL::Uniform1(_l.ShiftIndex, constants.ShiftIndex);
                GL::Uniform1(_l.LerpFactor, constants.LerpFactor);
                GL::Uniform1(_l.WhiteoutFactor, constants.WhiteoutFactor);
            }

            void SetShiftTable(std::span<const float> table) override
            {
                GL::Uniform1(_l.ShiftTable, static_cast<std::int32_t>(std::min(table.size(), ShiftTableLength)),
                    table.data());
            }

            void SetWhiteoutTable(std::span<const float> table) override
            {
                GL::Uniform1(_l.WhiteoutTable,
                    static_cast<std::int32_t>(std::min(table.size(), WhiteoutTableLength)), table.data());
            }

            void SetView(const Matrix4& view) override { GL::UniformMatrix4(_l.ViewMatrix, false, view); }
            void SetProjection(const Matrix4& projection) override
            {
                GL::UniformMatrix4(_l.ProjectionMatrix, false, projection);
            }

            void SetFogEnabled(bool enabled) override { GL::Uniform1(_l.UseFog, enabled ? 1 : 0); }
            void SetCelBands(std::int32_t bands) override { GL::Uniform1(_l.CelBands, bands); }
            void SetShowColors(bool show) override { GL::Uniform1(_l.ShowColors, show ? 1 : 0); }

            void SetBillboard(const Matrix4& viewInverse) override
            {
                GL::UniformMatrix4(_l.ViewInvMatrix, false, viewInverse);
            }

            // The material without its alpha: DoMaterial's order was use_light,
            // the vertex colour, then the four colours; the vertex colour is the
            // caller's (SetInheritedColor).
            void SetSurface(const MaterialConstants& constants) override
            {
                GL::Uniform1(_l.UseLight, constants.UseLight ? 1 : 0);
                GL::Uniform3(_l.Diffuse, constants.Diffuse);
                GL::Uniform3(_l.Ambient, constants.Ambient);
                GL::Uniform3(_l.Specular, constants.Specular);
                GL::Uniform3(_l.Emission, constants.Emission);
                GL::Uniform1(_l.MaterialMode, constants.PolygonMode);
            }

            void SetMaterialAlpha(float alpha) override { GL::Uniform1(_l.MaterialAlpha, alpha); }
            void SetUseTexture(bool enabled) override { GL::Uniform1(_l.UseTexture, enabled ? 1 : 0); }

            void SetTexgen(std::int32_t mode, const Matrix4& textureMatrix) override
            {
                GL::Uniform1(_l.TexgenMode, mode);
                GL::UniformMatrix4(_l.TextureMatrix, false, textureMatrix);
            }

            void SetOverride(const Vector4* color) override
            {
                GL::Uniform1(_l.UseOverride, color != nullptr ? 1 : 0);
                if (color != nullptr)
                {
                    GL::Uniform4(_l.OverrideColor, *color);
                }
            }

            void SetOverrideColor(const Vector4& color) override { GL::Uniform4(_l.OverrideColor, color); }

            void SetPaletteOverride(const Vector4* color) override
            {
                GL::Uniform1(_l.UsePaletteOverride, color != nullptr ? 1 : 0);
                if (color != nullptr)
                {
                    GL::Uniform4(_l.PaletteOverrideColor, *color);
                }
            }

            void SetFlatColor(const Vector3* color) override
            {
                GL::Uniform1(_l.UseFlat, color != nullptr ? 1 : 0);
                if (color != nullptr)
                {
                    GL::Uniform3(_l.FlatColor, *color);
                }
            }

            void SetInheritedColor(const Vector4& color) override
            {
                GL::Color4(color.X, color.Y, color.Z, color.W);
            }

            void SetInheritedTexCoord(const Vector3& texCoord) override
            {
                GL::TexCoord3(texCoord.X, texCoord.Y, texCoord.Z);
            }

        private:
            const ShaderLocations& _l;
        };

        class OpenGlSceneShaderSet final : public SceneShaderSet
        {
        public:
            OpenGlSceneShaderSet(GraphicsDevice& device, const SceneShaderSources& sources)
            {
                // The order the renderer always compiled them in, so the
                // names come out as they did.
                std::string vertexLog;
                std::string fragmentLog;
                try { _mainVertex = CreateGlslShader(device, ShaderStage::Vertex, *sources.MainVertex); }
                catch (const std::exception& ex) { vertexLog = ex.what(); }
                try { _mainFragment = CreateGlslShader(device, ShaderStage::Fragment, *sources.MainFragment); }
                catch (const std::exception& ex) { fragmentLog = ex.what(); }
                if (!_mainVertex || !_mainFragment)
                {
                    throw std::runtime_error("Failed to compile main shaders. vertex: " + vertexLog
                        + " fragment: " + fragmentLog);
                }
                const std::int32_t main = ProgramFor(device, *_mainVertex, *_mainFragment);
                try
                {
                    _compositeVertex = CreateGlslShader(device, ShaderStage::Vertex, *sources.CompositeVertex);
                    _compositeFragment = CreateGlslShader(device, ShaderStage::Fragment, *sources.CompositeFragment);
                }
                catch (const std::exception&)
                {
                    throw std::runtime_error("Failed to compile RTT shaders.");
                }
                const std::int32_t composite = ProgramFor(device, *_compositeVertex, *_compositeFragment);
                try
                {
                    _shiftFragment = CreateGlslShader(device, ShaderStage::Fragment, *sources.ShiftFragment);
                }
                catch (const std::exception&)
                {
                    throw std::runtime_error("Failed to compile shift shader.");
                }
                const std::int32_t shift = ProgramFor(device, *_compositeVertex, *_shiftFragment);
                try
                {
                    _celFragment = CreateGlslShader(device, ShaderStage::Fragment, *sources.CelFragment);
                }
                catch (const std::exception& ex)
                {
                    throw std::runtime_error(std::string("Failed to compile the cel shading shader. ") + ex.what());
                }
                const std::int32_t cel = ProgramFor(device, *_compositeVertex, *_celFragment);

                const auto at = [](std::int32_t program, const char* name)
                {
                    return GL::GetUniformLocation(program, name);
                };
                ShaderLocations& l = _locations;
                l.UseLight = at(main, "use_light");
                l.ShowColors = at(main, "show_colors");
                l.UseTexture = at(main, "use_texture");
                l.Light1Color = at(main, "light1col");
                l.Light1Vector = at(main, "light1vec");
                l.Light2Color = at(main, "light2col");
                l.Light2Vector = at(main, "light2vec");
                l.Diffuse = at(main, "diffuse");
                l.Ambient = at(main, "ambient");
                l.Specular = at(main, "specular");
                l.Emission = at(main, "emission");
                l.UseFog = at(main, "fog_enable");
                l.CelBands = at(main, "cel_bands");
                l.UseFlat = at(main, "use_flat");
                l.FlatColor = at(main, "flat_color");
                l.FogColor = at(main, "fog_color");
                l.FogMinDistance = at(main, "fog_min");
                l.FogMaxDistance = at(main, "fog_max");
                l.UseOverride = at(main, "use_override");
                l.OverrideColor = at(main, "override_color");
                l.UsePaletteOverride = at(main, "use_pal_override");
                l.PaletteOverrideColor = at(main, "pal_override_color");
                l.MaterialAlpha = at(main, "mat_alpha");
                l.MaterialMode = at(main, "mat_mode");
                l.ViewMatrix = at(main, "view_mtx");
                l.ViewInvMatrix = at(main, "view_inv_mtx");
                l.ProjectionMatrix = at(main, "proj_mtx");
                l.TextureMatrix = at(main, "tex_mtx");
                l.TexgenMode = at(main, "texgen_mode");
                l.MatrixStack = at(main, "mtx_stack");
                l.ToonTable = at(main, "toon_table");
                l.CelOutline = at(cel, "outline");
                l.CelTexelWidth = at(cel, "texel_w");
                l.CelTexelHeight = at(cel, "texel_h");
                l.CelNearPlane = at(cel, "near_plane");
                l.CelFarPlane = at(cel, "far_plane");
                l.CelDepthQuantum = at(cel, "depth_quantum");
                l.CelProbe = at(cel, "probe");
                l.FadeColor = at(composite, "fade_color");
                l.LayerAlpha = at(composite, "alpha");
                l.UseMask = at(composite, "use_mask");
                l.ViewWidth = at(composite, "view_width");
                l.ViewHeight = at(composite, "view_height");
                const auto textureUnits = [&](std::int32_t native, std::string_view program) {
                    GL::UseProgram(native);
                    for (const auto& texture : SceneShaderAbi::Textures)
                        if (texture.program == program)
                            GL::Uniform1(at(native, texture.name.data()), static_cast<std::int32_t>(texture.unit));
                };
                textureUnits(composite, "composite");
                textureUnits(cel, "cel");
                textureUnits(shift, "shift");
                textureUnits(main, "main");
                l.ShiftTable = at(shift, "shift_table");
                l.ShiftIndex = at(shift, "shift_idx");
                l.ShiftFactor = at(shift, "shift_fac");
                l.LerpFactor = at(shift, "lerp_fac");
                l.WhiteoutTable = at(shift, "white_table");
                l.WhiteoutFactor = at(shift, "white_fac");
                _constants = std::make_unique<OpenGlShaderConstants>(_locations);
                GL::UseProgram(shift);
                _constants->SetShiftTable(sources.ShiftTable);
                GL::UseProgram(main);
                GL::Uniform3(l.ToonTable, static_cast<std::int32_t>(sources.ToonTable.size() / 3U),
                    sources.ToonTable.data());
            }

            [[nodiscard]] const Shader& Vertex(SceneProgram program) const override
            {
                return program == SceneProgram::Main ? *_mainVertex : *_compositeVertex;
            }

            [[nodiscard]] const Shader& Fragment(SceneProgram program) const override
            {
                switch (program)
                {
                case SceneProgram::Main: return *_mainFragment;
                case SceneProgram::Composite: return *_compositeFragment;
                case SceneProgram::Shift: return *_shiftFragment;
                case SceneProgram::CelOutline:
                default: return *_celFragment;
                }
            }

            [[nodiscard]] ShaderConstantSink& Constants() override { return *_constants; }

        private:
            std::unique_ptr<Shader> _mainVertex;
            std::unique_ptr<Shader> _mainFragment;
            std::unique_ptr<Shader> _compositeVertex;
            std::unique_ptr<Shader> _compositeFragment;
            std::unique_ptr<Shader> _shiftFragment;
            std::unique_ptr<Shader> _celFragment;
            ShaderLocations _locations{};
            std::unique_ptr<OpenGlShaderConstants> _constants;
        };
    }

    std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(GraphicsDevice& device, const SceneShaderSources& sources)
    {
        return std::make_unique<OpenGlSceneShaderSet>(device, sources);
    }
}
