"""Generate Vulkan GLSL from the frozen desktop shader bodies.

The generated binding manifest is the production uniform/descriptor contract.
Compilation is deliberately separate, so CMake can own the glslc commands.

The main program is also written as main_fast: the same body over the same
std140 blocks, all in one set of dynamic uniform buffers, so a draw binds
its blocks' records in a per-frame ring by offset instead of descriptor sets
written for each draw (performance mode, VulkanCommandList::DrawSceneFast).
"""
import argparse
import json
import re
from scene_shader_abi import read_contract, read_programs, UNIFORM
from pathlib import Path


# The main program's blocks in the order main_fast binds them: set 0,
# bindings 0..4, each a dynamic uniform buffer into the slot's record ring.
FAST_RECORDS = ('Frame', 'Light', 'Fog', 'Material', 'Draw')


def fast_prefix(blocks, textures):
    """main's blocks, all in one set of dynamic uniform buffers.

    The declarations and offsets are main's own, so a record is a block's
    bytes as the constant sink wrote them, and Intel and Mali still push
    the blocks into registers as they do for main.
    """
    out = []
    for binding, semantic in enumerate(FAST_RECORDS):
        block = blocks[semantic]
        declarations = '\n'.join(f"layout(offset={m['offset']}) {m['type']} {m['name']}{'[%d]' % m['count'] if m['count'] else ''};"
                                  for m in block['members'])
        out.append(f"layout(std140,set=0,binding={binding}) uniform Scene{semantic} {{\n{declarations}\n}};\n")
    for block in blocks.values():
        if block['small']:
            declarations = '\n'.join(f"layout(offset={m['offset']}) {m['type']} {m['name']};" for m in block['members'])
            out.append(f"layout(std140,push_constant) uniform Scene{block['semantic']} {{\n{declarations}\n}};\n")
        elif block['semantic'] not in FAST_RECORDS:
            raise ValueError('main_fast has no record for block ' + block['semantic'])
    return ''.join(out) + '\n'.join(textures) + '\n'


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
                fast_body = body.replace('#version 450', '#version 450\n' + fast, 1)
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
