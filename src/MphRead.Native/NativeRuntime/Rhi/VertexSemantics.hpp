#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// The one definition of what a vertex carries and where each backend reads
// it. Every shader declares its inputs by these names, and every submission
// path sends them to these locations -- nothing else in the renderer spells
// out an attribute number.
//
// The matrix-stack index is not a separate stream: it travels in the third
// component of TexCoord (a DS texcoord is two-dimensional, and the stack is
// 32 entries), exactly as it did when it rode in gl_MultiTexCoord0.z. That is
// part of the contract, not an accident of packing, so the Vulkan interface
// reads it from the same place.
namespace MphRead::NativeRuntime::Rhi
{
    enum class VertexSemantic : std::uint8_t
    {
        Position,   // vec4, xyz from the stream, w = 1
        Normal,     // vec3
        Color,      // vec4; alpha 0 marks a DIF_AMB vertex colour
        TexCoord,   // vec3: st, then the matrix-stack index
        TexCoord1,  // vec2: the backdrop's second (noise) coordinate
        Count
    };

    inline constexpr std::size_t VertexSemanticCount
        = static_cast<std::size_t>(VertexSemantic::Count);

    // The GLSL input name for each semantic. Desktop programs are bound by
    // name before they are linked (GL::LinkProgram), so a shader that
    // declares an input by any other name reads nothing.
    inline constexpr std::array<std::string_view, VertexSemanticCount> VertexSemanticNames{
        "a_position",
        "a_normal",
        "a_color",
        "a_texcoord",
        "a_texcoord1",
    };

    // Desktop inputs are explicit generic attributes, in the same order as
    // Vulkan. No conventional arrays or NV_vertex_program alias slots are used.
    inline constexpr std::array<std::uint32_t, VertexSemanticCount> OpenGlDesktopLocations{
        0U, 1U, 2U, 3U, 4U,
    };

    // OpenGL ES 3.0 (the Android head). Its shaders are written with
    // `layout(location = N)` in EsShaders.cpp, which must agree with this
    // table; location 4 there is the emulated "colour array enabled" flag,
    // which belongs to the immediate-mode emulation rather than to the vertex.
    // No ES shader reads TexCoord1 (the moving backdrop is desktop-only), so
    // its entry only has to stay clear of the others.
    inline constexpr std::array<std::uint32_t, VertexSemanticCount> OpenGlEsLocations{
        0U, 2U, 1U, 3U, 5U,
    };

    // Vulkan: the plan's canonical order. A Vulkan pipeline has no aliasing
    // table and no fixed function to share slots with.
    inline constexpr std::array<std::uint32_t, VertexSemanticCount> VulkanLocations{
        0U, 1U, 2U, 3U, 4U,
    };

    [[nodiscard]] constexpr std::uint32_t Location(
        const std::array<std::uint32_t, VertexSemanticCount>& table, VertexSemantic semantic)
    {
        return table[static_cast<std::size_t>(semantic)];
    }

    [[nodiscard]] constexpr std::string_view Name(VertexSemantic semantic)
    {
        return VertexSemanticNames[static_cast<std::size_t>(semantic)];
    }
}
