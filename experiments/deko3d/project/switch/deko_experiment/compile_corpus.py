"""Compile accepted GLSL with installed uam; no shader source is executed."""
import argparse, concurrent.futures, hashlib, json, subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('directory',type=Path)
    p.add_argument('--uam',type=Path,required=True)
    a=p.parse_args(); root=a.directory.resolve(); uam=a.uam.resolve()
    targets=[json.loads(s) for s in (root/'conversion.jsonl').read_text().splitlines()]
    def compile_one(row):
        if not row['converted']: return dict(file=row['file'],stage='conversion',ok=False,error=row['error'])
        name=row['file']; src=root/(name+'.glsl'); dst=root/(name+'.dksh')
        stage='vert' if name.startswith('vs_') else 'frag' if name.startswith('ps_') else None
        if not stage: raise ValueError('Unrecognized stage: '+name)
        try:
            run=subprocess.run([str(uam),'-s',stage,'-o',str(dst),str(src)],capture_output=True,timeout=90)
            data=dst.read_bytes() if dst.exists() else b''
            ok=run.returncode==0 and len(data)>=24 and data[:4]==b'DKSH'
            return dict(file=name,stage='uam',ok=ok,exit_code=run.returncode,bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest() if ok else None,
                diagnostic=(run.stdout+run.stderr).decode('utf-8','replace'))
        except subprocess.TimeoutExpired: return dict(file=name,stage='uam',ok=False,error='compile timeout')
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: rows=list(pool.map(compile_one,targets))
    summary=dict(modules=len(rows),dksh_ok=sum(x['ok'] for x in rows),
        conversion_rejected=sum(x['stage']=='conversion' for x in rows),
        uam_rejected=sum(x['stage']=='uam' and not x['ok'] for x in rows),
        uam_sha256=hashlib.sha256(uam.read_bytes()).hexdigest(),results=rows)
    (root/'uam-results.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if k!='results'},indent=2))
    return 0 if summary['dksh_ok']==len(rows) else 1
if __name__=='__main__': raise SystemExit(main())
