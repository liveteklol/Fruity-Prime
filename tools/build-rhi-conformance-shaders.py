"""Generate and embed the shared GPU conformance fixture from one shader source."""
import argparse
import re
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--phase', choices=('generate', 'embed'), required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
if args.phase == 'generate':
    shaders = dict(re.findall(r'(Vertex|Fragment) = R"glsl\((.*?)\)glsl";',
                             args.source.read_text(encoding='utf-8'), re.S))
    for name, stage in (('Vertex', 'vert'), ('Fragment', 'frag')):
        (args.directory / f'conformance.{stage}').write_text(shaders[name], encoding='utf-8', newline='\n')
else:
    lines = ['#pragma once', '#include <array>', '#include <cstdint>',
             'namespace MphRead::NativeRuntime::Rhi::TestingShaderAssets {']
    for stage in ('vert', 'frag'):
        code = (args.directory / f'conformance.{stage}.spv').read_bytes()
        if len(code) < 20 or len(code) % 4:
            raise ValueError('Invalid SPIR-V length')
        words = struct.unpack(f'<{len(code)//4}I', code)
        if words[0] != 0x07230203:
            raise ValueError('Invalid SPIR-V magic')
        lines.append(f'inline constexpr std::array<std::uint32_t,{len(words)}> {stage}{{{{')
        for start in range(0, len(words), 8):
            lines.append(','.join(f'0x{word:08x}U' for word in words[start:start+8]) + ',')
        lines.append('}};')
    lines.append('}')
    args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
