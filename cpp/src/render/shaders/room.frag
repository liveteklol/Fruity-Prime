#version 450

// Port of Shaders.FragmentShader, minus toon, cel shading and overrides.

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform Frame {
    mat4 proj;
    mat4 view;
    mat4 billboardSphere;   // inverse view rotation
    mat4 billboardCylinder; // inverse view yaw
    vec4 light1Vec;
    vec4 light1Col;
    vec4 light2Vec;
    vec4 light2Col;
    vec4 fogColor;
    vec4 fogParams;
} frame;

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Draw {
    mat4 texMtx;
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    ivec4 flags;
} draw;

layout(location = 0) out vec4 outColor;

const int PASS_OPAQUE = 0;
const int PASS_DECAL = 1;
const int PASS_TRANSLUCENT = 2;

void main()
{
    float matAlpha = draw.diffuse.w;
    int mode = int(draw.specular.w);
    vec4 col;
    if ((draw.flags.x & 1) != 0) {
        vec4 t = texture(tex, inUv);
        if (mode == 1) {
            col = vec4(mix(inColor.rgb, t.rgb, t.a), matAlpha);
        } else {
            col = vec4(inColor.rgb * t.rgb, matAlpha * t.a);
        }
    } else {
        col = vec4(inColor.rgb, matAlpha);
    }

    // The GL renderer's alpha test: opaque pass keeps alpha == 1, the
    // translucent pass keeps alpha < 1. Compared at 8-bit precision, and the
    // vertex alpha (always 1) is left out: interpolating a constant 1.0 can
    // land on 0.99999994, which dropped whole pixel columns.
    int pass = draw.flags.z;
    bool opaque = col.a >= 254.5 / 255.0;
    if (pass == PASS_OPAQUE && !opaque) {
        discard;
    }
    if (pass == PASS_TRANSLUCENT && opaque) {
        discard;
    }

    if (frame.fogParams.x > 0.5) {
        float depth = gl_FragCoord.z;
        float density = 0.0;
        if (depth >= frame.fogParams.z) {
            density = 1.0;
        } else if (depth > frame.fogParams.y) {
            density = (depth - frame.fogParams.y) / (frame.fogParams.z - frame.fogParams.y) * 124.0 / 128.0;
        }
        col.rgb = mix(col.rgb, frame.fogColor.rgb, density);
    }
    outColor = col;
}
