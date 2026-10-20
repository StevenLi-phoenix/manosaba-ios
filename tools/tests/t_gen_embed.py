#!/usr/bin/env python3
"""ios/tools/gen_embed.py applies the product's code options before it embeds a module (plan item 136): iOS maps the signed executable's code, which
the loader cannot change, so the options the app is built with (DM_OPTIONS at build time, else the product's options.default) are written into a
copy of the image that the assembly includes. On a scratch product with two options: the default one is in the copy (both of its rows), the other
is not, the assembly includes the copy, the product's own image is untouched; DM_OPTIONS=none includes the product's image itself; an option whose
original bytes differ, or one no module lists, stops the build. Options not applied (DM_TEST_EMBED_NO_OPTIONS=1, the broken variant): 3 fail."""
import os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TOOL = os.path.join(ROOT, 'ios', 'tools', 'gen_embed.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<92} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def product(d, default='fast', fast_ob='11223344'):
    os.makedirs(d, exist_ok=True)
    img = bytearray(0x14000)
    img[0x100:0x104] = bytes.fromhex('11223344'); img[0x200:0x204] = bytes.fromhex('55667788'); img[0x300:0x304] = bytes.fromhex('0a0b0c0d')
    open(f'{d}/m.patched.bin', 'wb').write(img)
    open(f'{d}/m.fixups.tsv', 'w').write('# PATCH_OFF\t0x10000\n# KEY_PAGE_OFF\t0x8000\n# KEY_PAGE_SIZE\t0x4000\n# TEXT_END\t0x2000\n')
    open(f'{d}/m.options.tsv', 'w').write('# BUILD_ID\t00\n'
        f'fast\t0x100\t{fast_ob}\taabbccdd\tthe first of two words\n'
        'fast\t0x200\t55667788\t99aabbcc\tthe second\n'
        'other\t0x300\t0a0b0c0d\t01020304\tanother option\n')
    if default is not None: open(f'{d}/options.default', 'w').write(default + '\n')
def run(d, out, extra=None):
    env = {k: v for k, v in os.environ.items() if k != 'DM_OPTIONS'}; env.update(extra or {})
    r = subprocess.run([sys.executable, '-I', TOOL, d, out, 'm'], capture_output=True, text=True, env=env); return r.returncode, r.stdout + r.stderr
with tempfile.TemporaryDirectory() as td:
    p, g = os.path.join(td, 'prod'), os.path.join(td, 'gen'); os.makedirs(g); product(p)
    code, log = run(p, os.path.join(g, 'embed.S'))
    S = open(os.path.join(g, 'embed.S')).read() if code == 0 else ''
    copy = os.path.join(g, 'm.embedded.bin')
    emb = open(copy, 'rb').read() if os.path.exists(copy) else b''
    check('the default option: built (exit 0)', code == 0, log.strip()[-300:])
    check('... both of its rows written into the embedded copy', emb[0x100:0x104].hex() == 'aabbccdd' and emb[0x200:0x204].hex() == '99aabbcc', emb[0x100:0x104].hex())
    check('... the other option not', emb[0x300:0x304].hex() == '0a0b0c0d')
    check('... the assembly includes the copy, the product\'s image untouched', '/m.embedded.bin"' in S and open(f'{p}/m.patched.bin', 'rb').read()[0x100:0x104].hex() == '11223344')
    code, log = run(p, os.path.join(g, 'embed2.S'), {'DM_OPTIONS': 'none'})
    S2 = open(os.path.join(g, 'embed2.S')).read() if code == 0 else ''
    check('DM_OPTIONS=none: the product\'s own image is included', code == 0 and '/m.patched.bin"' in S2 and 'embedded.bin' not in S2, log.strip()[-200:])
    p2 = os.path.join(td, 'prod2'); product(p2, fast_ob='deadbeef')
    code, log = run(p2, os.path.join(g, 'embed3.S'))
    check('an option whose original bytes differ: the build stops', code != 0 and 'fast' in log, log.strip()[-200:])
    code, log = run(p, os.path.join(g, 'embed4.S'), {'DM_OPTIONS': 'fast,typo'})
    check('an option no module lists: the build stops', code != 0 and 'typo' in log, log.strip()[-200:])
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
