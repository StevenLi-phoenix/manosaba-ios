#!/usr/bin/env python3
"""ios/tools/shader_check.py (plan item 174) on a chain directory written here: complete (two shaders, one with its integer variant) passes; a library
missing, a json of another chain, a json of another stage than the map's, and a fout key without its integer variant are each reported and fail; a
map key with neither file (the directory of another game) fails. The library check left out (T_SHADER_CHECK_NO_LIB=1, the broken variant): 2 fail."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import shader_check as sc
if os.environ.get('T_SHADER_CHECK_NO_LIB'):   # the broken variant: a library is not looked for
    _check = sc.check
    def check_nolib(shmap, d, fout=None):
        n, bad = _check(shmap, d, fout); return n, [b for b in bad if not b.endswith('no library') and 'integer variant' not in b]
    sc.check = check_nolib
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
with tempfile.TemporaryDirectory() as t:
    d = os.path.join(t, 'b-abc123'); os.makedirs(d)
    shmap = os.path.join(t, 'map.tsv'); open(shmap, 'w').write('# map\n1111111111111111\tvs\tf1\n2222222222222222\tps\tf2\n')
    fout = os.path.join(t, 'fout.tsv'); open(fout, 'w').write('# fout\n2222222222222222\t0\tuint\n')
    def put(k, stage, chain='abc123', lib=True, js=True):
        if lib: open(os.path.join(d, f'{k}_b.metallib'), 'wb').write(b'lib')
        if js: json.dump({'chain': chain, 'stage': stage}, open(os.path.join(d, f'{k}_b.json'), 'w'))
    put('1111111111111111', 'vs'); put('2222222222222222', 'ps'); open(os.path.join(d, '2222222222222222_i_b.metallib'), 'wb').write(b'lib')
    n, bad = sc.check(shmap, d, fout); check('complete: 2 shaders, the integer variant there', n == 2 and not bad, str(bad))
    os.remove(os.path.join(d, '1111111111111111_b.metallib')); n, bad = sc.check(shmap, d, fout)
    check('a library missing: reported', bad == ['1111111111111111: no library'], str(bad)); put('1111111111111111', 'vs')
    put('1111111111111111', 'vs', chain='old999'); n, bad = sc.check(shmap, d, fout)
    check('a json of another chain: reported', len(bad) == 1 and 'chain old999' in bad[0], str(bad)); put('1111111111111111', 'vs')
    put('2222222222222222', 'vs'); n, bad = sc.check(shmap, d, fout)
    check('a json of another stage than the map\'s: reported', len(bad) == 1 and 'stage vs' in bad[0], str(bad)); put('2222222222222222', 'ps')
    os.remove(os.path.join(d, '2222222222222222_i_b.metallib')); n, bad = sc.check(shmap, d, fout)
    check('a fout key without its integer variant: reported', len(bad) == 1 and 'integer variant' in bad[0], str(bad))
    open(shmap, 'a').write('3333333333333333\tps\tf3\n')
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'shader_check.py'), shmap, d], capture_output=True, text=True)
    check('another game\'s directory (a map key with no file at all): exit 1 with the red line', r.returncode == 1 and '[red]' in r.stdout and '3333333333333333: no library' in r.stdout, r.stdout)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
