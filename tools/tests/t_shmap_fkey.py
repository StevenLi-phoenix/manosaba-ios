#!/usr/bin/env python3
"""gfx/tools/shmap_from_capture.py's functional keys (plan item 169) on a capture written here: two pixel shaders that differ only in metadata the
translator does not read (control +0x7d0) get two keys and one functional key; a third with one constant byte changed gets another; a compute shader
with control bounds that are not sane gets none; the pixel shader's functional key is the value the host's shader_fkey gives for the same bytes
(gfx/tests/t_nvn.c 5c, 7c7333bea0fcafb6). The workgroup left out of a compute shader's key (T_SHMAP_NO_WORKGROUP=1, the broken variant): 1 fails."""
import hashlib, os, struct, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TOOL = os.path.join(ROOT, 'gfx', 'tools', 'shmap_from_capture.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

def shader(d, ctl, data):
    n = struct.unpack_from('<I', ctl, 0x704)[0]; key = hashlib.sha256(bytes(ctl) + bytes(data[:n])).hexdigest()[:16]
    open(os.path.join(d, key + '.ctl'), 'wb').write(ctl); open(os.path.join(d, key + '.data'), 'wb').write(data); return key
def graphics(meta=b'', flip=None):
    ctl = bytearray(0x880); ctl[:4] = bytes.fromhex('34127698'); struct.pack_into('<III', ctl, 0x700, 0x80, 0x100, 0x30); struct.pack_into('<I', ctl, 0x714, 1)
    ctl[0x7d0:0x7d0 + len(meta)] = meta
    data = bytearray((i * 7) % 256 for i in range(0x200)); data[:4] = bytes.fromhex('78563412'); struct.pack_into('<I', data, 0x30, 2 | 5 << 10)
    if flip is not None: data[flip] ^= 1
    return ctl, data
with tempfile.TemporaryDirectory() as d:
    cap = os.path.join(d, 'cap'); os.makedirs(cap)
    a = shader(cap, *graphics()); b = shader(cap, *graphics(b'NVN hash')); c = shader(cap, *graphics(flip=0x90))
    ctl = bytearray(0x880); ctl[:4] = bytes.fromhex('34127698'); struct.pack_into('<III', ctl, 0x700, 0x80, 0x100, 0x0); struct.pack_into('<I', ctl, 0x714, 5)
    struct.pack_into('<III', ctl, 0x748, 8, 4, 1); comp = bytearray((i * 7) % 256 for i in range(0x200)); e = shader(cap, ctl, comp)
    ctl2 = bytearray(ctl); struct.pack_into('<III', ctl2, 0x748, 16, 4, 1); e2 = shader(cap, ctl2, comp)
    ctl3 = bytearray(ctl); struct.pack_into('<III', ctl3, 0x700, 0x80, 0x100, 0x90); g = shader(cap, ctl3, comp)   # start after the constants
    out = os.path.join(d, 'map.tsv')
    env = dict(os.environ)
    src = open(TOOL).read()
    if os.environ.get('T_SHMAP_NO_WORKGROUP'):   # the broken variant: the workgroup not hashed
        src = src.replace("local = struct.unpack_from('<III', ctl, 0x748) if stage == 5 else (0, 0, 0)", "local = (0, 0, 0)")
        tool = os.path.join(d, 'shmap_broken.py'); open(tool, 'w').write(src)
    else: tool = TOOL
    r = subprocess.run([sys.executable, '-I', tool, cap, out], capture_output=True, text=True)
    check('the map tool runs', r.returncode == 0, r.stderr.strip())
    rows = {l.split('\t')[0]: l.rstrip('\n').split('\t') for l in open(out) if not l.startswith('#')} if r.returncode == 0 else {}
    fk = {k: v[2] if len(v) > 2 else None for k, v in rows.items()}
    check('two pixel shaders that differ in control metadata only: two keys, one functional key', a != b and fk.get(a) and fk.get(a) == fk.get(b), str(fk))
    check('... the host\'s value for the same bytes (t_nvn 5c)', fk.get(a) == '7c7333bea0fcafb6', str(fk.get(a)))
    check('a constant byte changed: another functional key', fk.get(c) and fk.get(c) != fk.get(a))
    check('compute shaders with other workgroups: other functional keys (2617563634b77c30 for 8 x 4 x 1, as the host)', fk.get(e) == '2617563634b77c30' and fk.get(e2) and fk.get(e2) != fk.get(e), f'{fk.get(e)} {fk.get(e2)}')
    check('bounds that are not sane (the code starting after the constants): an empty column', fk.get(g) == '' and rows.get(g, [None, None])[1] == 'cs')
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
