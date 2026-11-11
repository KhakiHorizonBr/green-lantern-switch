"""Reuse local compiled guest objects, snapshot all link inputs, never write the shared build."""
from pathlib import Path
import ctypes, json, re, shutil, subprocess, hashlib
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
BUILD=ROOT/'work/green-lantern-360/project/out/build/win-amd64-vulkan'
def argv(command):
    n=ctypes.c_int(); fn=ctypes.windll.shell32.CommandLineToArgvW
    fn.restype=ctypes.POINTER(ctypes.c_wchar_p)
    p=fn(command,ctypes.byref(n)); a=[p[i] for i in range(n.value)]
    ctypes.windll.kernel32.LocalFree(p); return a
HERE.joinpath('bin').mkdir(exist_ok=True)
HERE.joinpath('link-inputs').mkdir(exist_ok=True)
recipe=(HERE/'renderer-compile.txt').read_text().strip()
args=argv(recipe)
compiler=args[0]
args=[a for a in args if not a.startswith(('/Fo','/Fd','-clang:-MT','-clang:-MF'))]
private_objects={}
for name in ['native_renderer','native_system','native_geometry_shaders','native_textures']:
    source=HERE/'native_experiment/native'/(name+'.cpp'); obj=HERE/(name+'.obj')
    private_objects[name+'.cpp.obj']=obj
    latest=max([source.stat().st_mtime]+[h.stat().st_mtime for h in (HERE/'native_experiment/native').rglob('*') if h.is_file()])
    if obj.exists() and obj.stat().st_mtime >= latest: continue
    cmd=list(args); cmd[-1]=str(source); cmd.insert(1,'/Fo'+str(obj))
    subprocess.run(cmd,cwd=BUILD,check=True)
block=(HERE/'link-block.txt').read_text().splitlines()
objects=block[0].split(': ',1)[1].split(' ',1)[1].split(' |')[0].split()
libs=argv('dummy '+next(l.split(' = ',1)[1] for l in block if l.startswith('  LINK_LIBRARIES =')))[1:]
link=[]; manifest=[]
for item in objects+libs:
    private=next((obj for name,obj in private_objects.items() if item.endswith(name)),None)
    if private: link.append(str(private)); continue
    src=Path(item) if Path(item).is_absolute() else BUILD/item
    if not src.exists():
        assert item.endswith('.lib') and not ('/' in item or '\\' in item), item
        link.append(item); continue # Windows system libraries via vcvars LIB
    name=hashlib.sha256(str(src).encode()).hexdigest()[:12]+'-'+src.name
    dest=HERE/'link-inputs'/name
    if not dest.exists(): shutil.copy2(src,dest)
    link.append(str(dest));manifest.append(dict(source=str(src),copy=str(dest),sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
linker=str(Path(compiler).with_name('lld-link.exe'))
rsp=HERE/'link.rsp'
rsp.write_text('\n'.join('"'+s+'"' for s in link),encoding='utf-8')
subprocess.run([linker,'/nologo','@'+str(rsp),'/out:'+str(HERE/'bin/green_lantern_native_pc.exe'),
               '/machine:x64','/INCREMENTAL:NO','/subsystem:windows'],cwd=HERE,check=True)
# Stable runtime dependencies from the previous validated private binary.
for src in (ROOT/'outputs/partial-submit/isolated-bin').glob('*.dll'):
    shutil.copy2(src,HERE/'bin'/src.name)
(HERE/'link-inputs.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('Private PC diagnostic build passed',flush=True)
