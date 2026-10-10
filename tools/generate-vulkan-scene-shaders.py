"""Generate Vulkan GLSL from the frozen desktop shader bodies.

The generated binding manifest is the production uniform/descriptor contract.
Compilation is deliberately separate, so CMake can own the glslc commands.

The main program is also written as main_fast: the same body over the same
std140 blocks, read from per-frame storage-buffer arrays indexed by push
constants instead of descriptor sets rewritten for each draw (the renderer's
performance-mode path, VulkanCommandList::DrawSceneFast).
"""
import argparse
import json
import re
from scene_shader_abi import read_contract, read_programs, UNIFORM
from pathlib import Path


# The main program's blocks in the order main_fast binds them (set 0,
# bindings 0..4) and where each one's record index is in the push constants.
FAST_RECORDS = (('Frame', 'fast_index0.x'), ('Light', 'fast_index0.y'), ('Fog', 'fast_index0.z'),
                ('Material', 'fast_index0.w'), ('Draw', 'fast_index1.x'))
FAST_MATRIX_BASE = 'fast_index1.y'


def fast_prefix(blocks, textures):
    """Declarations and defines that turn main's uniforms into record reads."""
    small = next(block for block in blocks.values() if block['small'])
    structs, buffers, defines = [], [], []
    for binding, (semantic, index) in enumerate(FAST_RECORDS):
        block = blocks[semantic]
        members = [m for m in block['members'] if m['name'] != 'mtx_stack']
        if semantic == 'Draw' and block['members'][-1]['name'] != 'mtx_stack':
            raise ValueError('main_fast copies the Draw block up to mtx_stack: it must come last')
        fields = ''.join(f"    {m['type']} {m['name']}{'[%d]' % m['count'] if m['count'] else ''};\n" for m in members)
        structs.append(f"struct FastScene{semantic} {{\n{fields}}};\n")
        buffers.append(f"layout(std140,set=0,binding={binding}) readonly buffer FastScene{semantic}Records "
                       f"{{ FastScene{semantic} fast_{semantic.lower()}[]; }};\n")
        defines += [f"#define {m['name']} (fast_{semantic.lower()}[{index}].{m['name']})" for m in members]
    buffers.append(f"layout(std430,set=0,binding={len(FAST_RECORDS)}) readonly buffer FastSceneMatrices "
                   f"{{ mat4 fast_matrices[]; }};\n")
    smalls = ''.join(f"layout(offset={m['offset']}) {m['type']} {m['name']};\n" for m in small['members'])
    push = (f"layout(std140,push_constant) uniform SceneFastDraw {{\n{smalls}"
            f"layout(offset={small['size']}) ivec4 fast_index0;\nlayout(offset={small['size'] + 16}) ivec4 fast_index1;\n}};\n")
    return ''.join(structs) + ''.join(buffers) + push + '\n'.join(defines) + '\n' + '\n'.join(textures) + '\n'


def generate(source, output):
    contract = read_contract()
    programs = read_programs(source, contract)
    declaration = UNIFORM
    locations = contract['locations']
    output.mkdir(parents=True, exist_ok=True)
    manifest = dict(version=2, schema_digest=contract['digest'], group_count=len(contract['groups']), programs={})
    for program, inputs in programs.items():
        bodies = inputs["bodies"]
        # Legacy GL accepts unused unmatched varyings. Vulkan requires every
        # declared fragment input to have a vertex output even with -O0.
        for index, body in enumerate(bodies):
            for kind, name in re.findall(r'^varying (\w+) (\w+);$', body, re.M):
                if len(re.findall(r'\b' + re.escape(name) + r'\b', body)) == 1:
                    body = re.sub(r'^varying ' + kind + ' ' + name + r';$', '', body, flags=re.M)
            bodies[index] = body
        blocks, textures, metadata = {}, [], []
        for name, (kind, count) in inputs['uniforms'].items():
            if kind == 'sampler2D':
                item = contract['textures'][program, name]
                textures += [f"layout(set={item['group']},binding={item['image_binding']}) uniform texture2D {name}_image;",
                             f"layout(set={item['group']},binding={item['sampler_binding']}) uniform sampler {name}_sampler;",
                             f"#define {name} sampler2D({name}_image, {name}_sampler)"]
                metadata.append(dict(name=name, **item))
                continue
            logical = contract['constants'][program, name]
            identity = logical['block']
            binding = contract['bindings'][identity]
            block = blocks.setdefault(identity, dict(semantic=identity, group=binding['group'], binding=binding['binding'],
                size=0, small=binding['type'] == 'SmallConstants', members=[], declarations=[]))
            alignment, size = {"bool": (4, 4), "int": (4, 4), "float": (4, 4),
                               "vec3": (16, 12), "vec4": (16, 16), "mat4": (16, 64)}[kind]
            if count:
                alignment = 16
                size = ((size + 15) // 16 * 16) * count
            offset = (block['size'] + alignment - 1) // alignment * alignment
            suffix = f"[{count}]" if count else ""
            block['declarations'].append(f"layout(offset={offset}) {kind} {name}{suffix};")
            member = dict(name=name, type=kind, count=count, offset=offset, size=size, block=identity)
            block['members'].append(member)
            metadata.append(member)
            block['size'] = offset + size
        prefix_blocks = []
        for block in blocks.values():
            block['size'] = (block['size'] + 15) // 16 * 16
            declarations = '\n'.join(block.pop('declarations'))
            layout = 'std140,push_constant' if block['small'] else f"std140,set={block['group']},binding={block['binding']}"
            if block['small'] and block['size'] > 128:
                raise ValueError('Small constants exceed the portable 128-byte budget')
            prefix_blocks.append(f"layout({layout}) uniform Scene{block['semantic']} {{\n{declarations}\n}};\n")
        block = '\n'.join(prefix_blocks)
        varyings = {}
        for body in bodies:
            for kind, name in re.findall(r'^varying (\w+) (\w+);$', body, re.M):
                if name not in varyings:
                    varyings[name] = len(varyings)
        for stage, body in zip(("vert", "frag"), bodies):
            body = re.sub(r'^#version .*$', '#version 450', body, flags=re.M)
            body = declaration.sub('', body)
            body = re.sub(r'^attribute (\w+) (\w+);$',
                          lambda m: f"layout(location={locations[m[2]]}) in {m[1]} {m[2]};",
                          body, flags=re.M)
            body = re.sub(r'^varying (\w+) (\w+);$',
                          lambda m: f"layout(location={varyings[m[2]]}) {'out' if stage == 'vert' else 'in'} {m[1]} {m[2]};",
                          body, flags=re.M)
            body = body.replace('texture2D(', 'texture(')
            body = body.replace('gl_FragColor', 'fragment_color')
            if stage == 'vert':
                # Preserve GL window-depth values with Vulkan's [0,w] clip Z.
                body = re.sub(r'(gl_Position\s*=\s*[^;]+;)',
                              r'\1\n    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;', body)
            prefix = block + '\n'.join(textures) + '\n'
            if stage == 'frag':
                prefix += 'layout(location=0) out vec4 fragment_color;\n'
            if program == 'main':
                fast = fast_prefix(blocks, textures)
                if stage == 'frag':
                    fast += 'layout(location=0) out vec4 fragment_color;\n'
                fast_body = body.replace('mtx_stack[', f'fast_matrices[{FAST_MATRIX_BASE} + ')
                fast_body = fast_body.replace('#version 450', '#version 450\n' + fast, 1)
                (output / f"main_fast.{stage}").write_text(fast_body, encoding="utf-8", newline="\n")
            body = body.replace('#version 450', '#version 450\n' + prefix, 1)
            (output / f"{program}.{stage}").write_text(body, encoding="utf-8", newline="\n")
        manifest['programs'][program] = dict(blocks=list(blocks.values()), members=metadata, inputs=inputs['inputs'])
    (output / 'bindings.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.source, args.output)
