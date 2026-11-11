"""Summarize short measured-time windows without calling them display FPS."""
from pathlib import Path
import argparse, json, re

parser=argparse.ArgumentParser()
parser.add_argument('log', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--min-frame', type=int, default=0)
parser.add_argument('--max-frame', type=int, default=2**63-1)
args=parser.parse_args()
text=args.log.read_text(encoding='utf-8',errors='replace')
pattern=re.compile(r'\[gl-fps-second-v1\] frame=(\d+) seconds=([\d.]+) frames=(\d+) fps=([\d.]+); intervals <=35ms=(\d+) 35-50ms=(\d+) 50-100ms=(\d+) >100ms=(\d+)')
windows=[]
for m in pattern.finditer(text):
    frame,seconds,count,fps,a,b,c,d=m.groups()
    seconds=float(seconds); count=int(count); fps=float(fps)
    bins=[int(x) for x in [a,b,c,d]]
    assert count == sum(bins),(count,bins)
    assert abs(fps-count/seconds)<0.002,(fps,count,seconds)
    # Keep only windows entirely inside the selected frame interval.
    if int(frame)-count+1 >= args.min_frame and int(frame) <= args.max_frame:
        windows.append(dict(end_frame=int(frame),seconds=seconds,frames=count,
            fps=fps,interval_counts=bins))
elapsed=sum(w['seconds'] for w in windows)
bands=[('below10',0,10),('10to20',10,20),('20to25',20,25),('25plus',25,float('inf'))]
summary=dict(log=str(args.log),windows=windows,measured_seconds=elapsed,
    completion_fps=sum(w['frames'] for w in windows)/elapsed if elapsed else None,
    seconds_by_window_fps={label:sum(w['seconds'] for w in windows if lo<=w['fps']<hi) for label,lo,hi in bands},
    clock_reports=[l for l in text.splitlines() if '[gl-clocks-v1]' in l],
    warning='Host-observed completion intervals. Bands classify one-second averages, not individual frames or screen scanout. Includes menus/loading unless filtered by frame. Partial final window omitted.')
result=json.dumps(summary,ensure_ascii=False,indent=2)
if args.output: args.output.write_text(result,encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k not in ['windows','clock_reports']},ensure_ascii=False,indent=2))
