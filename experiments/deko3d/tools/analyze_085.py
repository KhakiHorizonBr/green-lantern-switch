from pathlib import Path
import re,json,shutil
root=Path(__file__).resolve().parents[2]
p=Path('<USER>/AppData/Local/Temp/green_lantern_085.log')
s=p.read_text(encoding='utf-8-sig',errors='replace')
metrics=[]
for line in s.splitlines():
    if '[deko-perf-v8]' in line:
        data={k:float(v) for k,v in re.findall(r'(\w+)=([0-9]+(?:\.[0-9]+)?)',line)}
        data['timestamp']=line[1:24];metrics.append(data)
missing=sorted(set(re.findall(r'missing shader ([vp]) ([0-9A-F]+) mod ([0-9A-F]+)',s)))
other=sorted(set(re.findall(r'\[deko-game\] (.*)',s)))
out=root/'outputs/deko-native/hardware-085';out.mkdir(exist_ok=True)
shutil.copy2(p,out/p.name)
data=dict(metrics=metrics,missing_shader_variants=len(missing),missing_shaders=missing,
    unique_backend_errors=other,boot=[l for l in s.splitlines() if '[deko-game-v9]' in l],
    fatal_driver_errors=[l for l in s.splitlines() if 'kickoff result=' in l or 'error de deko3d' in l])
(out/'analysis.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
print(json.dumps({'windows':len(metrics),'fps_min':min(x['fps'] for x in metrics),
 'fps_max':max(x['fps'] for x in metrics),'first':metrics[:3],'last':metrics[-3:],
 'missing_shader_variants':len(missing),'backend_error_types':sorted(set(x.split(' shader ')[0] for x in other)),
 'boot':data['boot'],'fatal_driver_errors':len(data['fatal_driver_errors'])},indent=2))
