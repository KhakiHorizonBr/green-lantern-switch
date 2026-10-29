"""Summarize CPU observations; export a Chrome/Perfetto trace without fake GPU spans."""
from pathlib import Path
import argparse
import collections
import json
import math

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('input', type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--start-frame', type=int, default=0)
parser.add_argument('--end-frame', type=int, default=2**63)
parser.add_argument('--min-draws', type=int, default=0,
                    help='Filter by observed draw count; this is not an automatic scene label')
args = parser.parse_args()
events, dropped = [], 0
for line_no, line in enumerate(args.input.open(encoding='utf-8'), 1):
    if not line.strip():
        continue
    event = json.loads(line)
    if 'dropped_events' in event:
        dropped += event['dropped_events']
        continue
    assert event['start_ns'] >= 0 and event['duration_ns'] >= 0, line_no
    events.append(event)
assert events, 'No timeline events'
frames = collections.defaultdict(dict)
for e in events:
    if e['stage'] == 'frame_interval':
        frames[e['frame']].update(interval=e['duration_ns'], draws=e['value'])
    elif e['stage'].startswith('frame_'):
        frames[e['frame']][e['stage']] = e['value']
required = ['interval', 'frame_renderer_wall_ns', 'frame_allocation_wall_ns',
            'frame_allocation_fence_ns', 'frame_fence_wait_ns', 'frame_recorder_wait_ns',
            'frame_producer_idle_ns', 'frame_pacing_hold_ns']
selected = {f: d for f, d in frames.items()
            if args.start_frame <= f <= args.end_frame and d.get('draws', -1) >= args.min_draws
            and all(m in d for m in required)}
assert selected, 'No completed frame samples match filter'

def distribution(values):
    s = sorted(values)
    if not s:
        return None
    def p(n):
        return s[max(0, math.ceil(len(s) * n / 100) - 1)] / 1e6
    bins = [0] * 9
    thresholds = [16.666667, 25, 28.571429, 33.333333, 50, 66.666667, 100, 150]
    for value in s:
        i = sum(value / 1e6 > threshold for threshold in thresholds)
        bins[i] += 1
    return dict(n=len(s), p50_ms=p(50), p95_ms=p(95), p99_ms=p(99), max_ms=s[-1]/1e6,
                over_33_33_ms=sum(v > 33333333 for v in s),
                over_25_ms=sum(v > 25000000 for v in s),
                histogram_upper_bounds_ms=thresholds + [None], histogram_counts=bins)

metrics = ['interval', 'frame_renderer_wall_ns', 'frame_allocation_wall_ns',
           'frame_allocation_fence_ns', 'frame_fence_wait_ns', 'frame_recorder_wait_ns',
           'frame_producer_idle_ns', 'frame_pacing_hold_ns']
summary = dict(
    source=str(args.input), total_events=len(events), dropped_events=dropped,
    selected_frames=len(selected), first_frame=min(selected), last_frame=max(selected),
    incomplete_frame_rows=sum(not all(m in d for m in required) for d in frames.values()),
    min_draws_filter=args.min_draws,
    warning='CPU wall observations. Overlapping scopes are not additive. Doorbells are sampled; '
            'guest frame boundaries and display scanout are not measured. GPU completion time is '
            'host observation; GPU timestamps have a separate clock. Diagnostic I/O affects timing.',
    metrics={m: distribution([d[m] for d in selected.values() if m in d]) for m in metrics},
    draw_counts=dict(min=min(d['draws'] for d in selected.values()),
                     max=max(d['draws'] for d in selected.values())),
)
summary['allocation_wall_excluding_fence'] = distribution([
    max(0, d.get('frame_allocation_wall_ns', 0) - d.get('frame_allocation_fence_ns', 0))
    for d in selected.values()])
summary['completed_gpu_submissions'] = distribution([
    e['value'] for e in events if e['stage'] == 'gpu_completion_observed'
    and e['frame'] in selected and e['value'] > 0])
submitted, completed = collections.defaultdict(set), {}
for e in events:
    if e['stage'] == 'queue_submit':
        submitted[e['frame']].add(e['submission'])
    elif e['stage'] == 'gpu_completion_observed' and e['value'] > 0:
        completed[e['submission']] = e['value']
summary['gpu_duration_sum_per_fully_completed_frame'] = distribution([
    sum(completed[s] for s in ids) for frame, ids in submitted.items()
    if frame in selected and ids and all(s in completed for s in ids)])
summary['observed_scope_counts'] = dict(collections.Counter(e['stage'] for e in events))
args.output.mkdir(parents=True, exist_ok=True)
(args.output/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')

# Export complete trace, not just the summary filter, to preserve causal context.
origin = min(e['start_ns'] for e in events)
threads = {'ring': 1, 'record': 2, 'producer': 3, 'cadence': 4}
chrome = [dict(ph='M', name='thread_name', pid=1, tid=i, args={'name': n})
          for n, i in threads.items()]
for e in events:
    name = e['stage']
    thread = 'cadence' if name == 'frame_interval' else e['thread']
    item = dict(name=name, pid=1, tid=threads[thread],
                ts=(e['start_ns']-origin)/1e3,
                args=dict(frame=e['frame'], submission=e['submission'], value=e['value']))
    if e['duration_ns'] > 0:
        item.update(ph='X', dur=e['duration_ns']/1e3)
    else:
        item.update(ph='i', s='t')
    chrome.append(item)
(args.output/'timeline.chrome.json').write_text(json.dumps(dict(traceEvents=chrome)), encoding='utf-8')
print(json.dumps({k: summary[k] for k in ['selected_frames','first_frame','last_frame','dropped_events']}, indent=2))
for key in ['interval','frame_renderer_wall_ns','frame_allocation_wall_ns','frame_allocation_fence_ns']:
    print(key, json.dumps(summary['metrics'][key]))
