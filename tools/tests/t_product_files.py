#!/usr/bin/env python3
"""ios/tools/product_files.py (plan item 62l): the app carries each module's fixups and gates, and its code option list when the product has
one (product60), so host/options.c knows the options built into the image; a module's missing fixups or gates are reported. The option lists
left out (T_PRODUCT_FILES_NO_OPTIONS=1, the broken variant: as ios/run.sh copied before): 2 fail, 3 with the workspace's product60."""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import product_files as pf
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def fs(names): return lambda p: os.path.basename(p) in names
base = {f'{m}.{k}.tsv' for m in ('main', 'sdk') for k in ('fixups', 'gates')}
paths, bad = pf.files('/p', ['main', 'sdk'], fs(base))
check('a product without options (the 30 fps one): fixups and gates of each module, nothing missing', sorted(map(os.path.basename, paths)) == sorted(base) and not bad, (paths, bad))
paths, bad = pf.files('/p', ['main', 'sdk'], fs(base | {'main.options.tsv', 'sdk.options.tsv'}))
names = set(map(os.path.basename, paths))
check('product60: main.options.tsv carried', 'main.options.tsv' in names, paths)
check('product60: sdk.options.tsv carried', 'sdk.options.tsv' in names, paths)
paths, bad = pf.files('/p', ['main'], fs({'main.fixups.tsv'}))
check('main.gates.tsv missing: reported', [os.path.basename(p) for p in bad] == ['main.gates.tsv'], bad)
p60 = os.path.join(os.environ.get('DM_ROOT', os.path.join(ROOT, 'work')), 'build', 'totk', 'product60')
if os.path.isdir(p60):
    paths, bad = pf.files(p60, ['main', 'sdk', 'subsdk0'])
    check('the workspace product60: its three option lists carried, nothing missing', sum(p.endswith('.options.tsv') for p in paths) == 3 and not bad, (paths, bad))
print(f'== {total} checks, {fails} failed ==')
sys.exit(fails != 0)
