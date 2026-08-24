#if ANDROID
using System;
using System.Security.Cryptography;
using System.Text;

namespace MphRead.Mods.Render
{
    /// <summary>
    /// The five shaders of <see cref="Shaders"/>, written for OpenGL ES 3.0.
    ///
    /// The desktop ones are GLSL 1.20 and read their vertex data out of the
    /// fixed-function pipeline -- <c>gl_Vertex</c>, <c>gl_Color</c>,
    /// <c>gl_Normal</c>, <c>gl_MultiTexCoord0</c> -- which is what ties the
    /// desktop build to a compatibility profile. ES has no such profile and no
    /// such builtins, so these declare the same four things as real attributes
    /// at fixed locations and are otherwise the same program, expression for
    /// expression. <see cref="GlEs"/> feeds those attributes and substitutes
    /// these sources as the shaders are compiled; nothing in the engine knows.
    ///
    /// Two things here are not in the desktop originals:
    ///
    /// - <c>imm_color</c> and <c>a_color_set</c>. In fixed-function GL a vertex
    ///   with no colour of its own takes the *current* colour, which the engine
    ///   sets per render item (<c>DoMaterial</c> calls <c>GL.Color3</c> before
    ///   <c>CallList</c>) and a display list therefore reads at execution time,
    ///   not at compile time. The buffers <see cref="GlEs"/> bakes carry a flag
    ///   saying whether each vertex had its own colour; the ones that did not
    ///   take <c>imm_color</c>, which is the current colour at draw time.
    /// - <c>alpha_test</c>. ES has no <c>glAlphaFunc</c>. The engine uses
    ///   exactly two comparisons -- equal to 1 and less than 1 -- so the
    ///   fragment shaders discard on those instead.
    ///
    /// If the desktop shaders change, these do not follow on their own, and a
    /// silent divergence would be a rendering bug with no message anywhere. So
    /// each one is checked against the hash of the source it was written from,
    /// and a mismatch throws with the name of the shader that moved.
    /// </summary>
    internal static class EsShaders
    {
        public static string VertexShader { get; } = @"#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec3 a_texcoord;
layout(location = 4) in float a_color_set;

uniform vec4 imm_color;

uniform bool use_light;
uniform bool use_texture;
uniform bool show_colors;
uniform bool fog_enable;
uniform vec3 light1vec;
uniform vec3 light1col;
uniform vec3 light2vec;
uniform vec3 light2col;
uniform vec3 diffuse;
uniform vec3 ambient;
uniform vec3 specular;
uniform vec3 emission;
uniform vec4 fog_color;
uniform float far_plane;
uniform mat4 proj_mtx;
uniform mat4 view_mtx;
uniform mat4 view_inv_mtx;
uniform mat4 tex_mtx;
uniform int texgen_mode;
uniform mat4 mtx_stack[32];

out vec2 texcoord;
out vec4 color;

vec3 light_calc(vec3 light_vec, vec3 light_col, vec3 normal_vec, vec3 dif_col, vec3 amb_col, vec3 spe_col)
{
    vec3 sight_vec = vec3(0.0, 0.0, -1.0);
    float dif_factor = max(0.0, -dot(light_vec, normal_vec));
    vec3 half_vec = (light_vec + sight_vec) / 2.0;
    float spe_factor = max(0.0, dot(-half_vec, normal_vec));
    spe_factor = spe_factor * spe_factor;
    vec3 spe_out = spe_col * light_col * spe_factor;
    vec3 dif_out = dif_col * light_col * dif_factor;
    vec3 amb_out = amb_col * light_col;
    return spe_out + dif_out + amb_out;
}

void main()
{
    vec4 vtx_in_color = a_color_set > 0.5 ? a_color : imm_color;
    mat4 stack_mtx = mtx_stack[int(a_texcoord.z)];
    // view_inv_mtx is set for billboard transforms
    mat4 model_mtx = stack_mtx * view_inv_mtx;
    gl_Position = proj_mtx * view_mtx * model_mtx * a_position;
    vec4 vtx_color = show_colors ? vtx_in_color : vec4(1.0);
    vec3 normal = normalize(mat3(model_mtx) * a_normal);
    if (use_light) {
        vec3 dif_current = diffuse;
        vec3 amb_current = ambient;
        if (vtx_in_color.a == 0.0) {
            // see comment on DIF_AMB
            dif_current = vtx_color.rgb;
            amb_current = vec3(0.0, 0.0, 0.0);
        }
        vec3 col1 = light_calc(light1vec, light1col, normal, dif_current, amb_current, specular);
        vec3 col2 = light_calc(light2vec, light2col, normal, dif_current, amb_current, specular);
        color = vec4(min((col1 + col2 + emission), vec3(1.0, 1.0, 1.0)), 1.0);
    }
    else {
        // alpha will only be less than 1.0 here if DIF_AMB is used but lighting is disabled
        color = vec4(vtx_color.rgb, 1.0);
    }
    texcoord = vec2(0.0, 0.0);
    if (use_texture) {
        // texgen mode: 0 - none, 1 - texcoord, 2 - normal, 3 - vertex
        if (texgen_mode == 0 || texgen_mode == 1) {
            texcoord = vec2(tex_mtx * vec4(a_texcoord.xy, 0.0, 1.0));
        }
        else if (texgen_mode == 2 || texgen_mode == 3) {
            mat4 tex_mul = tex_mtx;
            if (texgen_mode == 2) {
                // texgen uses the node transform, which doesn't have billboard transform applied
                tex_mul = transpose(tex_mtx * (use_light ? view_mtx : mat4(1.0)) * mat4(mat3(stack_mtx)));
            }
            mat2x4 texgen_mtx = mat2x4(
                vec4(tex_mul[0][0], tex_mul[0][1], tex_mul[0][2], a_texcoord.x),
                vec4(tex_mul[1][0], tex_mul[1][1], tex_mul[1][2], a_texcoord.y)
            );
            if (texgen_mode == 2) {
                texcoord = vec4(a_normal, 1.0) * texgen_mtx;
            }
            else {
                texcoord = vec4(a_position.xyz, 1.0) * texgen_mtx;
            }
        }
    }
}
";

        public static string FragmentShader { get; } = @"#version 300 es
precision highp float;
precision highp int;

uniform bool use_texture;
uniform bool fog_enable;
uniform vec4 fog_color;
uniform float fog_min;
uniform float fog_max;
uniform sampler2D tex;
uniform bool use_override;
uniform vec4 override_color;
uniform bool use_pal_override;
uniform vec4 pal_override_color;
uniform float mat_alpha;
uniform int mat_mode;
uniform vec3 toon_table[32];
// 0 - off, 1 - pass only alpha == 1, 2 - pass only alpha < 1
uniform int alpha_test;

in vec2 texcoord;
in vec4 color;

out vec4 frag_color;

vec4 toon_color(vec4 vtx_color)
{
    return vec4(toon_table[int(vtx_color.r * 31.0)], vtx_color.a);
}

void main()
{
    // mat_mode: 0 - modulate, 1 - decal, 2 - toon
    vec4 col;
    if (use_texture) {
        vec4 texcolor = use_pal_override ? vec4(pal_override_color.xyz, texture(tex, texcoord).w) : texture(tex, texcoord);
        if (mat_mode == 1) {
            col = vec4(
                (texcolor.r * texcolor.a + color.r * (1.0 - texcolor.a)),
                (texcolor.g * texcolor.a + color.g * (1.0 - texcolor.a)),
                (texcolor.b * texcolor.a + color.b * (1.0 - texcolor.a)),
                mat_alpha * color.a
            );
        }
        else if (mat_mode == 2) {
            vec4 toon = toon_color(color);
            col = vec4(texcolor.rgb * color.r + toon.rgb, mat_alpha * texcolor.a * color.a);
        }
        else {
            col = color * vec4(texcolor.rgb, mat_alpha * texcolor.a);
        }
        if (use_override) {
            col.r = override_color.r;
            col.g = override_color.g;
            col.b = override_color.b;
            col.a *= override_color.a;
        }
    }
    else if (use_override) {
        col = override_color;
    }
    else {
        col = mat_mode == 2 ? toon_color(color) : color;
        col.a *= mat_alpha;
    }
    if (fog_enable) {
        float depth = gl_FragCoord.z;
        float density = 0.0;
        if (depth >= fog_max) {
            density = 1.0;
        }
        else if (depth > fog_min) {
            // MPH fog table has min 0 and max 124
            density = (depth - fog_min) / (fog_max - fog_min) * 124.0 / 128.0;
        }
        col = vec4((col * (1.0 - density) + fog_color * density).xyz, col.a);
    }
    // glAlphaFunc, which ES does not have. The engine only ever asks for
    // Equal 1.0 and Less 1.0, and the test runs on the final colour.
    if (alpha_test == 1 && col.a < 1.0) {
        discard;
    }
    if (alpha_test == 2 && col.a >= 1.0) {
        discard;
    }
    frag_color = col;
}
";

        public static string RttVertexShader { get; } = @"#version 300 es
precision highp float;

layout(location = 0) in vec4 a_position;
layout(location = 3) in vec3 a_texcoord;

out vec2 texcoord;

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    texcoord = a_texcoord.xy;
}
";

        public static string RttFragmentShader { get; } = @"#version 300 es
precision highp float;

uniform float alpha;
uniform bool use_mask;
uniform float view_width;
uniform float view_height;
uniform vec4 fade_color;
uniform sampler2D tex;
uniform sampler2D mask;

in vec2 texcoord;

out vec4 frag_color;

void main()
{
    if (fade_color.a > 0.0) {
        frag_color = fade_color;
    }
    else {
        frag_color = texture(tex, texcoord);
        if (use_mask) {
            float maskY = gl_FragCoord.y + (view_width - view_height) / 2.0;
            vec2 maskTexcoord = vec2(gl_FragCoord.x / view_width, 1.0 - maskY / view_width);
            vec4 maskColor = texture(mask, maskTexcoord);
            if (maskColor.a > 0.0) {
                frag_color.a = 0.0;
            }
        }
        frag_color.a *= alpha;
    }
}
";

        public static string ShiftFragmentShader { get; } = @"#version 300 es
precision highp float;
precision highp int;

uniform float shift_table[64];
uniform int shift_idx;
uniform float shift_fac;
uniform float lerp_fac;
uniform float white_table[192];
uniform float white_fac;
uniform sampler2D tex;

in vec2 texcoord;

out vec4 frag_color;

void main()
{
    int band = int((1.0 - texcoord.y) * 192.0);
    float bandf = float(band);
    int index = int(mod(bandf + float(shift_idx) + mod(bandf, 2.0) * 32.0, 64.0));
    float value1 = shift_table[index];
    float value2 = shift_table[int(mod(float(index) + 1.0, 64.0))];
    float value = mix(value1, value2, lerp_fac) * shift_fac;
    vec2 shifted = vec2(texcoord.x + value, texcoord.y);
    if (shifted.x < 0.0 || shifted.x > 1.0) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else {
        frag_color = texture(tex, shifted);
    }
    if (white_fac != 0.0) {
        float factor = white_table[band];
        if (white_fac < 0.0) {
            frag_color = vec4(factor, factor, factor, 1.0);
        }
        else {
            factor *= white_fac;
            if (factor >= 0.0) {
                float r = frag_color.r + (1.0 - frag_color.r) * factor;
                float g = frag_color.g + (1.0 - frag_color.g) * factor;
                float b = frag_color.b + (1.0 - frag_color.b) * factor;
                frag_color = vec4(r, g, b, 1.0);
            }
            else {
                factor = -factor;
                float r = frag_color.r - frag_color.r * factor;
                float g = frag_color.g - frag_color.g * factor;
                float b = frag_color.b - frag_color.b * factor;
                frag_color = vec4(r, g, b, 1.0);
            }
        }
    }
}
";

        /// <summary>
        /// The ES source for one of the desktop sources, or null if it is not
        /// one this file knows -- in which case <see cref="GlEs"/> passes the
        /// original through and lets the driver reject it, which is a clearer
        /// failure than silently compiling something else.
        /// </summary>
        public static string? Translate(string desktopSource)
        {
            if (ReferenceEquals(desktopSource, Shaders.VertexShader))
            {
                return VertexShader;
            }
            if (ReferenceEquals(desktopSource, Shaders.FragmentShader))
            {
                return FragmentShader;
            }
            if (ReferenceEquals(desktopSource, Shaders.RttVertexShader))
            {
                return RttVertexShader;
            }
            if (ReferenceEquals(desktopSource, Shaders.RttFragmentShader))
            {
                return RttFragmentShader;
            }
            if (ReferenceEquals(desktopSource, Shaders.ShiftFragmentShader))
            {
                return ShiftFragmentShader;
            }
            return null;
        }

        private static bool _checked = false;

        /// <summary>
        /// Throw if a desktop shader has been edited since its ES counterpart
        /// was written from it. Called once, before the first compile.
        /// </summary>
        public static void CheckInSync()
        {
            if (_checked)
            {
                return;
            }
            _checked = true;
            Check("VertexShader", Shaders.VertexShader,
                "4cf1422bddaa3ece44c9cfbf6dab1ede192ee8c3f4fbed362e7da5eebfdfc428");
            Check("FragmentShader", Shaders.FragmentShader,
                "d4de970d9214d1463b542a8f8f8609d3537732a7f9eb88fe566e01e1b3915927");
            Check("RttVertexShader", Shaders.RttVertexShader,
                "af070f447840bf1fc51d6bba88a339fab067a4e3a01e460351a2549ca9107f4f");
            Check("RttFragmentShader", Shaders.RttFragmentShader,
                "021b5992926cb3a8c714fb943b0c85e091cf3cd76d2c487950ca0fb03d27c56e");
            Check("ShiftFragmentShader", Shaders.ShiftFragmentShader,
                "2b2511d5506ad9a25d64005b7b9e452f56b550410f96c753a6072a743b3162fa");
        }

        private static void Check(string name, string source, string expected)
        {
            // Normalised the same way the hashes were taken, so a checkout with
            // CRLF line endings is not reported as a change.
            byte[] bytes = Encoding.UTF8.GetBytes(source.Replace("\r\n", "\n"));
            string actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
            if (actual != expected)
            {
                throw new ProgramException(
                    $"Shaders.{name} has changed since the OpenGL ES version of it was written "
                    + $"(expected {expected}, found {actual}). Update EsShaders.{name} to match, "
                    + "then update the hash here.");
            }
        }
    }
}
#endif
