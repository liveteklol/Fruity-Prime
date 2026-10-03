#if defined(__ANDROID__)
#include "OpenGlAndroidGeometryInternal.inc"
#else
#include "OpenGlGeometry.hpp"
#include "OpenGlDevice.hpp"
#include "../../OpenTK/GL.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    namespace
    {
        namespace GL = ::OpenTK::Graphics::OpenGL::GL;
        constexpr auto Position = GL::VertexInput::Position;
        constexpr auto Normal = GL::VertexInput::Normal;
        constexpr auto Color = GL::VertexInput::Color;
        constexpr auto TexCoord = GL::VertexInput::TexCoord;

        struct MeshVertex final { float Position[3], Normal[3], Color[4], TexCoord[3]; };
        struct DynamicVertex final { float Position[3], TexCoord[3]; };
        enum class Mode : std::uint8_t { Inherited, Static, Mixed };

        template <typename T>
        std::unique_ptr<Buffer> Upload(GraphicsDevice& device, std::span<const T> values, BufferUsage usage,
            MemoryUsage memory = MemoryUsage::GpuOnly)
        {
            if (values.empty()) return {};
            BufferDesc desc{};
            desc.size = values.size_bytes(); desc.usage = usage | BufferUsage::TransferDst; desc.memoryUsage = memory;
            auto buffer = device.CreateBuffer(desc);
            device.WriteBuffer(*buffer, 0, std::as_bytes(values));
            return buffer;
        }
        std::uint32_t Count(std::size_t value)
        {
            if (value > UINT32_MAX) throw std::overflow_error("Geometry index count exceeds uint32_t.");
            return static_cast<std::uint32_t>(value);
        }

        class OpenGlGpuMesh final : public GpuMeshResource
        {
        public:
            OpenGlGpuMesh(GraphicsDevice& device, CommandList& commands, const RendererGeometry& geometry)
                : _device(device), _commands(commands), _terminal(geometry.TerminalState),
                  _terminalAttributes(geometry.TerminalAttributeState), _states(geometry.VertexAttributeStates)
            {
                if (_states.size() != geometry.Vertices.size())
                    throw std::invalid_argument("GPU mesh vertex attribute-state count does not match the vertex count.");
                bool anyColor = false, allColor = true, anyNormal = false, allNormal = true;
                _vertices.reserve(geometry.Vertices.size());
                for (std::size_t i = 0; i < geometry.Vertices.size(); ++i)
                {
                    const auto& v = geometry.Vertices[i];
                    _vertices.push_back({{v.Position.X, v.Position.Y, v.Position.Z}, {v.Normal.X, v.Normal.Y, v.Normal.Z},
                        {v.Color.X, v.Color.Y, v.Color.Z, v.Color.W}, {v.TexCoord.X, v.TexCoord.Y, static_cast<float>(v.MatrixIndex)}});
                    const bool color = HasSceneVertexAttributeState(_states[i], SceneVertexAttributeState::Color);
                    const bool normal = HasSceneVertexAttributeState(_states[i], SceneVertexAttributeState::Normal);
                    anyColor |= color; allColor &= color; anyNormal |= normal; allNormal &= normal;
                }
                _colorMode = !anyColor ? Mode::Inherited : allColor ? Mode::Static : Mode::Mixed;
                _normalMode = !anyNormal ? Mode::Inherited : allNormal ? Mode::Static : Mode::Mixed;
                const auto plan = BuildGpuMeshDrawPlan(geometry);
                std::vector<std::uint32_t> indices;
                for (const auto& range : plan.Ranges)
                {
                    const auto first = Count(indices.size());
                    AppendSceneTriangleIndices(indices, std::span(geometry.Indices).subspan(range.FirstIndex, range.IndexCount), range.Topology);
                    const auto end = Count(indices.size());
                    if (end > first) _ranges.emplace_back(first, end - first);
                }
                _vertexBuffer = Upload(_device, std::span<const MeshVertex>(_vertices), BufferUsage::Vertex);
                _indexBuffer = Upload(_device, std::span<const std::uint32_t>(indices), BufferUsage::Index);
                if (_colorMode == Mode::Mixed)
                {
                    _colorScratch.resize(_vertices.size() * 4);
                    _colorBuffer = Upload(_device, std::span<const float>(_colorScratch), BufferUsage::Vertex, MemoryUsage::CpuToGpu);
                }
                if (_normalMode == Mode::Mixed)
                {
                    _normalScratch.resize(_vertices.size() * 3);
                    _normalBuffer = Upload(_device, std::span<const float>(_normalScratch), BufferUsage::Vertex, MemoryUsage::CpuToGpu);
                }
            }

            void Draw() override
            {
                float currentColor[4], currentNormal[3];
                GL::GetFloat(GL::GetPName::CurrentColor, currentColor);
                GL::GetFloat(GL::GetPName::CurrentNormal, currentNormal);
                if (_vertexBuffer && _indexBuffer)
                {
                    std::array<VertexBufferLayoutDesc, 3> buffers{};
                    std::array<VertexAttributeDesc, 4> attributes{};
                    std::size_t bufferCount = 1, attributeCount = 2;
                    buffers[0] = {0, sizeof(MeshVertex)};
                    attributes[0] = {Position, 0, VertexFormat::Float3, offsetof(MeshVertex, Position)};
                    attributes[1] = {TexCoord, 0, VertexFormat::Float3, offsetof(MeshVertex, TexCoord)};
                    _commands.SetVertexBuffer(0, *_vertexBuffer);
                    _commands.SetIndexBuffer(*_indexBuffer, IndexType::UInt32);
                    Configure(_colorMode, Color, VertexFormat::Float4, offsetof(MeshVertex, Color), 4,
                        SceneVertexAttributeState::Color, currentColor, _colorScratch, _colorBuffer,
                        buffers, bufferCount, attributes, attributeCount);
                    Configure(_normalMode, Normal, VertexFormat::Float3, offsetof(MeshVertex, Normal), 3,
                        SceneVertexAttributeState::Normal, currentNormal, _normalScratch, _normalBuffer,
                        buffers, bufferCount, attributes, attributeCount);
                    for (const auto& [first, count] : _ranges)
                        DrawSceneGeometry(_commands, std::span(buffers).first(bufferCount), std::span(attributes).first(attributeCount),
                            PrimitiveTopology::TriangleList, count, first);
                }
                // Generic array draws may leave current attributes undefined.
                // Restore the display-list terminal state explicitly.
                if (HasSceneVertexAttributeState(_terminalAttributes, SceneVertexAttributeState::Color))
                    GL::Color4(_terminal.Color.X, _terminal.Color.Y, _terminal.Color.Z, _terminal.Color.W);
                else GL::Color4(currentColor[0], currentColor[1], currentColor[2], currentColor[3]);
                if (HasSceneVertexAttributeState(_terminalAttributes, SceneVertexAttributeState::Normal))
                    GL::Normal3(_terminal.Normal.X, _terminal.Normal.Y, _terminal.Normal.Z);
                else GL::Normal3(currentNormal[0], currentNormal[1], currentNormal[2]);
                GL::TexCoord3(_terminal.TexCoord.X, _terminal.TexCoord.Y, static_cast<float>(_terminal.MatrixIndex));
            }
        private:
            void Configure(Mode mode, std::uint32_t location, VertexFormat format, std::uint32_t offset, unsigned components,
                SceneVertexAttributeState flag, const float* inherited, std::vector<float>& scratch, const std::unique_ptr<Buffer>& buffer,
                std::array<VertexBufferLayoutDesc, 3>& buffers, std::size_t& bufferCount,
                std::array<VertexAttributeDesc, 4>& attributes, std::size_t& attributeCount)
            {
                if (mode == Mode::Inherited) return;
                if (mode == Mode::Static) { attributes[attributeCount++] = {location, 0, format, offset}; return; }
                for (std::size_t i = 0; i < _vertices.size(); ++i)
                {
                    const float* value = HasSceneVertexAttributeState(_states[i], flag)
                        ? (flag == SceneVertexAttributeState::Color ? _vertices[i].Color : _vertices[i].Normal) : inherited;
                    std::copy_n(value, components, scratch.data() + i * components);
                }
                _device.WriteBuffer(*buffer, 0, std::as_bytes(std::span(scratch)));
                const auto slot = static_cast<std::uint32_t>(bufferCount);
                buffers[bufferCount++] = {slot, components * sizeof(float)};
                attributes[attributeCount++] = {location, slot, format, 0};
                _commands.SetVertexBuffer(slot, *buffer);
            }
            GraphicsDevice& _device;
            CommandList& _commands;
            SceneVertex _terminal{};
            SceneVertexAttributeState _terminalAttributes{};
            std::vector<SceneVertexAttributeState> _states;
            std::vector<MeshVertex> _vertices;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> _ranges;
            Mode _colorMode = Mode::Inherited, _normalMode = Mode::Inherited;
            std::vector<float> _colorScratch, _normalScratch;
            std::unique_ptr<Buffer> _vertexBuffer, _indexBuffer, _colorBuffer, _normalBuffer;
        };

        class OpenGlTransientGeometry final : public TransientGeometryResource
        {
        public:
            OpenGlTransientGeometry(GraphicsDevice& device, CommandList& commands) : _device(device), _commands(commands) {}
            void BeginFrame() override {}
            void Draw(TransientPrimitiveTopology topology, std::span<const TransientVertex> input, bool hasTexCoords) override
            {
                if (input.empty()) return;
                _vertices.resize(input.size()); _sequence.resize(input.size()); _indices.clear();
                BuildTransientIndexSequence(_sequence);
                AppendTransientDrawIndices(_indices, _sequence, topology);
                if (_indices.empty()) return;
                for (std::size_t i = 0; i < input.size(); ++i)
                {
                    const auto& v = input[i];
                    _vertices[i] = {{v.Position.X, v.Position.Y, v.Position.Z}, {v.TexCoord.X, v.TexCoord.Y, v.TexCoord.Z}};
                }
                WriteStream(_vertexBuffer, std::span<const DynamicVertex>(_vertices), BufferUsage::Vertex);
                WriteStream(_indexBuffer, std::span<const std::uint32_t>(_indices), BufferUsage::Index);
                const std::array<VertexBufferLayoutDesc, 1> buffers{{{0, sizeof(DynamicVertex)}}};
                const std::array<VertexAttributeDesc, 2> attributes{{
                    {Position, 0, VertexFormat::Float3, offsetof(DynamicVertex, Position)},
                    {TexCoord, 0, VertexFormat::Float3, offsetof(DynamicVertex, TexCoord)}}};
                float color[4], normal[3], texCoord[4];
                GL::GetFloat(GL::GetPName::CurrentColor, color); GL::GetFloat(GL::GetPName::CurrentNormal, normal);
                GL::GetFloat(static_cast<GL::GetPName>(0x0B03), texCoord);
                _commands.SetVertexBuffer(0, *_vertexBuffer); _commands.SetIndexBuffer(*_indexBuffer, IndexType::UInt32);
                DrawSceneGeometry(_commands, buffers, std::span(attributes).first(hasTexCoords ? 2 : 1),
                    topology == TransientPrimitiveTopology::LineLoop ? PrimitiveTopology::LineList : PrimitiveTopology::TriangleList, Count(_indices.size()));
                GL::Color4(color[0], color[1], color[2], color[3]); GL::Normal3(normal[0], normal[1], normal[2]);
                GL::TexCoord3(texCoord[0], texCoord[1], texCoord[2]);
            }
        private:
            template <typename T> void WriteStream(std::unique_ptr<Buffer>& buffer, std::span<const T> values, BufferUsage usage)
            {
                // Keep two stream names across changing primitive sizes.
                // A full allocation write lets the backend orphan storage
                // without synchronously waiting for its previous draw.
                if (!buffer || buffer->Desc().size < values.size_bytes())
                {
                    std::uint64_t capacity = buffer ? buffer->Desc().size : 256;
                    while (capacity < values.size_bytes()) capacity *= 2;
                    BufferDesc desc{};
                    desc.size = capacity; desc.usage = usage | BufferUsage::TransferDst;
                    desc.memoryUsage = MemoryUsage::CpuToGpu;
                    buffer = _device.CreateBuffer(desc);
                }
                _uploadScratch.resize(static_cast<std::size_t>(buffer->Desc().size));
                std::memcpy(_uploadScratch.data(), values.data(), values.size_bytes());
                _device.WriteBuffer(*buffer, 0, _uploadScratch);
            }
            GraphicsDevice& _device;
            CommandList& _commands;
            std::vector<DynamicVertex> _vertices;
            std::vector<std::uint32_t> _sequence, _indices;
            std::vector<std::byte> _uploadScratch;
            std::unique_ptr<Buffer> _vertexBuffer, _indexBuffer;
        };
    }

    std::shared_ptr<MphRead::GpuMeshResource> CreateGpuMeshResource(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry)
    { return std::make_shared<OpenGlGpuMesh>(device, commands, geometry); }
    std::shared_ptr<MphRead::TransientGeometryResource> CreateTransientGeometryResource(GraphicsDevice& device, CommandList& commands)
    { return std::make_shared<OpenGlTransientGeometry>(device, commands); }
}
#endif
