#!/usr/bin/env python3
"""ios/tools/border_msl.py (plan item 214): the listed shaders' .metal go into the app's MSL directory (comments skipped), with every vertex
shader whose texture bindings are a listed vertex shader's (not a fragment shader's, not one with other bindings); a listed one without a source
refuses, naming it. Nothing copied (DM_TEST_BORDER_MSL_NONE=1): 2 fail; the list alone (DM_TEST_BORDER_MSL_LISTED_ONLY=1): 1 fails."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TOOL = os.path.join(ROOT, 'ios', 'tools', 'border_msl.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
GRASS = [{'msl_name': 'vp_c1', 'kind': 'c1'}, {'msl_name': 'vp_t_tcb_C', 'kind': 'tex', 'reg': 2}, {'msl_name': 'vp_t_tcb_CSmplr', 'kind': 'samp_tex', 'reg': 2}]
with tempfile.TemporaryDirectory() as d:
    mac, app = os.path.join(d, 'mac'), os.path.join(d, 'app'); os.makedirs(mac); os.makedirs(app)
    shaders = {'aaaa_b': ('vs', GRASS), 'bbbb_b': ('ps', [GRASS[1], GRASS[2]]), 'cccc_b': ('vs', GRASS[:1] + [dict(GRASS[1], reg=3), GRASS[2]]),
               'dddd_b': ('vs', [{'msl_name': 'vp_c7', 'kind': 'cbuf', 'reg': 4}] + GRASS[1:]), 'eeee_b': ('ps', GRASS[1:])}
    for k, (st, b) in shaders.items():
        open(os.path.join(mac, k + '.metal'), 'w').write(f'// {k}\n'); json.dump({'stage': st, 'bindings': b}, open(os.path.join(app, k + '.json'), 'w'))
    lst = os.path.join(d, 'list.txt'); open(lst, 'w').write('# a comment\naaaa_b\n\nbbbb_b\n')
    r = subprocess.run([sys.executable, '-I', TOOL, lst, mac, app], capture_output=True, text=True)
    got = sorted(f for f in os.listdir(app) if f.endswith('.metal'))
    check('the listed sources copied, nothing else of theirs', r.returncode == 0 and {'aaaa_b.metal', 'bbbb_b.metal'} <= set(got), r.stdout + r.stderr + str(got))
    check('with the vertex shader of the same texture bindings (dddd), not one on another register (cccc) nor a fragment shader (eeee)',
          got == ['aaaa_b.metal', 'bbbb_b.metal', 'dddd_b.metal'], str(got))
    open(lst, 'a').write('ffff_b\n')
    r = subprocess.run([sys.executable, '-I', TOOL, lst, mac, app], capture_output=True, text=True)
    check('a listed shader without a source: refused, named', r.returncode == 1 and 'ffff_b' in r.stdout, r.stdout + r.stderr)
print(f't_border_msl: {total} checks, {fails} failed'); sys.exit(fails != 0)
