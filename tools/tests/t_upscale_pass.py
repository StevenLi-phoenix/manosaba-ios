#!/usr/bin/env python3
"""The game's own upscale and compose passes (plan item 63): tools/gen_game.py takes game.json "upscale_pass" and "compose_pass" ({"fs": the
fragment shader key, 16 hex digits; "color": perceptual / linear / hdr}) into the profile as .upscale_fs / .upscale_color and .compose_fs /
.compose_color (MTLFXSpatialScalerColorProcessingMode: 0 / 1 / 2), which DeviceInitialize hands to the Metal layer (mtl_set_upscale_pass); without
them .upscale_fs = 0 and .compose_fs = 0. A key that is not 16 hex digits, another colour, or another field is refused, in either. Tears of the
Kingdom's profile: ac58fc18eda5f387 and 0ac9a54938974396, both hdr. The check left out (T_UPSCALE_PASS_NO_CHECK=1, the broken variant): 4 fail."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
GEN = os.path.join(ROOT, 'tools', 'gen_game.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
with tempfile.TemporaryDirectory() as d:
    gen = GEN
    if os.environ.get('T_UPSCALE_PASS_NO_CHECK'):   # the broken variant: upscale_pass not checked (an unknown colour then fails on its lookup)
        src = open(GEN).read().replace("        if up is not None and not (isinstance(up, dict) and sorted(up) == ['color', 'fs'] and re.fullmatch(r'[0-9a-f]{16}', str(up['fs'])) and up['color'] in UPSCALE_COLOR):",
                                       "        if False:")
        src = src.replace("UPSCALE_COLOR[g[k]['color']]", "UPSCALE_COLOR.get(g[k]['color'], 0)")
        gen = os.path.join(d, 'gen_game_broken.py'); open(gen, 'w').write(src)
    base = json.load(open(os.path.join(ROOT, 'games', 'manosaba', 'game.json')))
    def run(up, game=None, key='upscale_pass', compose=None):
        g = dict(game or base); g.pop('upscale_pass', None); g.pop('compose_pass', None)
        if up is not None: g[key] = up
        if compose is not None: g['compose_pass'] = compose
        p, out = os.path.join(d, 'game.json'), os.path.join(d, 'p.c'); json.dump(g, open(p, 'w'))
        if os.path.exists(out): os.remove(out)
        r = subprocess.run([sys.executable, '-I', gen, p, out], capture_output=True, text=True)
        return r, open(out).read() if os.path.exists(out) else ''
    r, c = run({'fs': '0123456789abcdef', 'color': 'linear'})
    check('upscale_pass {fs, linear}: .upscale_fs and .upscale_color 1', r.returncode == 0 and '.upscale_fs = "0123456789abcdef", .upscale_color = 1,' in c, r.stderr)
    r, c = run(None)
    check('neither: .upscale_fs = 0, .compose_fs = 0', r.returncode == 0 and '.upscale_fs = 0,' in c and '.compose_fs = 0,' in c, r.stderr)
    r, c = run({'fs': 'fedcba9876543210', 'color': 'perceptual'}, key='compose_pass')
    check('compose_pass {fs, perceptual}: .compose_fs and .compose_color 0', r.returncode == 0 and '.compose_fs = "fedcba9876543210", .compose_color = 0,' in c, r.stderr)
    r, c = run({'fs': 'fedcba987654321', 'color': 'hdr'}, key='compose_pass')
    check('compose_pass with a key of 15 hex digits: refused', r.returncode != 0 and 'compose_pass must be' in r.stderr, r.stdout + r.stderr)
    for what, up in (('a key of 15 hex digits', {'fs': '0123456789abcde', 'color': 'hdr'}), ('another colour (srgb)', {'fs': '0123456789abcdef', 'color': 'srgb'}),
                     ('another field', {'fs': '0123456789abcdef', 'color': 'hdr', 'when': 'docked'})):
        r, c = run(up)
        check(f'{what}: refused', r.returncode != 0 and 'upscale_pass must be' in r.stderr, r.stdout + r.stderr)
    totk = json.load(open(os.path.join(ROOT, 'games', 'totk', 'game.json')))
    r, c = run(totk['upscale_pass'], totk, compose=totk['compose_pass'])
    check('Tears of the Kingdom: ac58fc18eda5f387 and 0ac9a54938974396, both hdr (2)', r.returncode == 0 and '.upscale_fs = "ac58fc18eda5f387", .upscale_color = 2,' in c
          and '.compose_fs = "0ac9a54938974396", .compose_color = 2,' in c, r.stderr)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
