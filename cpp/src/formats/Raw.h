#pragma once

#include <cstdint>

// On-disk layouts, byte for byte. Ported from src/MphRead/Formats/RawFormats.cs.
namespace fp::raw {

struct Vector3Fx {
    int32_t x, y, z;
};

struct ColorRgb {
    uint8_t r, g, b;
};

struct Header {
    uint32_t scaleFactor;
    int32_t scaleBase;
    uint32_t primitiveCount;
    uint32_t vertexCount;
    uint32_t materialOffset;
    uint32_t dlistOffset;
    uint32_t nodeOffset;
    uint16_t nodeWeightCount;
    uint8_t flags;
    uint8_t padding1F;
    uint32_t nodeWeightOffset;
    uint32_t meshOffset;
    uint16_t textureCount;
    uint16_t padding2A;
    uint32_t textureOffset;
    uint16_t paletteCount;
    uint16_t padding32;
    uint32_t paletteOffset;
    uint32_t nodePosCounts;
    uint32_t nodePosScales;
    uint32_t nodeInitialPosition;
    uint32_t nodePosition;
    uint16_t materialCount;
    uint16_t nodeCount;
    uint32_t textureMatrixOffset;
    uint32_t nodeAnimationOffset;
    uint32_t textureCoordinateAnimations;
    uint32_t materialAnimations;
    uint32_t textureAnimations;
    uint16_t meshCount;
    uint16_t textureMatrixCount;
};

struct Node {
    char name[64];
    int16_t parentId;
    int16_t childId;
    int16_t nextId;
    uint16_t padding46;
    uint32_t enabled;
    uint16_t meshCount;
    uint16_t meshId;
    Vector3Fx scale;
    int16_t angleX;
    int16_t angleY;
    int16_t angleZ;
    uint16_t padding62;
    Vector3Fx position;
    int32_t boundingRadius;
    Vector3Fx minBounds;
    Vector3Fx maxBounds;
    uint8_t billboardMode;
    uint8_t padding8D;
    uint16_t padding8E;
    int32_t transform[12];
    uint32_t beforeTransform;
    uint32_t afterTransform;
    uint32_t unused[10];
};

struct Mesh {
    uint16_t materialId;
    uint16_t dlistId;
};

struct DisplayList {
    uint32_t offset;
    uint32_t size;
    Vector3Fx minBounds;
    Vector3Fx maxBounds;
};

struct Material {
    char name[64];
    uint8_t lighting;
    uint8_t culling;
    uint8_t alpha;
    uint8_t wireframe;
    int16_t paletteId;
    int16_t textureId;
    uint8_t xRepeat;
    uint8_t yRepeat;
    ColorRgb diffuse;
    ColorRgb ambient;
    ColorRgb specular;
    uint8_t padding53;
    uint32_t polygonMode;
    uint8_t renderMode;
    uint8_t animationFlags;
    uint16_t padding5A;
    uint32_t texgenMode;
    uint16_t texcoordAnimationId;
    uint16_t padding62;
    uint32_t matrixId;
    int32_t scaleS;
    int32_t scaleT;
    uint16_t rotateZ;
    uint16_t padding72;
    int32_t translateS;
    int32_t translateT;
    uint16_t materialAnimationId;
    uint16_t textureAnimationId;
    uint8_t packedRepeatMode;
    uint8_t padding81;
    uint16_t padding82;
};

struct Texture {
    uint8_t format;
    uint8_t padding1;
    uint16_t width;
    uint16_t height;
    uint16_t padding6;
    uint32_t imageOffset;
    uint32_t imageSize;
    uint32_t unusedOffset;
    uint32_t unusedCount;
    uint32_t vramOffset;
    uint32_t opaque;
    uint32_t skipVram;
    uint8_t packedSize;
    uint8_t nativeTextureFormat;
    uint16_t objectRef;
};

struct Palette {
    uint32_t offset;
    uint32_t size;
    uint32_t vramOffset;
    uint32_t objectRef;
};

static_assert(sizeof(Header) == 100);
static_assert(sizeof(Node) == 240);
static_assert(sizeof(Mesh) == 4);
static_assert(sizeof(DisplayList) == 32);
static_assert(sizeof(Material) == 132);
static_assert(sizeof(Texture) == 40);
static_assert(sizeof(Palette) == 16);

} // namespace fp::raw
