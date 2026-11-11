from pathlib import Path
import re,json,datetime,statistics
temp=Path('<USER>/AppData/Local/Temp')
paths=[temp/n for n in ['green_lantern_082.2.log','green_lantern_082.1.log','green_lantern_082.log']]
rows=[]
for p in paths:
    for line in p.read_text(encoding='utf-8-sig',errors='replace').splitlines():
        m=re.match(r'\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]',line)
        if m:rows.append((datetime.datetime.fromisoformat(m[1]),line))
rows.sort(key=lambda r:r[0]);pending=None;times=[];last_frame=draws=resolves=0
errors=[]
for stamp,line in rows:
    if '[deko-check-v4] before ' in line:pending=stamp
    elif '[deko-check-v4] GPU completed ' in line and pending:
        times.append((stamp-pending).total_seconds()*1000);pending=None
    if 'frame=' in line:
        m=re.search(r'frame=(\d+)',line)
        if m:last_frame=max(last_frame,int(m[1]))
    m=re.search(r'draws=(\d+) resolves=(\d+)',line)
    if m:draws=max(draws,int(m[1]));resolves=max(resolves,int(m[2]))
    if '[error]' in line:errors.append(line)
wall=(rows[-1][0]-rows[0][0]).total_seconds()
result=dict(first=str(rows[0][0]),last=str(rows[-1][0]),wall_s=wall,
    last_frame=last_frame,draws=draws,resolves=resolves,checkpoint_pairs=len(times),
    checkpoint_span_s=sum(times)/1000,checkpoint_median_ms=statistics.median(times),
    checkpoint_p95_ms=sorted(times)[int(len(times)*.95)],
    log_bytes=sum(p.stat().st_size for p in paths),
    errors_unique=sorted(set(re.sub(r'^.*?\] \[error\].*?\] ','',s) for s in errors)),
    interpretation='Checkpoint spans include logging, submission and GPU waits; not a pure GPU timer. Rendering kept advancing to end; no proof of save completion.')
out=Path(__file__).resolve().parent/'hardware-082';out.mkdir(exist_ok=True)
for p in paths:
    import shutil;shutil.copy2(p,out/p.name)
(out/'analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
