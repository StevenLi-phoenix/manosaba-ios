#!/usr/bin/env python3
"""The pipeline warm-up's seed from the players' own lists (plan item 209; upstream d2r-mobile 08f51a8's merged warm lists, MIT: a seed of 436
pipelines from two phones' lists made a cold Den of Evil run go from 31 / 19 / 8 frames over 33 / 50 / 100 ms to 10 / 4 / 2, all built within
about 6 s of launch). Written as pso_seed.tsv beside the shader stage map, ios/run.sh build packs it as the app's pso_manifest.tsv, which the
warm-up (gfx/nvn/nvn_mtl.m warm_start) reads before the player's own pso_user.tsv: a new install warms what earlier play met. The Mac host does
not read it (warming it slowed the Mac's prologue, which the Mac's own list covers: 55 long frames against 29).
  pso_merge.py <out pso_seed.tsv> <pso_user.tsv>...
Rows are kept as the warm-up reads them (S <shader> <stage>, R <pipeline key>, C <compute shader>), each once, in the order first met (the lists in
the order given); other lines are dropped and counted. Exit 0 fine, 1 a problem (named), 2 usage."""
import os, sys

def rows_of(text):
    keep, dropped = [], 0
    for line in text.splitlines():
        if not line or line.startswith('#'): continue
        c = line.split('\t')
        if (c[0] == 'S' and len(c) >= 3 and c[1]) or (c[0] in ('R', 'C') and len(c) >= 2 and c[1]): keep.append(line)
        else: dropped += 1
    return keep, dropped

def merge(texts):
    out, seen, dropped = [], set(), 0
    for t in texts:
        keep, d = rows_of(t); dropped += d
        for r in keep:
            if os.environ.get('DM_TEST_PSO_MERGE_NO_DEDUPE') != '1' and r in seen: continue   # (DM_TEST_PSO_MERGE_NO_DEDUPE=1: t_pso_merge's broken variant)
            seen.add(r); out.append(r)
    return out, dropped

if __name__ == '__main__':
    if len(sys.argv) < 3: print(__doc__); sys.exit(2)
    texts = []
    for p in sys.argv[2:]:
        try: texts.append(open(p, encoding='utf-8', errors='replace').read())
        except OSError as e: print(f'[red] pso_merge: {p}: {e.strerror}'); sys.exit(1)
    rows, dropped = merge(texts)
    kinds = {k: sum(r.startswith(k + '\t') for r in rows) for k in 'SRC'}
    with open(sys.argv[1], 'w', encoding='utf-8') as f:
        f.write(f'# pipeline warm-up seed: {len(rows)} rows merged from {len(texts)} lists (tools/pso_merge.py)\n')
        for r in rows: f.write(r + '\n')
    print(f'{sys.argv[1]}: {len(rows)} rows ({kinds["S"]} shaders, {kinds["R"]} render pipelines, {kinds["C"]} compute) from {len(texts)} lists, {dropped} other lines dropped')
