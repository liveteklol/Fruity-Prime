"""Verify the production shader interface, not general SPIR-V validity.

The compiler owns instruction validity. This verifier independently reads
Khronos SPIR-V type/decorations for the ABI consumed by the native adapter.
"""
import struct


def expect(value, message):
    if not value:
        raise ValueError(message)


def literal_string(words):
    raw = struct.pack(f'<{len(words)}I', *words)
    expect(b'\0' in raw, 'Unterminated SPIR-V string')
    return raw.split(b'\0', 1)[0].decode('utf-8')


class Module:
    def __init__(self, data):
        expect(len(data) >= 20 and len(data) % 4 == 0, 'Malformed SPIR-V length')
        words = struct.unpack(f'<{len(data)//4}I', data)
        expect(words[0] == 0x07230203 and words[3] > 0 and words[4] == 0, 'Malformed SPIR-V header')
        self.types, self.constants, self.variables = {}, {}, {}
        self.names, self.members, self.decorations, self.member_decorations = {}, {}, {}, {}
        self.entries = []
        offset = 5
        while offset < len(words):
            size, op = words[offset] >> 16, words[offset] & 65535
            expect(size > 0 and offset + size <= len(words), 'Malformed SPIR-V instruction')
            args = words[offset+1:offset+size]
            if op in (20, 21, 22, 23, 24, 25, 26, 28, 30, 32):
                expect(len(args) >= 1 and args[0] not in self.types, 'Malformed SPIR-V type')
                self.types[args[0]] = (op, args[1:])
            elif op == 43:
                expect(len(args) >= 3, 'Malformed SPIR-V constant')
                self.constants[args[1]] = args[2:]
            elif op == 59:
                expect(len(args) >= 3 and args[1] not in self.variables, 'Malformed SPIR-V variable')
                self.variables[args[1]] = (args[0], args[2])
            elif op == 5:
                expect(len(args) >= 2, 'Malformed SPIR-V name')
                self.names[args[0]] = literal_string(args[1:])
            elif op == 6:
                expect(len(args) >= 3, 'Malformed SPIR-V member name')
                self.members[args[0], args[1]] = literal_string(args[2:])
            elif op in (71, 72):
                prefix = 2 if op == 71 else 3
                expect(len(args) >= prefix, 'Malformed SPIR-V decoration')
                target = self.decorations if op == 71 else self.member_decorations
                key = tuple(args[:prefix])
                expect(key not in target, 'Duplicate SPIR-V decoration')
                target[key] = args[prefix:]
            elif op == 15:
                expect(len(args) >= 3, 'Malformed SPIR-V entry')
                self.entries.append((args[0], literal_string(args[2:])))
            offset += size

    def value_type(self, type_id):
        op, args = self.types[type_id]
        if op == 20:
            return 'bool'
        if op == 21:
            expect(args[0] == 32, 'ABI integer is not 32 bits')
            return 'int' if args[1] else 'uint'
        if op == 22:
            expect(args == (32,), 'ABI float is not 32 bits')
            return 'float'
        if op == 23:
            expect(self.value_type(args[0]) == 'float' and args[1] in (2, 3, 4), 'ABI vector type drift')
            return f'vec{args[1]}'
        if op == 24:
            expect(self.value_type(args[0]) == 'vec4' and args[1] == 4, 'ABI matrix type drift')
            return 'mat4'
        raise ValueError(f'Unexpected ABI value type {op}')

    def member(self, struct_id, index, type_id):
        decor = self.member_decorations
        expect((struct_id, index, 35) in decor, 'Missing member offset')
        offset = decor[struct_id, index, 35][0]
        op, args = self.types[type_id]
        count = 0
        if op == 28:
            expect(self.constants[args[1]] and len(self.constants[args[1]]) == 1, 'Invalid array count')
            count = self.constants[args[1]][0]
            expect(count > 0, 'Empty ABI array')
            expect((type_id, 6) in self.decorations, 'Missing array stride')
            stride = self.decorations[type_id, 6][0]
            type_id = args[0]
        kind = self.value_type(type_id)
        if kind == 'mat4':
            expect(decor.get((struct_id, index, 7)) == (16,) and (struct_id, index, 5) in decor
                   and (struct_id, index, 4) not in decor, 'Matrix stride/major order drift')
        width = {'bool': 4, 'int': 4, 'uint': 4, 'float': 4, 'vec2': 8, 'vec3': 12, 'vec4': 16, 'mat4': 64}[kind]
        if count:
            expect(stride == (width + 15)//16*16, 'Array stride drift')
            width = stride * count
        return dict(name=self.members[struct_id, index], type=kind, count=count, offset=offset, size=width)


def verify(data, contract, stage):
    flattened = [m for b in contract['blocks'] for m in b['members']]
    constants = [m for m in contract['members'] if 'offset' in m]
    expect(len({m['name'] for m in flattened}) == len(flattened)
           and sorted(flattened, key=lambda x: x['name']) == sorted(constants, key=lambda x: x['name']),
           'Packing member table differs from reflected block table')
    module = Module(data)
    expect(module.entries == [(0 if stage == 'vert' else 4, 'main')], 'Shader entry/stage drift')
    expected = {('small', 0) if x.get('small') else (x['group'], x['binding']): ('block', x) for x in contract['blocks']}
    expect(sum(x.get('small', False) for x in contract['blocks']) <= 1, 'Duplicate small constant block')
    for x in contract['members']:
        if 'image_binding' in x:
            for kind, field in (('image', 'image_binding'), ('sampler', 'sampler_binding')):
                key = (x['group'], x[field])
                expect(key not in expected, 'Duplicate manifest descriptor')
                expected[key] = (kind, x)
    seen, inputs = set(), {}
    for variable, (pointer, storage) in module.variables.items():
        pointer_op, pointer_args = module.types[pointer]
        expect(pointer_op == 32 and pointer_args[0] == storage, 'Invalid variable pointer')
        target = pointer_args[1]
        expect(storage != 12, 'Unspecified storage buffer interface')
        if storage in (0, 2, 9): # UniformConstant / Uniform / PushConstant
            if storage == 9:
                key = ('small', 0)
                expect((variable, 34) not in module.decorations and (variable, 33) not in module.decorations,
                       'Small constants must not use descriptor bindings')
            else:
                expect((variable, 34) in module.decorations and (variable, 33) in module.decorations, 'Missing descriptor set/binding')
                key = (module.decorations[variable, 34][0], module.decorations[variable, 33][0])
            expect(key in expected and key not in seen, f'Unexpected/duplicate descriptor {key}')
            seen.add(key)
            kind, item = expected[key]
            op, args = module.types[target]
            if kind == 'block':
                expect(storage == (9 if item.get('small') else 2) and op == 30 and (target, 2) in module.decorations, 'Uniform block type drift')
                actual = [module.member(target, i, t) for i, t in enumerate(args)]
                wanted = [{k: v for k, v in m.items() if k != 'block'} for m in item['members']]
                expect(len(actual) == len(wanted), 'Block member count drift')
                for a, b in zip(actual, wanted):
                    if b['type'] == 'bool' and a['type'] == 'uint':
                        a['type'] = 'bool' # GLSL bool storage in SPIR-V UBOs
                    expect(a == b, f"Uniform member drift: {a} != {b}")
                size = (max(m['offset'] + m['size'] for m in actual)+15)//16*16
                expect(size == item['size'], 'Uniform block size drift')
                expect(not item.get('small') or size <= 128, 'Small constant budget exceeded')
            elif kind == 'image':
                expect(storage == 0 and op == 25 and args[1:] == (1, 0, 0, 0, 1, 0)
                       and module.value_type(args[0]) == 'float', 'Sampled 2D image type drift')
            else:
                expect(storage == 0 and op == 26, 'Sampler type drift')
        elif storage == 1 and (variable, 30) in module.decorations and stage == 'vert':
            location = module.decorations[variable, 30][0]
            expect(location not in inputs, 'Duplicate vertex input location')
            inputs[location] = dict(name=module.names[variable], type=module.value_type(target), location=location)
        elif storage == 1 and stage == 'vert':
            raise ValueError('Unspecified vertex input interface')
    expect(seen == set(expected), f'Missing shader descriptors: {set(expected)-seen}')
    if stage == 'vert':
        expect(sorted(inputs.values(), key=lambda x: x['location']) == sorted(contract['inputs'], key=lambda x: x['location']),
               'Vertex input semantic/type/location drift')
    return module
