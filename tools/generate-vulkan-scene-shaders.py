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


# main_fast2, the cpp-port renderer's layout (performance mode, meshes in the
# global geometry buffer): what the fragment stage reads of a material goes
# in push constants with the draw's record and matrix indices; what only the
# vertex stage reads (lighting, texgen, billboard, the DS's inherited vertex
# colour and normal) is a per-draw record in a storage buffer, the matrix
# stack another; frame, light and fog stay uniform blocks, and the toon table
# (fragment, 512 bytes, the same for every draw) is a fifth.
FAST2_UBOS = ('Frame', 'Light', 'Fog', 'Toon')
FAST2_TOON = ('toon_table', 'cel_bands')
STD430 = {"bool": (4, 4), "int": (4, 4), "uint": (4, 4), "float": (4, 4),
          "vec3": (16, 12), "vec4": (16, 16), "mat4": (16, 64)}


def std430_layout(fields):
    """[(type, name, count)] -> [(type, name, count, offset)], size."""
    out, offset, align_max = [], 0, 4
    for kind, name, count in fields:
        align, size = STD430[kind]
        if count:
            size = ((size + align - 1) // align * align) * count
        offset = (offset + align - 1) // align * align
        out.append((kind, name, count, offset))
        offset += size
        align_max = max(align_max, align)
    return out, (offset + align_max - 1) // align_max * align_max


def fast2(blocks, textures, bodies):
    vert, frag = bodies
    used = lambda body, name: re.search(r'\b' + re.escape(name) + r'\b', body) is not None
    material = blocks['Material']
    small = next(block for block in blocks.values() if block['small'])
    toon = [m for m in material['members'] if m['name'] in FAST2_TOON]
    push_fields = [(m['type'], m['name'], 0) for m in small['members']]
    record_fields = [('mat4', m['name'], 0) for m in blocks['Draw']['members'] if m['name'] != 'mtx_stack']
    sources = {m['name']: m for block in blocks.values() for m in block['members']}
    for m in material['members']:
        if m['name'] in FAST2_TOON:
            continue
        if m['count']:
            raise ValueError('main_fast2 has no place for material array ' + m['name'])
        if used(frag, m['name']):
            push_fields.append((m['type'], m['name'], 0))
        elif used(vert, m['name']):
            record_fields.append((m['type'], m['name'], 0))
    push_fields += [('int', 'fast_record', 0), ('int', 'fast_matrices', 0)]
    record_fields += [('vec4', 'fast_inherited_color', 0), ('vec3', 'fast_inherited_normal', 0), ('uint', 'fast_flags', 0)]
    # Big members first keeps the std430 struct dense.
    order = {'mat4': 0, 'vec4': 1, 'vec3': 2}
    record_fields.sort(key=lambda f: order.get(f[0], 3))
    push, push_size = std430_layout(push_fields)
    record, record_size = std430_layout(record_fields)
    if push_size > 128:
        raise ValueError(f'main_fast2 push constants are {push_size} bytes, over the portable 128')
    decl = lambda kind, name, count: f"{kind} {name}{'[%d]' % count if count else ''}"
    out = []
    for binding, semantic in enumerate(FAST2_UBOS):
        if semantic == 'Toon':
            members = toon
        else:
            members = blocks[semantic]['members']
        lines = '\n'.join(f"layout(offset={m['offset'] - (toon[0]['offset'] if semantic == 'Toon' else 0)}) "
                          f"{decl(m['type'], m['name'], m['count'])};" for m in members)
        out.append(f"layout(std140,set=0,binding={binding}) uniform Scene{semantic} {{\n{lines}\n}};\n")
    out.append('struct FastRecord {\n' + ''.join(f"    {decl(k, n, c)};\n" for k, n, c, _ in record) + '};\n')
    out.append(f"layout(std430,set=0,binding={len(FAST2_UBOS)}) readonly buffer FastRecords {{ FastRecord fast_records[]; }};\n")
    out.append(f"layout(std430,set=0,binding={len(FAST2_UBOS) + 1}) readonly buffer FastMatrices {{ mat4 fast_matrix_stack[]; }};\n")
    out.append('layout(std430,push_constant) uniform FastPush {\n'
               + ''.join(f"layout(offset={o}) {decl(k, n, c)};\n" for k, n, c, o in push) + '};\n')
    defines = ''.join(f"#define {n} (fast_records[fast_record].{n})\n" for k, n, c, o in record
                      if not n.startswith('fast_'))
    prefix = ''.join(out) + defines + '\n'.join(textures) + '\n'
    def tables():
        semantic_of = {m['name']: block['semantic'] for block in blocks.values() for m in block['members']}
        def entry(name, dest):
            m = sources[name]
            return dict(name=name, block=semantic_of[name], offset=m['offset'], size=m['size'], dest=dest)
        return dict(
            push_size=push_size, record_size=record_size,
            push=[entry(n, o) for k, n, c, o in push if not n.startswith('fast_')],
            record=[entry(n, o) for k, n, c, o in record if not n.startswith('fast_')],
            fields={n: o for k, n, c, o in push + record if n.startswith('fast_')},
            toon=dict(offset=toon[0]['offset'], size=toon[-1]['offset'] + toon[-1]['size'] - toon[0]['offset']))
    return prefix, tables()


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
        bodies_for_fast = [declaration.sub('', body) for body in bodies]
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
                fast2_prefix, fast2_tables = fast2(blocks, textures, bodies_for_fast)
                fast2_body = body
                if stage == 'vert':
                    fast2_body = fast2_body.replace('in vec4 a_color;', 'in vec4 fast_in_color;')
                    fast2_body = fast2_body.replace('in vec3 a_normal;', 'in vec3 fast_in_normal;')
                    fast2_prefix += ('#define a_color ((fast_records[fast_record].fast_flags & 1u) != 0u'
                                     ' ? fast_records[fast_record].fast_inherited_color : fast_in_color)\n'
                                     '#define a_normal ((fast_records[fast_record].fast_flags & 2u) != 0u'
                                     ' ? fast_records[fast_record].fast_inherited_normal : fast_in_normal)\n')
                fast2_body = fast2_body.replace('mtx_stack[', 'fast_matrix_stack[fast_matrices + ')
                if stage == 'frag':
                    fast2_prefix += 'layout(location=0) out vec4 fragment_color;\n'
                fast2_body = fast2_body.replace('#version 450', '#version 450\n' + fast2_prefix, 1)
                (output / f"main_fast2.{stage}").write_text(fast2_body, encoding="utf-8", newline="\n")
                (output / "main_fast2.json").write_text(json.dumps(fast2_tables, indent=2) + '\n', encoding='utf-8')
                fast = fast_prefix(blocks, textures)
                if stage == 'frag':
                    fast += 'layout(location=0) out vec4 fragment_color;\n'
                fast_body = body.replace('#version 450', '#version 450\n' + fast, 1)
                (output / f"main_fast.{stage}").write_text(fast_body, encoding="utf-8", newline="\n")
            if program == 'composite':
                # composite_fast (performance mode's HUD): the Hud block as
                # push constants, the textures in set 0, nothing else moved.
                hud = blocks['Hud']
                hud_layout = f"std140,set={hud['group']},binding={hud['binding']}"
                fast = prefix.replace(f"layout({hud_layout}) uniform", "layout(std430,push_constant) uniform", 1)
                fast = re.sub(r'layout\(set=\d+,binding=', 'layout(set=0,binding=', fast)
                if len(blocks) != 1 or hud['size'] > 128:
                    raise ValueError('composite_fast expects the Hud block alone, within 128 bytes')
                (output / f"composite_fast.{stage}").write_text(body.replace('#version 450', '#version 450\n' + fast, 1),
                                                                encoding="utf-8", newline="\n")
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
