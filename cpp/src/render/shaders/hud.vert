#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform Viewport {
    vec2 size;
} pc;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main()
{
    uv = inUv;
    color = inColor;
    gl_Position = vec4(inPos / pc.size * 2.0 - 1.0, 0.0, 1.0);
}
