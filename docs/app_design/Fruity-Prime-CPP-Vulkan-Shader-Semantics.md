# Vulkan scene shader semantics

Phase 16 production sources are generated from the desktop `Shaders.cpp`
main, RTT/composite, cel and shift programs. OpenGL remains the behavioral
reference. Shader generation changes declarations and API built-ins while
preserving the calculation bodies. Backdrop/UI rendering is a later phase.

| Required behavior | Source and preserved implementation |
|---|---|
| Lighting | Main vertex `light_calc`, two lights, diffuse/ambient/specular/emission, DIF_AMB vertex-color branch |
| Fog | Main fragment depth reconstruction and fog blend, existing enable/distance/color constants |
| Texgen | Main vertex modes 0–3, texture matrix and node/view transforms |
| Matrix stack | Main vertex matrix index from texcoord.z, clamped to 0–31, stack and billboard inverse transform |
| Palette override | Main fragment use_pal_override and pal_override_color branches |
| Flat color | Main fragment use_flat and flat_color branch |
| Alpha test | Main fragment existing 8-bit quantized comparison and discard |
| Material alpha | Main fragment mat_alpha calculation |
| Material mode | Main fragment mat_mode branches and toon_table |
| Cel | Main cel bands and cel outline depth sampling, depth quantum, probe and ink calculations |
| Shift | Shift fragment shift_table, shift_idx, shift_fac and lerp_fac |
| Whiteout | Shift fragment white_table and white_fac branches |
| Fade | Composite fragment fade_color and alpha |

## Production bindings and packing

Each program has descriptor set 0: binding 0 is its std140 uniform block.
Texture declarations become separate sampled-image/sampler binding pairs.
The generator emits `bindings.json`; the embedding tool emits matching C++
uniform offsets, sizes/counts and texture binding indices next to SPIR-V words.
That manifest, rather than the older four-set draft in VulkanShaderInterface,
is the production contract. Layouts used in diagnostics are built from it.
CPU runtime packing into those offsets remains to be connected to the scene sink.

Scalars/bools are four bytes, vec3 alignment is sixteen bytes with twelve data
bytes, mat4 is four columns, and uniform arrays use sixteen-byte-aligned strides.
Matrix memory must preserve the existing untransposed OpenGL uniform upload
convention. Pipeline alphaTest must agree with the packed alpha_test uniform;
native Vulkan pipeline state does not perform alpha discard.

## Coordinates

Generated vertex stages remap clip Z as `(z + w) / 2`, translating OpenGL
`[-w,w]` to Vulkan `[0,w]` while preserving window depth and fog/depth decoding.
Offscreen rendering is intended to keep GL numerical row order: positive
viewport height, no shader Y negation, and no generic texture-coordinate flip.
Thus uploaded texture rows, rendered target rows and existing gl_FragCoord
calculations share the GL row convention. The final presentation pass must
explicitly convert that convention to the displayed swapchain orientation.
Front-face/culling and the final presentation orientation require image tests
when drawing is introduced; compilation/pipeline creation does not prove them.

## Evidence boundaries

All eight modules compile and validate; four pipelines create/bind successfully
in local diagnostics. Cache checks compare native handles for identical,
equivalent replacement and changed-state descriptions. Shader execution and
image equivalence remain separate gates. No rendered parity is claimed yet.
