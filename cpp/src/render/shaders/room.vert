#version 450

// Port of Shaders.VertexShader. Matrices live in one buffer per frame: a draw
// uses matrices[base + vertex matrix id], the model's node matrix stack.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUv;
layout(location = 4) in uint inMatrixId;

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
    vec4 fogParams; // x: enabled, y: min depth, z: max depth
} frame;

layout(std430, set = 0, binding = 1) readonly buffer Matrices {
    mat4 matrices[];
};

layout(push_constant) uniform Draw {
    mat4 texMtx;
    vec4 diffuse;  // w: material alpha
    vec4 ambient;  // w: use lighting
    vec4 specular; // w: polygon mode
    ivec4 flags;   // x: use texture | own lights' matrix + 1 << 1, y: texgen mode | billboard << 8 | emission (BGR555) << 16, z: pass, w: matrix base
} draw;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

vec3 lightCalc(vec3 lightVec, vec3 lightCol, vec3 normal, vec3 dif, vec3 amb, vec3 spe)
{
    vec3 sightVec = vec3(0.0, 0.0, -1.0);
    float difFactor = max(0.0, -dot(lightVec, normal));
    vec3 halfVec = (lightVec + sightVec) / 2.0;
    float speFactor = max(0.0, dot(-halfVec, normal));
    speFactor = speFactor * speFactor;
    return spe * lightCol * speFactor + dif * lightCol * difFactor + amb * lightCol;
}

void main()
{
    mat4 stackMtx = matrices[draw.flags.w + int(inMatrixId)];
    int billboard = draw.flags.y >> 8;
    mat4 viewInv = billboard == 1 ? frame.billboardSphere : billboard == 2 ? frame.billboardCylinder : mat4(1.0);
    mat4 modelMtx = stackMtx * viewInv;
    gl_Position = frame.proj * frame.view * modelMtx * vec4(inPos, 1.0);

    // a < 0: no COLOR command yet, so the material diffuse the renderer
    // would have set with GL.Color before the list.
    vec4 vtxColor = inColor.a < 0.0 ? vec4(draw.diffuse.rgb, 1.0) : inColor;
    vec3 normal = normalize(mat3(modelMtx) * inNormal);

    if (draw.ambient.w > 0.5) {
        vec3 dif = draw.diffuse.rgb;
        vec3 amb = draw.ambient.rgb;
        if (vtxColor.a == 0.0) {
            dif = vtxColor.rgb;
            amb = vec3(0.0);
        }
        vec3 l1v = frame.light1Vec.xyz, l1c = frame.light1Col.rgb, l2v = frame.light2Vec.xyz, l2c = frame.light2Col.rgb;
        int lights = draw.flags.x >> 1;
        if (lights > 0) {
            mat4 own = matrices[lights - 1];
            l1v = own[0].xyz;
            l1c = own[1].rgb;
            l2v = own[2].xyz;
            l2c = own[3].rgb;
        }
        vec3 c1 = lightCalc(l1v, l1c, normal, dif, amb, draw.specular.rgb);
        vec3 c2 = lightCalc(l2v, l2c, normal, dif, amb, draw.specular.rgb);
        int e = (draw.flags.y >> 16) & 0x7FFF;
        vec3 emission = vec3(float(e & 31), float((e >> 5) & 31), float((e >> 10) & 31)) / 31.0;
        outColor = vec4(min(c1 + c2 + emission, vec3(1.0)), 1.0);
    } else {
        outColor = vec4(vtxColor.rgb, 1.0);
    }

    int texgen = draw.flags.y & 0xFF;
    if ((draw.flags.x & 1) == 0) {
        outUv = vec2(0.0);
    } else if (texgen == 2) {
        mat4 m = transpose(draw.texMtx);
        outUv = vec2(dot(vec4(inNormal, 1.0), vec4(m[0].xyz, inUv.x)),
                     dot(vec4(inNormal, 1.0), vec4(m[1].xyz, inUv.y)));
    } else {
        outUv = vec2(draw.texMtx * vec4(inUv, 0.0, 1.0));
    }
}
