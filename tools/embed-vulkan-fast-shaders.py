"""Embed the main_fast SPIR-V (performance mode's scene program).

Its blocks are main's own (same names, members and offsets), so the binding
contract is main's: generate-vulkan-scene-shaders.py writes both from it.
"""
import argparse
import json
import struct
from pathlib import Path


def embed(directory, destination):
    lines = ['#pragma once', '#include <array>', '#include <cstdint>', '#include <string_view>',
             'namespace MphRead::NativeRuntime::Rhi::Vulkan::Generated {',
             '// One member of main\'s blocks and where main_fast2 wants it.',
             'struct FastCopy { std::string_view block; std::uint32_t offset, size, dest; };']
    for program in ('main_fast', 'main_fast2'):
        for stage in ('vert', 'frag'):
            code = (directory / f'{program}.{stage}.spv').read_bytes()
            if len(code) % 4 or len(code) < 20:
                raise ValueError('Malformed SPIR-V length')
            words = struct.unpack(f'<{len(code)//4}I', code)
            if words[0] != 0x07230203:
                raise ValueError('Malformed SPIR-V magic')
            lines.append(f'inline constexpr std::array<std::uint32_t, {len(words)}> {program}_{stage}{{{{')
            for start in range(0, len(words), 8):
                lines.append(','.join(f'0x{word:08x}U' for word in words[start:start+8]) + ',')
            lines.append('}};')
    tables = json.loads((directory / 'main_fast2.json').read_text(encoding='utf-8'))
    for kind in ('push', 'record'):
        entries = tables[kind]
        lines.append(f'inline constexpr std::array<FastCopy, {len(entries)}> main_fast2_{kind}{{{{')
        for e in entries:
            lines.append('{"%s",%d,%d,%d},' % (e['block'], e['offset'], e['size'], e['dest']))
        lines.append('}};')
    lines.append(f"inline constexpr std::uint32_t main_fast2_push_size = {tables['push_size']};")
    lines.append(f"inline constexpr std::uint32_t main_fast2_record_size = {tables['record_size']};")
    for name, offset in tables['fields'].items():
        lines.append(f"inline constexpr std::uint32_t main_fast2_{name} = {offset};")
    lines.append(f"inline constexpr std::uint32_t main_fast2_toon_offset = {tables['toon']['offset']};")
    lines.append(f"inline constexpr std::uint32_t main_fast2_toon_size = {tables['toon']['size']};")
    lines.append('}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    embed(args.directory, args.output)
