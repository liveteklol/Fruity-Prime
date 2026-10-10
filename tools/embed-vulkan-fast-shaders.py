"""Embed the main_fast SPIR-V (performance mode's scene program).

Its blocks are main's own (same names, members and offsets), so the binding
contract is main's: generate-vulkan-scene-shaders.py writes both from it.
"""
import argparse
import struct
from pathlib import Path


def embed(directory, destination):
    lines = ['#pragma once', '#include <array>', '#include <cstdint>',
             'namespace MphRead::NativeRuntime::Rhi::Vulkan::Generated {']
    for stage in ('vert', 'frag'):
        code = (directory / f'main_fast.{stage}.spv').read_bytes()
        if len(code) % 4 or len(code) < 20:
            raise ValueError('Malformed SPIR-V length')
        words = struct.unpack(f'<{len(code)//4}I', code)
        if words[0] != 0x07230203:
            raise ValueError('Malformed SPIR-V magic')
        lines.append(f'inline constexpr std::array<std::uint32_t, {len(words)}> main_fast_{stage}{{{{')
        for start in range(0, len(words), 8):
            lines.append(','.join(f'0x{word:08x}U' for word in words[start:start+8]) + ',')
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
