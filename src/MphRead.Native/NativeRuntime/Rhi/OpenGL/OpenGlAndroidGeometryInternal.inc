#include "OpenGlGeometry.hpp"

#include "OpenGlDevice.hpp"
#include "../../OpenTK/GL.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace
    {
        namespace GL = ::OpenTK::Graphics::OpenGL::GL;

        constexpr std::uint32_t PositionAttribute = GL::VertexInput::Position;
        constexpr std::uint32_t ColorAttribute = GL::VertexInput::Color;
        constexpr std::uint32_t NormalAttribute = GL::VertexInput::Normal;
        constexpr std::uint32_t TexCoordAttribute = GL::VertexInput::TexCoord;

        struct OpenGlMeshVertex final
        {
            float Position[3]{};
            float Normal[3]{};
            float Color[4]{};
            float TexCoord[3]{};
        };

        [[nodiscard]] GL::PrimitiveType ToGlTopology(ScenePrimitiveTopology topology)
        {
            switch (topology)
            {
            case ScenePrimitiveTopology::Triangles:
                return GL::PrimitiveType::Triangles;
            case ScenePrimitiveTopology::Quads:
                return GL::PrimitiveType::Quads;
            case ScenePrimitiveTopology::TriangleStrip:
                return GL::PrimitiveType::TriangleStrip;
            case ScenePrimitiveTopology::QuadStrip:
                return GL::PrimitiveType::QuadStrip;
            }
            throw std::invalid_argument("Unknown scene primitive topology.");
        }

        [[nodiscard]] GL::PrimitiveType ToGlTopology(TransientPrimitiveTopology topology)
        {
            switch (topology)
            {
            case TransientPrimitiveTopology::LineLoop:
                return GL::PrimitiveType::LineLoop;
            case TransientPrimitiveTopology::Triangles:
                return GL::PrimitiveType::Triangles;
            case TransientPrimitiveTopology::TriangleStrip:
                return GL::PrimitiveType::TriangleStrip;
            case TransientPrimitiveTopology::TriangleFan:
                return GL::PrimitiveType::TriangleFan;
            case TransientPrimitiveTopology::Quads:
                return GL::PrimitiveType::Quads;
            case TransientPrimitiveTopology::QuadStrip:
                return GL::PrimitiveType::QuadStrip;
            }
            throw std::invalid_argument("Unknown transient primitive topology.");
        }

        [[nodiscard]] OpenGlMeshVertex PackVertex(const SceneVertex& vertex)
        {
            OpenGlMeshVertex packed{};
            packed.Position[0] = vertex.Position.X;
            packed.Position[1] = vertex.Position.Y;
            packed.Position[2] = vertex.Position.Z;
            packed.Normal[0] = vertex.Normal.X;
            packed.Normal[1] = vertex.Normal.Y;
            packed.Normal[2] = vertex.Normal.Z;
            packed.Color[0] = vertex.Color.X;
            packed.Color[1] = vertex.Color.Y;
            packed.Color[2] = vertex.Color.Z;
            packed.Color[3] = vertex.Color.W;
            packed.TexCoord[0] = vertex.TexCoord.X;
            packed.TexCoord[1] = vertex.TexCoord.Y;
            packed.TexCoord[2] = static_cast<float>(vertex.MatrixIndex);
            return packed;
        }

        class OpenGlGpuMesh final : public GpuMeshResource
        {
        public:
            explicit OpenGlGpuMesh(const RendererGeometry& geometry)
                : _drawPlan(BuildGpuMeshDrawPlan(geometry)),
                  _terminalState(geometry.TerminalState),
                  _terminalAttributeState(geometry.TerminalAttributeState),
                  _attributeStates(geometry.VertexAttributeStates)
            {
                if (_attributeStates.size() != geometry.Vertices.size())
                {
                    throw std::invalid_argument(
                        "GPU mesh vertex attribute-state count does not match the vertex count.");
                }

                _vertices.reserve(geometry.Vertices.size());
                bool anyColor = false;
                bool allColor = true;
                bool anyNormal = false;
                bool allNormal = true;
                for (std::size_t i = 0; i < geometry.Vertices.size(); ++i)
                {
                    _vertices.push_back(PackVertex(geometry.Vertices[i]));
                    const SceneVertexAttributeState state = _attributeStates[i];
                    const bool hasColor = HasSceneVertexAttributeState(
                        state, SceneVertexAttributeState::Color);
                    const bool hasNormal = HasSceneVertexAttributeState(
                        state, SceneVertexAttributeState::Normal);
                    anyColor = anyColor || hasColor;
                    allColor = allColor && hasColor;
                    anyNormal = anyNormal || hasNormal;
                    allNormal = allNormal && hasNormal;
                }
                _colorMode = !anyColor ? AttributeMode::Inherited
                    : allColor ? AttributeMode::Static : AttributeMode::Mixed;
                _normalMode = !anyNormal ? AttributeMode::Inherited
                    : allNormal ? AttributeMode::Static : AttributeMode::Mixed;

                if (_colorMode == AttributeMode::Mixed)
                {
                    _colorScratch.resize(_vertices.size() * 4U);
                }
                if (_normalMode == AttributeMode::Mixed)
                {
                    _normalScratch.resize(_vertices.size() * 3U);
                }

                try
                {
                    _vertexBuffer = CreateAndroidGeometryBuffer();
                    _indexBuffer = CreateAndroidGeometryBuffer();
                    if (_vertexBuffer == 0 || _indexBuffer == 0)
                    {
                        throw std::runtime_error("OpenGL returned buffer object 0.");
                    }
                    if (_colorMode == AttributeMode::Mixed)
                    {
                        _colorBuffer = CreateAndroidGeometryBuffer();
                        if (_colorBuffer == 0)
                        {
                            throw std::runtime_error("OpenGL returned color buffer object 0.");
                        }
                    }
                    if (_normalMode == AttributeMode::Mixed)
                    {
                        _normalBuffer = CreateAndroidGeometryBuffer();
                        if (_normalBuffer == 0)
                        {
                            throw std::runtime_error("OpenGL returned normal buffer object 0.");
                        }
                    }

                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
                    GL::BufferData(GL::BufferTarget::ArrayBuffer,
                        _vertices.size() * sizeof(OpenGlMeshVertex), _vertices.data(),
                        GL::BufferUsageHint::StaticDraw);
                    GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, _indexBuffer);
                    GL::BufferData(GL::BufferTarget::ElementArrayBuffer,
                        geometry.Indices.size() * sizeof(std::uint32_t), geometry.Indices.data(),
                        GL::BufferUsageHint::StaticDraw);
                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
                    GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);
                }
                catch (...)
                {
                    Release();
                    throw;
                }
            }

            ~OpenGlGpuMesh() override
            {
                Release();
            }

            void Draw() override
            {
                float inheritedColor[4]{};
                float inheritedNormal[3]{};
                if (_colorMode == AttributeMode::Mixed)
                {
                    GL::GetFloat(GL::GetPName::CurrentColor, inheritedColor);
                    for (std::size_t i = 0; i < _vertices.size(); ++i)
                    {
                        const bool explicitColor = HasSceneVertexAttributeState(
                            _attributeStates[i], SceneVertexAttributeState::Color);
                        for (std::size_t component = 0; component < 4U; ++component)
                        {
                            _colorScratch[i * 4U + component] = explicitColor
                                ? _vertices[i].Color[component] : inheritedColor[component];
                        }
                    }
                }
                if (_normalMode == AttributeMode::Mixed)
                {
                    GL::GetFloat(GL::GetPName::CurrentNormal, inheritedNormal);
                    for (std::size_t i = 0; i < _vertices.size(); ++i)
                    {
                        const bool explicitNormal = HasSceneVertexAttributeState(
                            _attributeStates[i], SceneVertexAttributeState::Normal);
                        for (std::size_t component = 0; component < 3U; ++component)
                        {
                            _normalScratch[i * 3U + component] = explicitNormal
                                ? _vertices[i].Normal[component] : inheritedNormal[component];
                        }
                    }
                }

                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
                GL::EnableVertexAttribArray(PositionAttribute);
                GL::VertexAttribPointer(PositionAttribute, 3, GL::PointerType::Float, false,
                    static_cast<std::int32_t>(sizeof(OpenGlMeshVertex)),
                    reinterpret_cast<const void*>(offsetof(OpenGlMeshVertex, Position)));
                GL::EnableVertexAttribArray(TexCoordAttribute);
                GL::VertexAttribPointer(TexCoordAttribute, 3, GL::PointerType::Float, false,
                    static_cast<std::int32_t>(sizeof(OpenGlMeshVertex)),
                    reinterpret_cast<const void*>(offsetof(OpenGlMeshVertex, TexCoord)));

                ConfigureColorArray();
                ConfigureNormalArray();

                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, _indexBuffer);
                for (const GpuMeshDrawRange& range : _drawPlan.Ranges)
                {
                    GL::DrawElements(ToGlTopology(range.Topology),
                        static_cast<std::int32_t>(range.IndexCount),
                        GL::DrawElementsType::UnsignedInt,
                        reinterpret_cast<const void*>(range.IndexByteOffset));
                }

                GL::DisableVertexAttribArray(PositionAttribute);
                GL::DisableVertexAttribArray(TexCoordAttribute);
                GL::DisableVertexAttribArray(ColorAttribute);
                GL::DisableVertexAttribArray(NormalAttribute);
                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);

                if (HasSceneVertexAttributeState(
                    _terminalAttributeState, SceneVertexAttributeState::Color))
                {
                    GL::Color4(_terminalState.Color.X, _terminalState.Color.Y,
                        _terminalState.Color.Z, _terminalState.Color.W);
                }
                if (HasSceneVertexAttributeState(
                    _terminalAttributeState, SceneVertexAttributeState::Normal))
                {
                    GL::Normal3(_terminalState.Normal.X, _terminalState.Normal.Y,
                        _terminalState.Normal.Z);
                }
                GL::TexCoord3(_terminalState.TexCoord.X, _terminalState.TexCoord.Y,
                    static_cast<float>(_terminalState.MatrixIndex));
            }

        private:
            enum class AttributeMode : std::uint8_t
            {
                Inherited,
                Static,
                Mixed
            };

            void ConfigureColorArray()
            {
                if (_colorMode == AttributeMode::Inherited)
                {
                    GL::DisableVertexAttribArray(ColorAttribute);
                    return;
                }
                GL::EnableVertexAttribArray(ColorAttribute);
                if (_colorMode == AttributeMode::Static)
                {
                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
                    GL::VertexAttribPointer(ColorAttribute, 4, GL::PointerType::Float, false,
                        static_cast<std::int32_t>(sizeof(OpenGlMeshVertex)),
                        reinterpret_cast<const void*>(offsetof(OpenGlMeshVertex, Color)));
                }
                else
                {
                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _colorBuffer);
                    GL::BufferData(GL::BufferTarget::ArrayBuffer,
                        _colorScratch.size() * sizeof(float), _colorScratch.data(),
                        GL::BufferUsageHint::StreamDraw);
                    GL::VertexAttribPointer(ColorAttribute, 4, GL::PointerType::Float, false, 0, nullptr);
                }
            }

            void ConfigureNormalArray()
            {
                if (_normalMode == AttributeMode::Inherited)
                {
                    GL::DisableVertexAttribArray(NormalAttribute);
                    return;
                }
                GL::EnableVertexAttribArray(NormalAttribute);
                if (_normalMode == AttributeMode::Static)
                {
                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
                    GL::VertexAttribPointer(NormalAttribute, 3, GL::PointerType::Float, false,
                        static_cast<std::int32_t>(sizeof(OpenGlMeshVertex)),
                        reinterpret_cast<const void*>(offsetof(OpenGlMeshVertex, Normal)));
                }
                else
                {
                    GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _normalBuffer);
                    GL::BufferData(GL::BufferTarget::ArrayBuffer,
                        _normalScratch.size() * sizeof(float), _normalScratch.data(),
                        GL::BufferUsageHint::StreamDraw);
                    GL::VertexAttribPointer(NormalAttribute, 3, GL::PointerType::Float, false, 0, nullptr);
                }
            }

            void Release() noexcept
            {
                try
                {
                    RetireAndroidGeometryBuffer(_normalBuffer);
                    RetireAndroidGeometryBuffer(_colorBuffer);
                    RetireAndroidGeometryBuffer(_indexBuffer);
                    RetireAndroidGeometryBuffer(_vertexBuffer);
                }
                catch (...)
                {
                    // Scene::ReleaseGpuResources clears the cache while the context is live.
                }
                _normalBuffer = 0;
                _colorBuffer = 0;
                _indexBuffer = 0;
                _vertexBuffer = 0;
            }

            GpuMeshDrawPlan _drawPlan{};
            SceneVertex _terminalState{};
            SceneVertexAttributeState _terminalAttributeState = SceneVertexAttributeState::None;
            std::vector<SceneVertexAttributeState> _attributeStates{};
            std::vector<OpenGlMeshVertex> _vertices{};
            AttributeMode _colorMode = AttributeMode::Inherited;
            AttributeMode _normalMode = AttributeMode::Inherited;
            std::vector<float> _colorScratch{};
            std::vector<float> _normalScratch{};
            std::int32_t _vertexBuffer = 0;
            std::int32_t _indexBuffer = 0;
            std::int32_t _colorBuffer = 0;
            std::int32_t _normalBuffer = 0;
        };

        struct OpenGlTransientVertex final
        {
            float Position[3]{};
            float TexCoord[3]{};
        };

        class OpenGlTransientGeometry final : public TransientGeometryResource
        {
        public:
            OpenGlTransientGeometry()
            {
                try
                {
                    _vertexBuffer = CreateAndroidGeometryBuffer();
                    _indexBuffer = CreateAndroidGeometryBuffer();
                    if (_vertexBuffer == 0 || _indexBuffer == 0)
                    {
                        throw std::runtime_error("OpenGL returned transient buffer object 0.");
                    }
                }
                catch (...)
                {
                    Release();
                    throw;
                }
            }

            ~OpenGlTransientGeometry() override
            {
                Release();
            }

            void BeginFrame() override
            {
                // The same two stream buffers are orphaned by each Draw call.
                // Keeping them scene-owned avoids per-draw GL object churn.
            }

            void Draw(TransientPrimitiveTopology topology,
                std::span<const TransientVertex> vertices, bool hasTexCoords) override
            {
                if (vertices.empty())
                {
                    return;
                }

                _vertices.resize(vertices.size());
                _indices.resize(vertices.size());
                BuildTransientIndexSequence(_indices);
                for (std::size_t i = 0; i < vertices.size(); ++i)
                {
                    const TransientVertex& source = vertices[i];
                    OpenGlTransientVertex& target = _vertices[i];
                    target.Position[0] = source.Position.X;
                    target.Position[1] = source.Position.Y;
                    target.Position[2] = source.Position.Z;
                    target.TexCoord[0] = source.TexCoord.X;
                    target.TexCoord[1] = source.TexCoord.Y;
                    target.TexCoord[2] = source.TexCoord.Z;
                }

                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, _vertexBuffer);
                GL::BufferData(GL::BufferTarget::ArrayBuffer,
                    _vertices.size() * sizeof(OpenGlTransientVertex), _vertices.data(),
                    GL::BufferUsageHint::StreamDraw);
                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, _indexBuffer);
                GL::BufferData(GL::BufferTarget::ElementArrayBuffer,
                    _indices.size() * sizeof(std::uint32_t), _indices.data(),
                    GL::BufferUsageHint::StreamDraw);

                GL::DisableVertexAttribArray(ColorAttribute);
                GL::DisableVertexAttribArray(NormalAttribute);
                GL::EnableVertexAttribArray(PositionAttribute);
                GL::VertexAttribPointer(PositionAttribute, 3, GL::PointerType::Float, false,
                    static_cast<std::int32_t>(sizeof(OpenGlTransientVertex)),
                    reinterpret_cast<const void*>(offsetof(OpenGlTransientVertex, Position)));
                if (hasTexCoords)
                {
                    GL::EnableVertexAttribArray(TexCoordAttribute);
                    GL::VertexAttribPointer(TexCoordAttribute, 3, GL::PointerType::Float, false,
                        static_cast<std::int32_t>(sizeof(OpenGlTransientVertex)),
                        reinterpret_cast<const void*>(offsetof(OpenGlTransientVertex, TexCoord)));
                }
                else
                {
                    GL::DisableVertexAttribArray(TexCoordAttribute);
                }

                GL::DrawElements(ToGlTopology(topology),
                    static_cast<std::int32_t>(_indices.size()),
                    GL::DrawElementsType::UnsignedInt, nullptr);

                GL::DisableVertexAttribArray(PositionAttribute);
                GL::DisableVertexAttribArray(TexCoordAttribute);
                GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
                GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, 0);
            }

        private:
            void Release() noexcept
            {
                try
                {
                    RetireAndroidGeometryBuffer(_indexBuffer);
                    RetireAndroidGeometryBuffer(_vertexBuffer);
                }
                catch (...)
                {
                    // Scene::ReleaseGpuResources releases this while the context is current.
                }
                _indexBuffer = 0;
                _vertexBuffer = 0;
            }

            std::vector<OpenGlTransientVertex> _vertices{};
            std::vector<std::uint32_t> _indices{};
            std::int32_t _vertexBuffer = 0;
            std::int32_t _indexBuffer = 0;
        };
    }

    std::shared_ptr<MphRead::GpuMeshResource> CreateGpuMeshResource(
        GraphicsDevice&, CommandList&, const MphRead::RendererGeometry& geometry)
    {
        return std::make_shared<OpenGlGpuMesh>(geometry);
    }

    std::shared_ptr<MphRead::TransientGeometryResource>
        CreateTransientGeometryResource(GraphicsDevice&, CommandList&)
    {
        return std::make_shared<OpenGlTransientGeometry>();
    }
}
