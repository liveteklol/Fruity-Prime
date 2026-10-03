#pragma once
#include <string_view>

namespace MphRead::NativeRuntime::Rhi::TestingShaderAssets
{
    // One logical ABI for the same test on both backends. GL's dense UBO
    // binding slots and combined sampler uniform are backend emission details.
    inline constexpr std::string_view Vertex = R"glsl(#version 450
layout(location=0) in vec2 position;
#ifdef FRUITY_VULKAN
layout(std140,set=2,binding=4) uniform Draw { vec4 offset; } draw;
#else
layout(std140,binding=1) uniform Draw { vec4 offset; } draw;
#endif
void main() { gl_Position = vec4(position + draw.offset.xy, 0.0, 1.0); }
)glsl";
    inline constexpr std::string_view Fragment = R"glsl(#version 450
layout(location=0) out vec4 color;
#ifdef FRUITY_VULKAN
layout(std140,set=0,binding=7) uniform Frame { vec4 tint; } frame;
layout(set=1,binding=3) uniform texture2D image;
layout(set=1,binding=4) uniform texture2D secondImage;
layout(set=1,binding=9) uniform sampler imageSampler;
layout(set=1,binding=10) uniform sampler secondSampler;
#define sampledImage sampler2D(image, imageSampler)
#define secondSampledImage sampler2D(secondImage, secondSampler)
#else
layout(std140,binding=0) uniform Frame { vec4 tint; } frame;
layout(binding=0) uniform sampler2D sampledImage;
layout(binding=1) uniform sampler2D secondSampledImage;
#endif
void main() {
    color = (gl_FragCoord.x < 8.0 ? texture(sampledImage, vec2(0.5))
        : texture(secondSampledImage, vec2(0.5))) * frame.tint;
}
)glsl";
}
