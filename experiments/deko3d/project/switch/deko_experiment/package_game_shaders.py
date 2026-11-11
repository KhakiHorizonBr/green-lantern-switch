"""Build the user's local DKSH library and primitive helpers; never publish game shaders."""
from pathlib import Path
import argparse,struct,re,json,subprocess,shutil,concurrent.futures,hashlib

def names_and_bindings(data):
    w=struct.unpack('<%dI'%(len(data)//4),data); names={};bindings={};i=5
    while i<len(w):
        n=w[i]>>16;op=w[i]&65535
        if not n or i+n>len(w):raise ValueError('Invalid SPIR-V')
        if op==5:names[w[i+1]]=struct.pack('<%dI'%(n-2),*w[i+2:i+n]).split(b'\0')[0].decode()
        if op==71 and n==4 and w[i+2]==33:bindings[w[i+1]]=w[i+3]
        i+=n
    return names,bindings

def geometry(kind,count,size,coords,system):
    inp={'point':'points','rect':'triangles','quad':'lines_adjacency'}[kind]
    text='#version 460\nlayout('+inp+') in;\nlayout(triangle_strip,max_vertices=4) out;\n'
    for i in range(count):text+=f'layout(location={i}) in vec4 v{i}[];\nlayout(location={i}) invariant out vec4 o{i};\n'
    if size:text+=f'layout(location={count}) in float diameter[];\n'
    if coords:text+=f'layout(location={count}) invariant out vec2 point_coords;\n'
    if kind=='point':text+=system+'\n'
    text+='void main(){\n'
    if kind=='point':
        text+='vec4 center=gl_in[0].gl_Position;vec2 d=xe_uniform_system_constants.point_constant_diameter;\n'
        if size:text+='if(diameter[0]>=0.0)d=vec2(diameter[0]);\n'
        text+='if(any(lessThanEqual(d,vec2(0))))return;vec2 r=d*xe_uniform_system_constants.point_screen_diameter_to_ndc_radius*center.w;\n'
        text+='for(int n=0;n<4;++n){vec2 uv=vec2(n>>1,n&1);gl_Position=vec4(center.xy+(uv*2.0-1.0)*r,center.zw);\n'
        for i in range(count):text+=f'o{i}=v{i}[0];\n'
        if coords:text+='point_coords=uv;\n'
        text+='EmitVertex();}\n'
    elif kind=='quad':
        text+='for(int n=0;n<4;++n){int i=n^(n>>1);gl_Position=gl_in[i].gl_Position;\n'
        for i in range(count):text+=f'o{i}=v{i}[i];\n'
        if coords:text+='point_coords=vec2(0);\n'
        text+='EmitVertex();}\n'
    else:
        text+='float a=dot(gl_in[1].gl_Position.xy-gl_in[2].gl_Position.xy,gl_in[1].gl_Position.xy-gl_in[2].gl_Position.xy);\n'
        text+='float b=dot(gl_in[2].gl_Position.xy-gl_in[0].gl_Position.xy,gl_in[2].gl_Position.xy-gl_in[0].gl_Position.xy);\n'
        text+='float c=dot(gl_in[0].gl_Position.xy-gl_in[1].gl_Position.xy,gl_in[0].gl_Position.xy-gl_in[1].gl_Position.xy);\n'
        text+='int first=a>b&&a>c?0:b>c?1:2;for(int n=0;n<3;++n){int i=(first+n)%3;gl_Position=gl_in[i].gl_Position;\n'
        for i in range(count):text+=f'o{i}=v{i}[i];\n'
        if coords:text+='point_coords=vec2(0);\n'
        text+='EmitVertex();}\nint i=first,j=(first+1)%3,k=(first+2)%3;gl_Position=gl_in[j].gl_Position+gl_in[k].gl_Position-gl_in[i].gl_Position;\n'
        for i in range(count):text+=f'o{i}=v{i}[j]+v{i}[k]-v{i}[i];\n'
        if coords:text+='point_coords=vec2(0);\n'
        text+='EmitVertex();\n'
    return text+'EndPrimitive();}\n'

def main():
    p=argparse.ArgumentParser();p.add_argument('corpus',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--uam',default='C:/devkitPro/tools/bin/uam.exe');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    refs={}
    for row in map(json.loads,(a.corpus/'interfaces.jsonl').read_text().splitlines()):
        refs.setdefault(row['file'],{})[row['binding']]=row['bytes']
    manifest=[];hashes={};counts=set([0]);jobs=[]
    first=next(a.corpus.glob('vs_*.spv.glsl')).read_text()
    system=re.search(r'layout\(binding = 0, std140\) uniform XeSystemConstants\s*\{.*?\} xe_uniform_system_constants;',first,re.S)[0]
    for shader in sorted(a.corpus.glob('*.spv.dksh')):
        match=re.fullmatch(r'(vs|ps)_([0-9A-F]+)_([0-9A-F]+)\.spv\.dksh',shader.name)
        if not match:continue
        stage,h,mod=match.groups();name=shader.name.removesuffix('.dksh');src=a.corpus/(name+'.adapted.spv')
        names,bindings=names_and_bindings(src.read_bytes());glsl=(a.corpus/(name+'.glsl')).read_text()
        outputs=re.findall(r'layout\(location = (\d+)\).*?out vec4 xe_out_interpolator_',glsl)
        if stage=='vs':counts.add(len(outputs))
        ubos=refs[name];mask=sum(1<<b for b in ubos if b<5)
        textures=[]
        for ident,b in bindings.items():
            tex=re.fullmatch(r'xe_texture(\d+)_(2d|3d|cube)_([us])',names.get(ident,''))
            if tex:
                fetch,dim,sign=tex.groups();samplers=[n for n in names.values() if n.startswith('xe_sampler'+fetch+'_')]
                sampler=samplers[0] if samplers else 'xe_sampler'+fetch+'_fff'
                filters=sampler.split('_')[2] if len(sampler.split('_'))>2 else 'fff'
                # Names are xe_samplerN_fff[_aX].
                filters=sampler.split('_')[2]
                textures.append((b,int(fetch),sign=='s',{'2d':5,'3d':3,'cube':9}[dim],filters))
        file=shader.name;shutil.copy2(shader,a.output/file);hashes[file]=hashlib.sha256(shader.read_bytes()).hexdigest()
        fields=[stage,h,mod,file,str(mask)]+[str(ubos.get(i,0)) for i in range(5)]+[str(len(textures))]
        for b,f,s,d,fil in sorted(textures):fields += [str(b),str(f),str(int(s)),str(d),fil]
        manifest.append(' '.join(fields))
    for kind in ('point','rect','quad'):
        for count in sorted(counts):
            for size in ([0,1] if kind=='point' else [0]):
                for coords in [0,1]:
                    path=a.output/f'gs_{kind}_{count}_{size}_{coords}.geom'
                    path.write_text(geometry(kind,count,size,coords,system));jobs.append((path,'geom'))
    local=Path(__file__).resolve().parent
    for filename,stage in [('output.vert.glsl','vert'),('output.frag.glsl','frag'),
                           ('copy_color.frag.glsl','frag'),('restore_stencil.frag.glsl','frag')]:
        target=a.output/filename;shutil.copy2(local/filename,target);jobs.append((target,stage))
    def compile(job):
        source,stage=job;dest=source.with_suffix(source.suffix+'.dksh')
        run=subprocess.run([a.uam,'-s',stage,'-o',str(dest),str(source)],capture_output=True,timeout=90)
        if run.returncode:raise RuntimeError(source.name+': '+(run.stdout+run.stderr).decode(errors='replace'))
        return dest.name,hashlib.sha256(dest.read_bytes()).hexdigest()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        hashes.update(dict(pool.map(compile,jobs)))
    (a.output/'modules.txt').write_text('\n'.join(manifest)+'\n')
    (a.output/'hashes.json').write_text(json.dumps(hashes,indent=2))
    print(f'Game modules={len(manifest)} helper modules={len(jobs)}; game-derived shaders stay local')
if __name__=='__main__':main()
