#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace fp {

namespace fs = std::filesystem;

namespace {

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("read past end of file at offset " + std::to_string(offset));
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <typename T>
std::vector<T> readArray(const std::vector<uint8_t>& bytes, size_t offset, size_t count)
{
    std::vector<T> out;
    out.reserve(count);
    for (size_t i = 0; i < count; i++) {
        out.push_back(readAt<T>(bytes, offset + i * sizeof(T)));
    }
    return out;
}

std::string fixedString(const char* chars, size_t max)
{
    return std::string(chars, strnlen(chars, max));
}

float angleToRadians(int16_t value) { return value / 65536.0f * 2.0f * std::numbers::pi_v<float>; }

std::array<float, 3> toFloat(const raw::Vector3Fx& v) { return {fxToFloat(v.x), fxToFloat(v.y), fxToFloat(v.z)}; }

std::array<float, 3> toFloat(const raw::ColorRgb& c) { return {c.r / 31.0f, c.g / 31.0f, c.b / 31.0f}; }

int32_t signExtend(uint32_t value, int bits)
{
    const uint32_t sign = 1u << (bits - 1);
    value &= (1u << bits) - 1;
    return static_cast<int32_t>((value ^ sign) - sign);
}

uint8_t channel5to8(uint32_t value) { return static_cast<uint8_t>(std::lround((value & 0x1F) / 31.0f * 255.0f)); }

uint32_t packColor(uint16_t color, uint8_t alpha)
{
    return channel5to8(color) | (channel5to8(color >> 5) << 8) | (channel5to8(color >> 10) << 16)
        | (static_cast<uint32_t>(alpha) << 24);
}

enum class Op : uint32_t {
    NOP = 0x400,
    MTX_RESTORE = 0x450,
    COLOR = 0x480,
    NORMAL = 0x484,
    TEXCOORD = 0x488,
    VTX_16 = 0x48C,
    VTX_10 = 0x490,
    VTX_XY = 0x494,
    VTX_XZ = 0x498,
    VTX_YZ = 0x49C,
    VTX_DIFF = 0x4A0,
    DIF_AMB = 0x4C0,
    BEGIN_VTXS = 0x500,
    END_VTXS = 0x504,
};

int arity(Op op)
{
    switch (op) {
    case Op::NOP:
    case Op::END_VTXS:
        return 0;
    case Op::VTX_16:
        return 2;
    case Op::MTX_RESTORE:
    case Op::COLOR:
    case Op::NORMAL:
    case Op::TEXCOORD:
    case Op::VTX_10:
    case Op::VTX_XY:
    case Op::VTX_XZ:
    case Op::VTX_YZ:
    case Op::VTX_DIFF:
    case Op::DIF_AMB:
    case Op::BEGIN_VTXS:
        return 1;
    }
    throw std::runtime_error("invalid display list opcode " + std::to_string(static_cast<uint32_t>(op)));
}

void emitPrimitive(uint32_t type, const std::vector<Vertex>& in, std::vector<Vertex>& out)
{
    const size_t n = in.size();
    switch (type) {
    case 0: // triangles
        for (size_t i = 0; i + 2 < n; i += 3) {
            out.insert(out.end(), {in[i], in[i + 1], in[i + 2]});
        }
        break;
    case 1: // quads
        for (size_t i = 0; i + 3 < n; i += 4) {
            out.insert(out.end(), {in[i], in[i + 1], in[i + 2], in[i], in[i + 2], in[i + 3]});
        }
        break;
    case 2: // triangle strip
        for (size_t i = 0; i + 2 < n; i++) {
            if (i % 2 == 0) {
                out.insert(out.end(), {in[i], in[i + 1], in[i + 2]});
            } else {
                out.insert(out.end(), {in[i + 1], in[i], in[i + 2]});
            }
        }
        break;
    case 3: // quad strip
        for (size_t i = 0; i + 3 < n; i += 2) {
            const Vertex& a = in[i];
            const Vertex& b = in[i + 1];
            const Vertex& c = in[i + 2];
            const Vertex& d = in[i + 3];
            out.insert(out.end(), {a, b, d, a, d, c});
        }
        break;
    default:
        throw std::runtime_error("invalid geometry type " + std::to_string(type));
    }
}

} // namespace

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

fs::path resolveCaseInsensitive(const fs::path& root, const std::string& relative)
{
    fs::path current = root;
    for (const fs::path& part : fs::path(relative)) {
        fs::path exact = current / part;
        if (fs::exists(exact)) {
            current = exact;
            continue;
        }
        std::string wanted = part.string();
        std::ranges::transform(wanted, wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        bool found = false;
        if (fs::is_directory(current)) {
            for (const auto& entry : fs::directory_iterator(current)) {
                std::string name = entry.path().filename().string();
                std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name == wanted) {
                    current = entry.path();
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return root / relative;
        }
    }
    return current;
}

Model Model::loadRoom(const fs::path& modelFile, const fs::path& textureFile, const fs::path& animationFile)
{
    const std::string model = modelFile.string();
    const std::string texture = textureFile.string();
    const std::string stem = modelFile.stem().string();
    const std::string animation = animationFile.string();
    ModelMetadata meta{stem.c_str(), model.c_str(), animation.empty() ? nullptr : animation.c_str(), nullptr,
        {RecolorMetadata{"default", model.c_str(), texture.c_str(), texture.c_str(), nullptr, {}}}};
    // Absolute paths: the root is not prepended to them.
    return loadInternal({}, meta, true);
}

Model Model::load(const fs::path& root, const ModelMetadata& meta) { return loadInternal(root, meta, false); }

Model Model::loadInternal(const fs::path& root, const ModelMetadata& meta, bool isRoom)
{
    auto resolve = [&](const char* rel) {
        return root.empty() ? fs::path(rel) : resolveCaseInsensitive(root, rel);
    };
    Model model;
    model.m_isRoom = isRoom;
    const fs::path modelFile = resolve(meta.modelPath);
    model.m_name = meta.name != nullptr ? meta.name : modelFile.stem().string();
    const auto bytes = std::make_shared<const std::vector<uint8_t>>(readFile(modelFile));
    const auto header = readAt<raw::Header>(*bytes, 0);

    model.m_scale = fxToFloat(header.scaleBase) * static_cast<float>(1 << header.scaleFactor);

    for (const raw::Node& rn : readArray<raw::Node>(*bytes, header.nodeOffset, header.nodeCount)) {
        model.m_nodes.push_back(Node{
            .name = fixedString(rn.name, sizeof(rn.name)),
            .parentIndex = rn.parentId,
            .childIndex = rn.childId,
            .nextIndex = rn.nextId,
            .enabled = rn.enabled != 0,
            .meshCount = rn.meshCount,
            .meshId = rn.meshId,
            .scale = toFloat(rn.scale),
            .angle = {angleToRadians(rn.angleX), angleToRadians(rn.angleY), angleToRadians(rn.angleZ)},
            .position = toFloat(rn.position),
            .billboardMode = static_cast<BillboardMode>(rn.billboardMode),
        });
    }

    for (const raw::Mesh& rm : readArray<raw::Mesh>(*bytes, header.meshOffset, header.meshCount)) {
        model.m_meshes.push_back(Mesh{rm.materialId, rm.dlistId});
    }

    for (const raw::Material& rm : readArray<raw::Material>(*bytes, header.materialOffset, header.materialCount)) {
        auto renderMode = static_cast<RenderMode>(rm.renderMode);
        if (renderMode == RenderMode::Unknown3 || renderMode == RenderMode::Unknown4) {
            renderMode = RenderMode::Normal;
        }
        model.m_materials.push_back(Material{
            .name = fixedString(rm.name, sizeof(rm.name)),
            .lighting = rm.lighting != 0,
            .culling = static_cast<CullingMode>(rm.culling),
            .alpha = rm.alpha / 31.0f,
            .paletteId = rm.paletteId,
            .textureId = rm.textureId,
            .xRepeat = static_cast<RepeatMode>(rm.xRepeat),
            .yRepeat = static_cast<RepeatMode>(rm.yRepeat),
            .diffuse = toFloat(rm.diffuse),
            .ambient = toFloat(rm.ambient),
            .specular = toFloat(rm.specular),
            .polygonMode = static_cast<PolygonMode>(rm.polygonMode),
            .renderMode = renderMode,
            .texgenMode = static_cast<TexgenMode>(rm.texgenMode),
            .scaleS = fxToFloat(rm.scaleS),
            .scaleT = fxToFloat(rm.scaleT),
            .translateS = fxToFloat(rm.translateS),
            .translateT = fxToFloat(rm.translateT),
            .rotateZ = rm.rotateZ / 65536.0f * 2.0f * std::numbers::pi_v<float>,
            .animationFlags = rm.animationFlags,
        });
    }

    // One recolor per metadata entry, as Read.ReadModel builds them.
    auto readPalettes = [](const std::vector<uint8_t>& paletteBytes, const std::vector<raw::Palette>& table) {
        std::vector<std::vector<uint16_t>> out;
        for (const raw::Palette& palette : table) {
            if (palette.size % 2 != 0) {
                throw std::runtime_error("palette size not divisible by 2");
            }
            out.push_back(readArray<uint16_t>(paletteBytes, palette.offset, palette.size / 2));
        }
        return out;
    };
    auto readHeaderTables = [](const std::vector<uint8_t>& b) {
        const auto h = readAt<raw::Header>(b, 0);
        return std::make_pair(readArray<raw::Texture>(b, h.textureOffset, h.textureCount),
            readArray<raw::Palette>(b, h.paletteOffset, h.paletteCount));
    };
    for (const RecolorMetadata& rm : meta.recolors) {
        Recolor recolor;
        recolor.name = rm.name != nullptr ? rm.name : "";
        std::shared_ptr<const std::vector<uint8_t>> modelBytes = bytes;
        if (resolve(rm.modelPath) != modelFile) {
            modelBytes = std::make_shared<const std::vector<uint8_t>>(readFile(resolve(rm.modelPath)));
        }
        auto [textures, paletteTable] = readHeaderTables(*modelBytes);
        recolor.textures = std::move(textures);
        const std::string modelPath = rm.modelPath;
        const std::string texturePath = rm.texturePath != nullptr ? rm.texturePath : modelPath;
        const std::string palettePath = rm.palettePath != nullptr ? rm.palettePath : texturePath;
        recolor.texelBytes = texturePath == modelPath
            ? modelBytes
            : std::make_shared<const std::vector<uint8_t>>(readFile(resolve(texturePath.c_str())));
        const std::vector<uint8_t>* paletteBytes = recolor.texelBytes.get();
        std::vector<uint8_t> separatePalette;
        if (palettePath != texturePath && rm.replaceIds.empty()) {
            separatePalette = readFile(resolve(palettePath.c_str()));
            paletteTable = readHeaderTables(separatePalette).second;
            paletteBytes = &separatePalette;
        }
        recolor.palettes = readPalettes(*paletteBytes, paletteTable);
        const std::string replacePath = rm.replacePath != nullptr ? rm.replacePath : palettePath;
        if (replacePath != texturePath && !rm.replaceIds.empty()) {
            const std::vector<uint8_t> replaceBytes = readFile(resolve(replacePath.c_str()));
            const auto replacement = readPalettes(replaceBytes, readHeaderTables(replaceBytes).second);
            for (const auto& [from, targets] : rm.replaceIds) {
                if (from < 0 || from >= static_cast<int>(replacement.size())) {
                    continue;
                }
                for (int to : targets) {
                    if (to >= 0 && to < static_cast<int>(recolor.palettes.size())) {
                        recolor.palettes[to] = replacement[from];
                    }
                }
            }
        }
        model.m_recolors.push_back(std::move(recolor));
    }

    model.m_nodeWeights = readArray<int32_t>(*bytes, header.nodeWeightOffset, header.nodeWeightCount);
    for (const char* animation : {meta.animationPath, meta.animationShare}) {
        if (animation != nullptr && animation[0] != 0) {
            model.m_animations.append(loadAnimations(resolve(animation), model.m_nodes));
        }
    }
    model.buildGeometry(*bytes, readArray<raw::DisplayList>(*bytes, header.dlistOffset, header.meshCount));
    model.computeNodeMatrices();
    return model;
}

namespace {

// Model.ComputeNodeTransforms, element for element.
Mat4 nodeTransform(const std::array<float, 3>& scale, const std::array<float, 3>& angle, const std::array<float, 3>& position)
{
    const float sinAx = std::sin(angle[0]), sinAy = std::sin(angle[1]), sinAz = std::sin(angle[2]);
    const float cosAx = std::cos(angle[0]), cosAy = std::cos(angle[1]), cosAz = std::cos(angle[2]);
    const float v18 = cosAx * cosAz;
    const float v19 = cosAx * sinAz;
    const float v20 = cosAx * cosAy;
    const float v22 = sinAx * sinAy;
    const float v17 = v19 * sinAy;
    Mat4 t;
    t.m[0][0] = scale[0] * cosAy * cosAz;
    t.m[0][1] = scale[0] * cosAy * sinAz;
    t.m[0][2] = scale[0] * -sinAy;
    t.m[1][0] = scale[1] * ((v22 * cosAz) - v19);
    t.m[1][1] = scale[1] * ((v22 * sinAz) + v18);
    t.m[1][2] = scale[1] * sinAx * cosAy;
    t.m[2][0] = scale[2] * (v18 * sinAy + sinAx * sinAz);
    t.m[2][1] = scale[2] * (v17 + (v19 * sinAy) - (sinAx * cosAz));
    t.m[2][2] = scale[2] * v20;
    t.m[3][0] = position[0];
    t.m[3][1] = position[1];
    t.m[3][2] = position[2];
    t.m[3][3] = 1.0f;
    return t;
}

} // namespace

void Model::computeNodeMatrices()
{
    // Walks child/next links from node 0, as the C# does, rather than trusting file order.
    auto visit = [&](auto&& self, int index) -> void {
        for (int i = index; i >= 0 && i < static_cast<int>(m_nodes.size());) {
            Node& node = m_nodes[i];
            const std::array<float, 3> position{
                node.position[0] / m_scale, node.position[1] / m_scale, node.position[2] / m_scale};
            const Mat4 local = nodeTransform(node.scale, node.angle, position);
            node.transform = node.parentIndex < 0 ? local : local * m_nodes[node.parentIndex].transform;
            if (node.childIndex >= 0) {
                self(self, node.childIndex);
            }
            i = node.nextIndex;
        }
    };
    if (!m_nodes.empty()) {
        visit(visit, 0);
    }
}

void Model::buildGeometry(const std::vector<uint8_t>& bytes, const std::vector<raw::DisplayList>& dlists)
{
    m_dlistRanges.assign(dlists.size(), DrawRange{});
    std::vector<bool> built(dlists.size(), false);
    std::vector<Vertex> primitive;

    // Texcoords are normalized by the texture of the first mesh that uses the
    // list, as Renderer.GenerateLists does.
    for (const Mesh& mesh : m_meshes) {
        if (mesh.dlistId < 0 || mesh.dlistId >= static_cast<int>(dlists.size()) || built[mesh.dlistId]) {
            continue;
        }
        built[mesh.dlistId] = true;
        const Material& material = m_materials.at(mesh.materialId);
        float texWidth = 0;
        float texHeight = 0;
        if (material.textureId >= 0 && material.textureId < static_cast<int>(textureCount())) {
            texWidth = m_recolors[0].textures[material.textureId].width;
            texHeight = m_recolors[0].textures[material.textureId].height;
        }
        const bool texgen = material.texgenMode == TexgenMode::Normal;

        const raw::DisplayList& dlist = dlists[mesh.dlistId];
        if (dlist.size % 4 != 0 || dlist.offset + dlist.size > bytes.size()) {
            throw std::runtime_error("malformed display list");
        }

        DrawRange range{static_cast<uint32_t>(m_vertices.size()), 0};
        Vertex current{};
        current.normal[2] = 1.0f;
        current.color[0] = current.color[1] = current.color[2] = 1.0f;
        current.color[3] = -1.0f;
        current.uv[0] = current.uv[1] = texgen ? 0.5f : 0.0f;
        uint32_t primitiveType = 0;
        bool inPrimitive = false;

        auto vertex = [&]() {
            if (inPrimitive) {
                primitive.push_back(current);
            }
        };

        size_t pointer = dlist.offset;
        const size_t end = dlist.offset + dlist.size;
        while (pointer < end) {
            uint32_t packed = readAt<uint32_t>(bytes, pointer);
            pointer += 4;
            for (int i = 0; i < 4; i++) {
                const auto op = static_cast<Op>(((packed & 0xFF) << 2) + 0x400);
                packed >>= 8;
                uint32_t args[2] = {0, 0};
                const int count = arity(op);
                for (int a = 0; a < count; a++) {
                    args[a] = readAt<uint32_t>(bytes, pointer);
                    pointer += 4;
                }
                switch (op) {
                case Op::BEGIN_VTXS:
                    primitiveType = args[0];
                    primitive.clear();
                    inPrimitive = true;
                    break;
                case Op::END_VTXS:
                    if (inPrimitive) {
                        emitPrimitive(primitiveType, primitive, m_vertices);
                    }
                    inPrimitive = false;
                    break;
                case Op::COLOR:
                    current.color[0] = (args[0] & 0x1F) / 31.0f;
                    current.color[1] = ((args[0] >> 5) & 0x1F) / 31.0f;
                    current.color[2] = ((args[0] >> 10) & 0x1F) / 31.0f;
                    current.color[3] = 1.0f;
                    break;
                case Op::DIF_AMB:
                    current.color[0] = (args[0] & 0x1F) / 31.0f;
                    current.color[1] = ((args[0] >> 5) & 0x1F) / 31.0f;
                    current.color[2] = ((args[0] >> 10) & 0x1F) / 31.0f;
                    current.color[3] = 0.0f;
                    break;
                case Op::NORMAL:
                    current.normal[0] = signExtend(args[0], 10) / 512.0f;
                    current.normal[1] = signExtend(args[0] >> 10, 10) / 512.0f;
                    current.normal[2] = signExtend(args[0] >> 20, 10) / 512.0f;
                    break;
                case Op::TEXCOORD:
                    if (texWidth > 0 && texHeight > 0) {
                        current.uv[0] = signExtend(args[0], 16) / 16.0f / texWidth;
                        current.uv[1] = signExtend(args[0] >> 16, 16) / 16.0f / texHeight;
                    }
                    break;
                case Op::VTX_16:
                    current.pos[0] = fxToFloat(signExtend(args[0], 16));
                    current.pos[1] = fxToFloat(signExtend(args[0] >> 16, 16));
                    current.pos[2] = fxToFloat(signExtend(args[1], 16));
                    vertex();
                    break;
                case Op::VTX_10:
                    current.pos[0] = signExtend(args[0], 10) / 64.0f;
                    current.pos[1] = signExtend(args[0] >> 10, 10) / 64.0f;
                    current.pos[2] = signExtend(args[0] >> 20, 10) / 64.0f;
                    vertex();
                    break;
                case Op::VTX_XY:
                    current.pos[0] = fxToFloat(signExtend(args[0], 16));
                    current.pos[1] = fxToFloat(signExtend(args[0] >> 16, 16));
                    vertex();
                    break;
                case Op::VTX_XZ:
                    current.pos[0] = fxToFloat(signExtend(args[0], 16));
                    current.pos[2] = fxToFloat(signExtend(args[0] >> 16, 16));
                    vertex();
                    break;
                case Op::VTX_YZ:
                    current.pos[1] = fxToFloat(signExtend(args[0], 16));
                    current.pos[2] = fxToFloat(signExtend(args[0] >> 16, 16));
                    vertex();
                    break;
                case Op::VTX_DIFF:
                    current.pos[0] += fxToFloat(signExtend(args[0], 10));
                    current.pos[1] += fxToFloat(signExtend(args[0] >> 10, 10));
                    current.pos[2] += fxToFloat(signExtend(args[0] >> 20, 10));
                    vertex();
                    break;
                case Op::MTX_RESTORE:
                    // Room nodes keep matrix 0, as Renderer.DoDlist does.
                    if (!m_isRoom) {
                        current.matrixId = args[0];
                    }
                    break;
                case Op::NOP:
                    break;
                }
            }
        }
        range.vertexCount = static_cast<uint32_t>(m_vertices.size()) - range.firstVertex;
        m_dlistRanges[mesh.dlistId] = range;
    }
}

Image Model::decodeTexture(int textureId, int paletteId, int recolorIndex) const
{
    const Recolor& recolor = m_recolors.at(static_cast<size_t>(recolorIndex));
    const std::vector<uint8_t>& m_textureBytes = *recolor.texelBytes;
    const auto& m_palettes = recolor.palettes;
    const raw::Texture& texture = recolor.textures.at(textureId);
    const auto format = static_cast<TextureFormat>(texture.format);
    Image image;
    image.width = texture.width;
    image.height = texture.height;
    const size_t pixelCount = static_cast<size_t>(texture.width) * texture.height;
    image.rgba.reserve(pixelCount);

    if (format == TextureFormat::DirectRgb) {
        for (size_t i = 0; i < pixelCount; i++) {
            const auto color = readAt<uint16_t>(m_textureBytes, texture.imageOffset + i * 2);
            const uint8_t alpha = (color & 0x8000) ? 255 : 0;
            image.rgba.push_back(packColor(color, alpha));
            image.opaque &= alpha == 255;
        }
        return image;
    }

    int entriesPerByte = 1;
    if (format == TextureFormat::Palette2Bit) {
        entriesPerByte = 4;
    } else if (format == TextureFormat::Palette4Bit) {
        entriesPerByte = 2;
    }
    const int bitsPerEntry = 8 / entriesPerByte;
    const std::vector<uint16_t>* palette
        = paletteId >= 0 && paletteId < static_cast<int>(m_palettes.size()) ? &m_palettes[paletteId] : nullptr;

    for (size_t byteIndex = 0; byteIndex < pixelCount / entriesPerByte; byteIndex++) {
        const uint8_t entry = readAt<uint8_t>(m_textureBytes, texture.imageOffset + byteIndex);
        for (int e = 0; e < entriesPerByte; e++) {
            uint32_t index = static_cast<uint32_t>(entry) >> (e * bitsPerEntry);
            uint8_t alpha = 255;
            switch (format) {
            case TextureFormat::Palette2Bit:
                index &= 0x3;
                break;
            case TextureFormat::Palette4Bit:
                index &= 0xF;
                break;
            case TextureFormat::PaletteA5I3:
                index &= 0x7;
                alpha = static_cast<uint8_t>(std::lround((entry >> 3) / 31.0f * 255.0f));
                break;
            case TextureFormat::PaletteA3I5:
                index &= 0x1F;
                alpha = static_cast<uint8_t>(std::lround((entry >> 5) / 7.0f * 255.0f));
                break;
            default:
                break;
            }
            const bool paletted = format == TextureFormat::Palette2Bit || format == TextureFormat::Palette4Bit
                || format == TextureFormat::Palette8Bit;
            if (paletted && texture.opaque == 0 && index == 0) {
                alpha = 0;
            }
            const uint16_t color = palette != nullptr && index < palette->size() ? (*palette)[index] : 0x7FFF;
            image.rgba.push_back(packColor(color, alpha));
            image.opaque &= alpha == 255;
        }
    }
    return image;
}

void Model::filterNodes(int layerMask)
{
    for (Node& node : m_nodes) {
        node.enabled = true;
        if (node.name.empty() || node.name[0] != '_') {
            continue;
        }
        uint32_t flags = 0;
        // Four characters at a time, not a substring search: "_ml_s010blocks"
        // must not match "_s01". See Model.FilterNodes.
        for (size_t i = 0; node.name.size() >= i + 4; i += 4) {
            const std::string chunk = node.name.substr(i, 4);
            if (chunk[0] == '_' && chunk[1] == 's') {
                const std::string digits = chunk.substr(2);
                if (std::isdigit(static_cast<unsigned char>(digits[0])) && std::isdigit(static_cast<unsigned char>(digits[1]))) {
                    const int id = std::stoi(digits) & 31;
                    flags = (flags & 0xC03F) | ((((flags << 18) >> 24) | (1u << id)) << 6);
                }
            } else if (chunk == "_ml0") {
                flags |= NodeLayer::MultiplayerLod0;
            } else if (chunk == "_ml1") {
                flags |= NodeLayer::MultiplayerLod1;
            } else if (chunk == "_mpu") {
                flags |= NodeLayer::MultiplayerU;
            } else if (chunk == "_ctf") {
                flags |= NodeLayer::CaptureTheFlag;
            }
        }
        if ((flags & static_cast<uint32_t>(layerMask)) == 0) {
            node.enabled = false;
        }
    }
}

} // namespace fp
