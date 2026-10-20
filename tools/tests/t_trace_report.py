#!/usr/bin/env python3
"""tools/trace_report.py (plan item 61c) on a made-up trace of five presents (four frames of 20 ms) on thread 100, with exact answers:
  frame 1  submit 2, sync waits 10, present 1: game 7 → sync waits; its submission's two GPU parts overlap by 1 ms: busy 9, not 10; latency 1
  frame 2  submit 11, a wait 1, present 1: game 7 → translation; the second part committed 3 ms after the first ended on the GPU, which waited 3.5
  frame 3  submit 2, wait 1, acquire 2, present 1: game 14 → game; a submit on thread 200 counted apart, not in the verdict
  frame 4  acquire 12, submit 2, present 1: game 5 → acquire
The output file's format as nvn_trace.c writes it (a comment line, the header). Parts counted twice where they overlap (T_TRACE_SUM_GPU=1, the
broken variant): 1 fails."""
import os, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools')); import trace_report as tr
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
M = 1000000
rows = []
def ev(t0, t1, kind, a=0, b=0, thread=100): rows.append((int(t0 * M), int(t1 * M), kind, thread, a, b))
for i, t in enumerate((0, 20, 40, 60, 80)): ev(t - 1, t, 'present', 0, 10 + i)
# frame 1 (0-20): fence 1 in two parts, overlapping on the GPU
ev(1, 3, 'submit', 1, 1); ev(1.5, 2.5, 'commit', 1, 1); ev(2.6, 2.9, 'commit', 1, 2); ev(4, 9, 'gpu', 1, 1); ev(8, 13, 'gpu', 1, 2); ev(5, 15, 'wait', 1, 0)
# frame 2 (20-40): fence 2, part 2 committed at 30 after part 1 ended at 27, started at 30.5
ev(21, 32, 'submit', 1, 2); ev(25.5, 26, 'commit', 2, 1); ev(29, 30, 'commit', 2, 2); ev(25, 27, 'gpu', 2, 1); ev(30.5, 33, 'gpu', 2, 2); ev(34, 35, 'wait', 2, 0)
# frame 3 (40-60): game-bound, and a submit on another thread
ev(41, 43, 'acquire', 0, 0); ev(44, 46, 'submit', 1, 3); ev(47, 48, 'wait', 3, 0); ev(45, 50, 'submit', 1, 4, thread=200); ev(46, 50, 'gpu', 3, 1)
# frame 4 (60-80): waiting for a texture
ev(60, 72, 'acquire', 1, 12 * M); ev(73, 75, 'submit', 1, 5); ev(74, 76, 'gpu', 5, 1); ev(76, 76, 'display', 3, 0)
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 't.tsv')
    with open(p, 'w') as f:
        f.write(f'# frames 1-4, {len(rows)} events, 0 not kept (room for 4096)\nt0_ns\tt1_ns\tkind\tthread\ta\tb\n')
        for r in sorted(rows): f.write('\t'.join(str(x) for x in r) + '\n')
    fr, main = tr.frames(tr.load(p))
ms = lambda v: None if v is None else v / M
check('four frames on the presenting thread 100, numbered by the present (11-14), 20 ms each',
      main == 100 and [f['frame'] for f in fr] == [11, 12, 13, 14] and all(f['period'] == 20 * M for f in fr), [(f['frame'], f['period']) for f in fr])
f1, f2, f3, f4 = fr if len(fr) == 4 else ({},) * 4
check('frame 1: submit 2, sync waits 10, present 1, game 7 → sync waits',
      [ms(f1.get(k)) for k in ('translation', 'sync waits', 'present', 'game')] == [2, 10, 1, 7] and f1.get('verdict') == 'sync waits', f1)
check('... GPU busy 9 ms (two parts overlapping by 1), latency 3 ms (submit at 1, GPU at 4), not starved',
      ms(f1.get('gpu busy')) == 9 and ms(f1.get('gpu latency')) == 3 and f1.get('gpu starved') == 0, f1)
check('frame 2: submit 11, game 7 → translation; the GPU starved 3.5 ms between the parts',
      ms(f2.get('translation')) == 11 and ms(f2.get('game')) == 7 and f2.get('verdict') == 'translation' and ms(f2.get('gpu starved')) == 3.5, f2)
check('frame 3: game 14 → game; the other thread\'s submit (5 ms) counted apart, the GPU busy 4 ms',
      ms(f3.get('game')) == 14 and f3.get('verdict') == 'game' and ms(f3.get('other threads translation')) == 5 and ms(f3.get('gpu busy')) == 4, f3)
check('frame 4: acquire 12 → acquire', ms(f4.get('acquire')) == 12 and f4.get('verdict') == 'acquire', f4)
s = tr.summary(fr)
check('summary: one frame of each verdict, the median period 20 ms', s['verdicts'] == {'translation': 1, 'sync waits': 1, 'acquire': 1, 'game': 1} and s['period']['median'] == 20, s['verdicts'])
print(f'== {total} checks, {fails} failed ==')
sys.exit(fails != 0)
