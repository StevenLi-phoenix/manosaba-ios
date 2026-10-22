#!/usr/bin/env python3
"""tools/pso_merge.py (plan item 209): two players' lists merged into the warm-up seed: each row once, in the order first met, comments and lines
the warm-up does not read dropped, the file the warm-up reads (a comment header, then the rows). Without the dedupe
(DM_TEST_PSO_MERGE_NO_DEDUPE=1, the broken variant): 2 fail."""
import os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools')); import pso_merge as m
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
a = 'S\taaaa_b\t0\nS\tbbbb_b\t1\nR\t6161\n# a comment\nC\tcccc_b\n'
b = 'R\t6161\nR\t6262\nS\taaaa_b\t0\nX\tsomething else\nS\tshort\n\n'
rows, dropped = m.merge([a, b])
check('each row once, in the order first met', rows == ['S\taaaa_b\t0', 'S\tbbbb_b\t1', 'R\t6161', 'C\tcccc_b', 'R\t6262'], str(rows))
check('lines the warm-up does not read dropped (X, a short S), comments and blank lines skipped', dropped == 2, str(dropped))
with tempfile.TemporaryDirectory() as d:
    for n, t in (('a', a), ('b', b)): open(os.path.join(d, n), 'w').write(t)
    out = os.path.join(d, 'pso_manifest.tsv')
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'pso_merge.py'), out, os.path.join(d, 'a'), os.path.join(d, 'b')], capture_output=True, text=True)
    lines = open(out).read().splitlines() if os.path.exists(out) else []
    check('the file: a comment header, then the 5 rows', r.returncode == 0 and lines[0].startswith('# ') and lines[1:] == rows and '5 rows (2 shaders, 2 render pipelines, 1 compute)' in r.stdout, r.stdout + r.stderr)
print(f't_pso_merge: {total} checks, {fails} failed'); sys.exit(fails != 0)
