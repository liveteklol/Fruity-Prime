#pragma once

#include "formats/Model.h"

#include <cstdint>
#include <vector>

namespace fp {

// 2D triangles drawn over the scene (the HUD): window pixels, origin top left,
// in order, one texture a batch.
struct HudVertex {
    float x, y, u, v;
    uint32_t color; // RGBA8, R in the low byte; multiplies the texture
};

struct HudDrawList {
    struct Batch {
        int texture;
        bool linear;
        uint32_t firstVertex, vertexCount;
    };
    std::vector<HudVertex> vertices;
    std::vector<Batch> batches;
    void clear()
    {
        vertices.clear();
        batches.clear();
    }
};

class OverlaySource {
public:
    virtual ~OverlaySource() = default;
    // Called once per frame, before the frame is recorded.
    virtual const HudDrawList& overlay(int width, int height) = 0;
    // Texture ids index this list; images are never changed once added.
    virtual const std::vector<Image>& overlayTextures() const = 0;
};

} // namespace fp
