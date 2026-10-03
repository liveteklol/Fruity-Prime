"""Negative ABI tests use actual production SPIR-V; no GPU/driver needed."""
import argparse
import copy
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from scene_shader_abi import ABI_ROOT, ROOT, read_contract, read_programs
from reflect_scene_spirv import Module, verify

PARSER = argparse.ArgumentParser()
PARSER.add_argument('--directory', type=Path, required=True)
ARGS = PARSER.parse_args()


def mutate(data, opcode, predicate, operand, value):
    words = list(struct.unpack(f'<{len(data)//4}I', data))
    index = 5
    while index < len(words):
        size, op = words[index] >> 16, words[index] & 65535
        if op == opcode and predicate(words[index+1:index+size]):
            words[index+1+operand] = value(words[index+1+operand])
            return struct.pack(f'<{len(words)}I', *words)
        index += size
    raise AssertionError('Mutation target missing')


class SceneAbi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ARGS.directory / 'bindings.json').read_text())
        cls.program = cls.manifest['programs']['main']
        cls.binary = (ARGS.directory / 'main.vert.spv').read_bytes()

    def test_all_production_stages(self):
        for program, contract in self.manifest['programs'].items():
            for stage in ('vert', 'frag'):
                with self.subTest(program=program, stage=stage):
                    verify((ARGS.directory / f'{program}.{stage}.spv').read_bytes(), contract, stage)

    def test_structural_rejections(self):
        malformed = [self.binary[:-1], self.binary[:16], bytes(4) + self.binary[4:], self.binary[:20]+bytes(4)+self.binary[24:]]
        for binary in malformed:
            with self.assertRaises(ValueError):
                verify(binary, self.program, 'vert')

    def test_descriptor_and_member_drift(self):
        for opcode, predicate, operand in [(71, lambda a: a[1] == 33, 2),
                (71, lambda a: a[1] == 34, 2), (72, lambda a: a[2] == 35, 3),
                (71, lambda a: a[1] == 6, 2), (72, lambda a: a[2] == 7, 3),
                (71, lambda a: a[1] == 30, 2)]:
            with self.subTest(opcode=opcode, operand=operand), self.assertRaises(ValueError):
                verify(mutate(self.binary, opcode, predicate, operand, lambda x: x+1), self.program, 'vert')

    def test_type_array_entry_drift(self):
        array_count_id = next(args[1] for op, args in Module(self.binary).types.values() if op == 28)
        mutations = [(43, lambda a: a[1] == array_count_id, 2, lambda x: x+1),
                     (22, lambda a: True, 1, lambda x: 16),
                     (25, lambda a: True, 2, lambda x: 2),
                     (15, lambda a: True, 0, lambda x: 4)]
        for opcode, predicate, operand, value in mutations:
            with self.subTest(opcode=opcode), self.assertRaises(ValueError):
                verify(mutate(self.binary, opcode, predicate, operand, value), self.program, 'vert')

    def test_manifest_drift(self):
        for field in ('offset', 'size', 'count', 'name', 'type'):
            contract = copy.deepcopy(self.program)
            member = contract['blocks'][0]['members'][0]
            member_name = member['name']
            member[field] = member[field]+1 if isinstance(member[field], int) else 'unexpected'
            next(m for m in contract['members'] if m['name'] == member_name)[field] = member[field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify(self.binary, contract, 'vert')
        contract = copy.deepcopy(self.program); contract['inputs'][0]['location'] += 5
        with self.assertRaises(ValueError):
            verify(self.binary, contract, 'vert')
        contract = copy.deepcopy(self.program)
        next(m for m in contract['members'] if 'offset' in m)['offset'] += 4
        with self.assertRaises(ValueError):
            verify(self.binary, contract, 'vert')
        contract = copy.deepcopy(self.program); contract['blocks'][0]['size'] += 16
        with self.assertRaises(ValueError):
            verify(self.binary, contract, 'vert')

    def test_glsl_logical_drift(self):
        source = (ROOT / 'src/MphRead.Native/Shaders.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='fruity-scene-abi-') as temporary:
            file = Path(temporary) / 'Shaders.cpp'
            for old, new in [('uniform float mat_alpha;', 'uniform int mat_alpha;'),
                    ('uniform float[64] shift_table;', 'uniform float[65] shift_table;'),
                    ('uniform bool use_light;', 'uniform bool use_light;\nuniform float surprise;'),
                    ('attribute vec4 a_position;', 'attribute vec4 unknown_semantic;')]:
                file.write_text(source.replace(old, new), encoding='utf-8')
                with self.subTest(old=old), self.assertRaises(ValueError):
                    read_programs(file, read_contract())

    def test_single_source_and_stale_manifest(self):
        with tempfile.TemporaryDirectory(prefix='fruity-scene-schema-') as temporary:
            root = Path(temporary)
            for name in ('SceneShaderAbi.def', 'SceneShaderAbi.hpp', 'VertexSemantics.hpp'):
                (root / name).write_bytes((ABI_ROOT / name).read_bytes())
            definition = (root / 'SceneShaderAbi.def').read_text()
            (root / 'SceneShaderAbi.def').write_text(definition + '\nRHI_SCENE_BINDING(Duplicate, Frame, 0, UniformBuffer, "duplicate")\n')
            with self.assertRaises(ValueError): read_contract(root)
        # Even matching native data cannot hide a stale logical schema from embedding.
        spec = importlib.util.spec_from_file_location('embed_scene', ROOT / 'tools/embed-vulkan-scene-shaders.py')
        embedder = importlib.util.module_from_spec(spec); spec.loader.exec_module(embedder)
        with tempfile.TemporaryDirectory(prefix='fruity-scene-embed-') as temporary:
            root = Path(temporary); manifest = copy.deepcopy(self.manifest)
            manifest['schema_digest'] = '0'*64
            (root / 'bindings.json').write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): embedder.embed(root, root / 'output.hpp')


if __name__ == '__main__':
    unittest.main(argv=['test-scene-shader-abi.py'])
