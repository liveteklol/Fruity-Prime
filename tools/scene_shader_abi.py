"""Read the same logical scene contract used by the C++ ABI header.

This module knows logical types/groups; byte packing belongs to each generator.
"""
import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI_ROOT = ROOT / 'src/MphRead.Native/NativeRuntime/Rhi'
PROGRAMS = {
    'main': ('VertexShader', 'FragmentShader'),
    'composite': ('RttVertexShader', 'RttFragmentShader'),
    'cel': ('RttVertexShader', 'CelFragmentShader'),
    'shift': ('RttVertexShader', 'ShiftFragmentShader'),
    'backdrop': ('BackdropVertexShader', 'BackdropFragmentShader'),
}
GLSL_TYPES = {'Bool': 'bool', 'Int': 'int', 'Float': 'float', 'Vec3': 'vec3', 'Vec4': 'vec4', 'Mat4': 'mat4'}
UNIFORM = re.compile(r'^uniform (\w+)(?:\[(\d+)\])? (\w+);$', re.M)


def read_contract(abi_root=ABI_ROOT):
    definition = (abi_root / 'SceneShaderAbi.def').read_text(encoding='utf-8')
    header = (abi_root / 'SceneShaderAbi.hpp').read_text(encoding='utf-8')
    vertex = (abi_root / 'VertexSemantics.hpp').read_text(encoding='utf-8')
    groups = re.search(r'enum class Group[^\{]*\{([^}]+)\}', header)[1]
    groups = [x.strip() for x in groups.split(',')]
    if groups.pop() != 'Count' or len(set(groups)) != len(groups):
        raise ValueError('Invalid logical groups')
    bindings, constants, textures = {}, {}, {}
    for line in definition.splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = re.fullmatch(r'RHI_SCENE_(BINDING|CONSTANT|TEXTURE)\((.*)\)', line)
        if not match:
            raise ValueError(f'Unrecognized ABI definition: {line}')
        kind, args = match[1], next(csv.reader([match[2]], skipinitialspace=True))
        if len(args) != 5:
            raise ValueError(f'Invalid ABI arguments: {line}')
        if kind == 'BINDING':
            name, group, index, resource, semantic = args
            if name in bindings or group not in groups or int(index) < 0:
                raise ValueError(f'Invalid binding {name}')
            bindings[name] = dict(group=groups.index(group), binding=int(index), type=resource, semantic=semantic)
        else:
            program, name, first, second, last = args
            if program not in PROGRAMS or (program, name) in constants or (program, name) in textures:
                raise ValueError(f'Invalid program member {program}.{name}')
            if kind == 'CONSTANT':
                if first not in GLSL_TYPES or last not in bindings or int(second) < 0:
                    raise ValueError(f'Invalid constant {program}.{name}')
                if bindings[last]['type'] not in ('UniformBuffer', 'SmallConstants'):
                    raise ValueError(f'Constant in non-buffer binding {last}')
                constants[program, name] = dict(type=GLSL_TYPES[first], count=int(second), block=last)
            else:
                image, sampler, unit = bindings[first], bindings[second], int(last)
                if image['type'] != 'SampledTexture' or sampler['type'] != 'Sampler' or image['group'] != sampler['group'] or not 0 <= unit < 4:
                    raise ValueError(f'Invalid texture {program}.{name}')
                textures[program, name] = dict(group=image['group'], image_binding=image['binding'],
                    sampler_binding=sampler['binding'], unit=unit, image_semantic=first, sampler_semantic=second)
    coordinates = [(v['group'], v['binding']) for v in bindings.values()]
    if len(set(coordinates)) != len(coordinates) or len({v['semantic'] for v in bindings.values()}) != len(bindings):
        raise ValueError('Duplicate logical binding')
    names = re.search(r'VertexSemanticNames\{(.*?)\}', vertex, re.S)[1]
    names = re.findall(r'"(\w+)"', names)
    locations = {}
    for backend in ('OpenGlDesktop', 'Vulkan'):
        table = re.search(backend + r'Locations\{(.*?)\}', vertex, re.S)[1]
        locations[backend] = [int(x) for x in re.findall(r'(\d+)U', table)]
    if locations['OpenGlDesktop'] != locations['Vulkan'] or len(names) != len(locations['Vulkan']):
        raise ValueError('Desktop vertex locations drift')
    digest = hashlib.sha256((definition + header + vertex).replace('\r\n', '\n').encode()).hexdigest()
    return dict(groups=groups, bindings=bindings, constants=constants, textures=textures,
                locations=dict(zip(names, locations['Vulkan'])), digest=digest)


def read_programs(source, contract):
    shaders = dict(re.findall(r'const std::string Shaders::(\w+) = R"shader\((.*?)\)shader";',
                             source.read_text(encoding='utf-8'), re.S))
    result = {}
    for program, names in PROGRAMS.items():
        bodies = [shaders[name] for name in names]
        uniforms = {}
        for body in bodies:
            for kind, count, name in UNIFORM.findall(body):
                value = (kind, int(count or 0))
                if name in uniforms and uniforms[name] != value:
                    raise ValueError(f'Conflicting uniform {program}.{name}')
                uniforms[name] = value
        expected = {name: (item['type'], item['count']) for (p, name), item in contract['constants'].items() if p == program}
        expected.update({name: ('sampler2D', 0) for p, name in contract['textures'] if p == program})
        if uniforms != expected:
            raise ValueError(f'{program}: shader declarations differ from logical ABI: actual={uniforms}, expected={expected}')
        inputs = re.findall(r'^attribute (\w+) (\w+);$', bodies[0], re.M)
        if any(name not in contract['locations'] for kind, name in inputs):
            raise ValueError(f'{program}: unknown vertex semantic')
        result[program] = dict(bodies=bodies, uniforms=uniforms,
            inputs=[dict(name=name, type=kind, location=contract['locations'][name]) for kind, name in inputs])
    return result
