#!/usr/bin/env python3
"""The critical path of each frame in a DM_NVN_TRACE file (gfx/nvn/nvn_trace.h; plan item 61c, the per-frame critical-path trace; the GPU
latency and the GPU starved between the parts of a submission after totk-mobile script/scene_report.py, Apache-2.0, written anew).

A frame is the presenting thread's time from the end of one nvnQueuePresentTexture to the end of the next. On that thread it is split into
  translation  nvnQueueSubmitCommands (the replay into Metal)
  sync waits   nvnSyncWait (the game waiting for the GPU)
  acquire      nvnWindowAcquireTexture (waiting for a texture to be free: the display's pace)
  present      nvnQueuePresentTexture
  game         the rest (the game's own work)
and the largest of translation, sync waits, acquire and game is the frame's verdict. The GPU side, for the submissions made in the frame (any
thread): busy (the union of their parts on the GPU), latency (the first submit's start to the first GPU start) and starved (time the GPU had
finished a part of a submission whose next part was not committed yet, until that part started).

Usage: trace_report.py <trace.tsv> [--json]"""
import collections, json, os, sys

KINDS = ('submit', 'commit', 'gpu', 'wait', 'fence', 'acquire', 'present', 'display')
VERDICTS = ('translation', 'sync waits', 'acquire', 'game')

def load(path):
    ev = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or line.startswith('t0_ns'): continue
        c = line.rstrip('\n').split('\t')
        if len(c) != 6 or c[2] not in KINDS: continue
        ev.append({'t0': int(c[0]), 't1': int(c[1]), 'kind': c[2], 'thread': int(c[3]), 'a': int(c[4]), 'b': int(c[5])})
    return ev

def clip(e, lo, hi):
    return max(0, min(e['t1'], hi) - max(e['t0'], lo))

def union(spans):
    if os.environ.get('T_TRACE_SUM_GPU'):   # the broken variant of tools/tests/t_trace_report.py: overlapping parts counted twice
        return sum(b - a for a, b in spans)
    total, end = 0, None
    for a, b in sorted(spans):
        if end is None or a > end: total += b - a; end = b
        elif b > end: total += b - end; end = b
    return total

def frames(ev):
    pres = [e for e in ev if e['kind'] == 'present']
    if len(pres) < 2: return [], None
    main = collections.Counter(e['thread'] for e in pres).most_common(1)[0][0]
    pres = sorted((e for e in pres if e['thread'] == main), key=lambda e: e['t1'])
    gpu = collections.defaultdict(list); commit = {}
    for e in ev:
        if e['kind'] == 'gpu': gpu[e['a']].append(e)
        elif e['kind'] == 'commit': commit[(e['a'], e['b'])] = e
    out = []
    for prev, cur in zip(pres, pres[1:]):
        lo, hi = prev['t1'], cur['t1']
        on = [e for e in ev if e['thread'] == main and e['t1'] > lo and e['t0'] < hi]
        part = {k: sum(clip(e, lo, hi) for e in on if e['kind'] == k) for k in ('submit', 'wait', 'acquire', 'present')}
        f = {'frame': cur['b'], 'period': hi - lo, 'translation': part['submit'], 'sync waits': part['wait'], 'acquire': part['acquire'],
             'present': part['present']}
        f['game'] = max(0, f['period'] - f['translation'] - f['sync waits'] - f['acquire'] - f['present'])
        f['verdict'] = max(VERDICTS, key=lambda k: f[k])
        subs = [e for e in ev if e['kind'] == 'submit' and lo < e['t1'] <= hi]
        f['other threads translation'] = sum(e['t1'] - e['t0'] for e in subs if e['thread'] != main)
        parts = [g for s in subs for g in gpu.get(s['b'], [])]
        f['gpu busy'] = union([(g['t0'], g['t1']) for g in parts])
        f['gpu latency'] = min(g['t0'] for g in parts) - min(s['t0'] for s in subs) if parts else None
        starved = 0
        for s in subs:
            ps = sorted(gpu.get(s['b'], []), key=lambda g: g['b'])
            for p, q in zip(ps, ps[1:]):
                c = commit.get((s['b'], q['b']))
                if c and c['t1'] > p['t1'] and q['t0'] > p['t1']: starved += q['t0'] - p['t1']
        f['gpu starved'] = starved
        out.append(f)
    return out, main

def pct(v, q):
    v = sorted(v); return v[min(len(v) - 1, int(len(v) * q))] if v else None

def summary(fr):
    keys = ('period', 'translation', 'game', 'sync waits', 'acquire', 'present', 'other threads translation', 'gpu busy', 'gpu latency', 'gpu starved')
    s = {k: {q: (pct([f[k] for f in fr if f[k] is not None], p) or 0) / 1e6 for q, p in (('median', 0.5), ('p95', 0.95), ('max', 1.0))} for k in keys}
    s['verdicts'] = {v: sum(f['verdict'] == v for f in fr) for v in VERDICTS}
    return s

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if x != '--json']
    if len(a) != 1: print(__doc__); sys.exit(2)
    fr, main = frames(load(a[0]))
    if not fr: print('no frames: fewer than two presents in the trace'); sys.exit(1)
    s = summary(fr)
    if '--json' in sys.argv: print(json.dumps({'frames': fr, 'summary': s}, indent=1)); sys.exit(0)
    print(f'{a[0]}: {len(fr)} frames ({fr[0]["frame"]}-{fr[-1]["frame"]}), presenting thread {main}')
    print(f'{"ms":28} {"median":>8} {"p95":>8} {"max":>8}')
    for k, v in s.items():
        if k != 'verdicts': print(f'{k:28} {v["median"]:8.2f} {v["p95"]:8.2f} {v["max"]:8.2f}')
    n = len(fr); print('verdicts: ' + ', '.join(f'{k} {c} ({100 * c / n:.0f}%)' for k, c in s['verdicts'].items()))
