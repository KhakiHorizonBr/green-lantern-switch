from pathlib import Path
import ctypes, hashlib, json, os, shutil, subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
BUILD=ROOT/'work/green-lantern-360/project/switch/build/generated-full-runtime'
NINJA='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
os.environ['DEVKITPRO']='C:/devkitPro'

def argv(command):
    n=ctypes.c_int(); fn=ctypes.windll.shell32.CommandLineToArgvW
    fn.restype=ctypes.POINTER(ctypes.c_wchar_p)
    p=fn(command,ctypes.byref(n)); a=[p[i] for i in range(n.value)]
    ctypes.windll.kernel32.LocalFree(p); return a

commands=subprocess.check_output([NINJA,'-t','commands','green_lantern_switch_game'],cwd=BUILD,text=True).splitlines()
private={}
for name in ['native_renderer','native_system','native_geometry_shaders','native_textures','green_lantern_switch_main']:
    matches=[c for c in commands if ' -c ' in c and c.endswith('/'+name+'.cpp') and 'green_lantern_switch_game.dir' in c]
    assert len(matches)==1,(name,len(matches))
    cmd=argv(matches[0]); obj=HERE/(name+'.o')
    cmd[cmd.index('-o')+1]=str(obj)
    # Do not write dependencies into the shared build.
    for flag in ['-MF','-MT']:
        if flag in cmd:
            i=cmd.index(flag); del cmd[i:i+2]
    if '-MD' in cmd: cmd.remove('-MD')
    cmd[-1]=str(HERE/'source/green_lantern_switch_main.cpp' if name.startswith('green_') else HERE/'native_experiment/native'/(name+'.cpp'))
    private[name+'.cpp.obj']=obj
    print('Compiling',name,flush=True)
    subprocess.run(cmd,cwd=BUILD,check=True)

lines=(BUILD/'build.ninja').read_text().splitlines()
start=next(i for i,l in enumerate(lines) if l.startswith('build green_lantern_switch_game: '))
block=lines[start:start+17]
objects=block[0].split(': ',1)[1].split(' ',1)[1].split(' |')[0].split()
def variable(name): return next(l.split(' = ',1)[1] for l in block if l.startswith('  '+name+' ='))
libs=argv('dummy '+variable('LINK_LIBRARIES'))[1:]
inputs=HERE/'switch-link-inputs'; inputs.mkdir(exist_ok=True)
link=[]; manifest=[]
for item in objects+libs:
    item=item.replace('C$:', 'C:')
    replacement=next((obj for name,obj in private.items() if item.endswith(name)),None)
    if replacement: link.append(str(replacement)); continue
    if item.startswith('-'): link.append(item); continue
    src=Path(item) if Path(item).is_absolute() else BUILD/item
    assert src.exists(),str(src)
    dest=inputs/(hashlib.sha256(str(src).encode()).hexdigest()[:12]+'-'+src.name)
    if not dest.exists(): shutil.copy2(src,dest)
    link.append(str(dest)); manifest.append(dict(source=str(src),copy=str(dest),sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
(HERE/'switch-link-inputs.json').write_text(json.dumps(manifest,indent=2))
rsp=HERE/'switch-link.rsp'; rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in link))
compiler='C:/devkitPro/devkitA64/bin/aarch64-none-elf-g++.exe'
elf=HERE/'green_lantern_escala_interna.elf'
subprocess.run([compiler]+argv('dummy '+variable('FLAGS'))[1:]+argv('dummy '+variable('LINK_FLAGS'))[1:]+argv('dummy '+variable('LINK_PATH'))[1:]+['@'+str(rsp),'-o',str(elf)],cwd=BUILD,check=True)
nacp=HERE/'green_lantern_escala_interna.nacp'
subprocess.run(['C:/devkitPro/tools/bin/nacptool.exe','--create','Green Lantern - Escala Interna','Green Lantern Recomp','0.1.0',str(nacp)],check=True)
stripped=HERE/'green_lantern_escala_interna.stripped.elf'; shutil.copy2(elf,stripped)
subprocess.run(['C:/devkitPro/devkitA64/bin/aarch64-none-elf-strip.exe','--strip-all',str(stripped)],check=True)
nro=HERE/'green_lantern_escala_interna.nro'
subprocess.run(['C:/devkitPro/tools/bin/elf2nro.exe',str(stripped),str(nro),'--nacp='+str(nacp),'--icon=C:/devkitPro/libnx/default_icon.jpg'],check=True)
print(json.dumps(dict(file=str(nro),size=nro.stat().st_size,sha256=hashlib.sha256(nro.read_bytes()).hexdigest())),flush=True)
