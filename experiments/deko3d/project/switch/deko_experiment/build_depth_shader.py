"""Adapt the working Vulkan depth-resolve math to deko3d declarations."""
from pathlib import Path
import argparse, hashlib, json, subprocess, re

def generate(source: str) -> str:
    replacements = {
        '#version 450': '#version 460',
        '#extension GL_EXT_samplerless_texture_functions : require': '',
        'layout(set=0,binding=0) uniform texture2D depth_source;':
            'layout(binding=0) uniform sampler2D depth_source;',
        'layout(set=0,binding=1) uniform utexture2D stencil_source;':
            'layout(binding=1) uniform usampler2D stencil_source;',
        'layout(set=0,binding=2,r32f)': 'layout(binding=0,r32f)',
        'layout(set=0,binding=3,rgba8)': 'layout(binding=1,rgba8)',
        'layout(push_constant) uniform Params { ivec2 origin; uvec2 size; };':
            'layout(std140,binding=0) uniform Params { ivec2 source_origin; ivec2 destination_origin; uvec2 size; uint flags; uint padding; };',
        'ivec2 c = origin + ivec2(p);': 'ivec2 c = source_origin + ivec2(p);\n  ivec2 d = destination_origin + ivec2(p);',
        'uint packed = (encode20e4(z)<<8u) | s;':
            'uint depth24 = (flags & 1u) != 0u ? encode20e4(z) : uint(z * 16777215.0 + 0.5);\n  uint packed = (depth24<<8u) | s;',
        'imageStore(depth_dest,c,': 'imageStore(depth_dest,d,',
        'imageStore(words_dest,c,': 'imageStore(words_dest,d,',
    }
    for old, new in replacements.items():
        if source.count(old) != 1:
            raise ValueError(f'Unexpected Vulkan shader source; expected one occurrence of {old!r}')
        source = source.replace(old, new)
    # "packed" is a reserved layout token in uam's OpenGL parser.
    return re.sub(r'\bpacked\b', 'packed_depth_word', source)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    parser.add_argument('--uam', type=Path, default=Path('C:/devkitPro/tools/bin/uam.exe'))
    args = parser.parse_args()
    original = Path(__file__).resolve().parent.parent/'native_experiment/native/shaders/depth_packed_direct.cs.glsl'
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output/'depth_resolve.comp'
    source.write_text(generate(original.read_text(encoding='utf-8')), encoding='utf-8')
    binary = args.output/'depth_resolve.dksh'
    run = subprocess.run([str(args.uam), '-s', 'comp', '-o', str(binary), str(source)], capture_output=True, timeout=90)
    if run.returncode:
        raise RuntimeError((run.stdout+run.stderr).decode('utf-8','replace'))
    data = binary.read_bytes()
    if len(data)<24 or data[:4]!=b'DKSH': raise ValueError('Invalid DKSH output')
    result = dict(source=str(original), source_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
        glsl_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), bytes=len(data),
        dksh_sha256=hashlib.sha256(data).hexdigest(), compiled=True, gpu_executed=False)
    (args.output/'depth-resolve-build.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
if __name__=='__main__': main()
