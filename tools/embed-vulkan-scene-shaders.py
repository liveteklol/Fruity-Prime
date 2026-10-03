"""Embed SPIR-V words and the generated binding contract without runtime files."""
import argparse
import json
import struct
from pathlib import Path
from scene_shader_abi import read_contract, read_programs, ROOT, GLSL_TYPES
from reflect_scene_spirv import verify


def embed(directory, destination):
    manifest = json.loads((directory / 'bindings.json').read_text(encoding='utf-8'))
    logical = read_contract()
    read_programs(ROOT / 'src/MphRead.Native/Shaders.cpp', logical)
    if manifest['version'] != 2 or manifest['schema_digest'] != logical['digest'] or manifest['group_count'] != len(logical['groups']):
        raise ValueError('Stale logical shader contract')
    if set(manifest['programs']) != set(read_programs(ROOT / 'src/MphRead.Native/Shaders.cpp', logical)):
        raise ValueError('Missing production program')
    lines = ['#pragma once', '#include <array>', '#include <cstdint>',
             '#include <string_view>', '#include "NativeRuntime/Rhi/Vulkan/VulkanSceneUniforms.hpp"',
             'namespace MphRead::NativeRuntime::Rhi::Vulkan::Generated {',
             'using UniformBlock = VulkanSceneUniforms::BlockDesc;',
             'using UniformMember = VulkanSceneUniforms::MemberDesc;',
             'struct TextureBinding { std::string_view name; std::uint32_t group, image, sampler, unit; };']
    for program, contract in manifest['programs'].items():
        for block in contract['blocks']:
            b = logical['bindings'][block['semantic']]
            if (block['group'], block['binding']) != (b['group'], b['binding']):
                raise ValueError('Logical block binding drift')
        wanted = {name for p, name in logical['constants'] if p == program}
        wanted |= {name for p, name in logical['textures'] if p == program}
        if len(contract['members']) != len(wanted) or {m['name'] for m in contract['members']} != wanted:
            raise ValueError('Logical member identity drift')
        for member in contract['members']:
            if 'offset' in member:
                rule = logical['constants'][program, member['name']]
                if any(member[k] != rule[k] for k in ('type', 'count', 'block')):
                    raise ValueError('Logical constant drift')
            elif any(member[k] != v for k, v in logical['textures'][program, member['name']].items()):
                raise ValueError('Logical texture drift')
        for stage in ('vert', 'frag'):
            code = (directory / f'{program}.{stage}.spv').read_bytes()
            if len(code) % 4 or len(code) < 20:
                raise ValueError('Malformed SPIR-V length')
            verify(code, contract, stage)
            words = struct.unpack(f'<{len(code)//4}I', code)
            if words[0] != 0x07230203:
                raise ValueError('Malformed SPIR-V magic')
            lines.append(f'inline constexpr std::array<std::uint32_t, {len(words)}> {program}_{stage}{{{{')
            for start in range(0, len(words), 8):
                lines.append(','.join(f'0x{word:08x}U' for word in words[start:start+8]) + ',')
            lines.append('}};')
        blocks = contract['blocks']
        lines.append(f'inline constexpr std::array<UniformBlock,{len(blocks)}> {program}_blocks{{{{')
        for block in blocks:
            lines.append('{"%s",%d,%d,%d,%s},' % (block['semantic'], block['group'], block['binding'], block['size'], str(block['small']).lower()))
        lines.append('}};')
        members = [entry for entry in contract['members'] if 'offset' in entry]
        textures = [entry for entry in contract['members'] if 'image_binding' in entry]
        lines.append(f'inline constexpr std::array<UniformMember,{len(members)}> {program}_uniforms{{{{')
        for entry in members:
            index = next(i for i, b in enumerate(blocks) if b['semantic'] == entry['block'])
            kind = next(k for k, v in GLSL_TYPES.items() if v == entry['type'])
            lines.append('{"%s",%d,SceneShaderAbi::ValueType::%s,%d,%d,%d},' % (entry['name'], index, kind, entry['offset'], entry['size'], entry['count']))
        lines.append('}};')
        lines.append(f'inline constexpr std::array<TextureBinding,{len(textures)}> {program}_textures{{{{')
        for entry in textures:
            lines.append('{"%s",%d,%d,%d,%d},' % (entry['name'], entry['group'], entry['image_binding'], entry['sampler_binding'], entry['unit']))
        lines.append('}};')
    lines.append('}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    embed(args.directory, args.output)
