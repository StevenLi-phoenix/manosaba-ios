#!/usr/bin/env python3
"""tools/new_game.py's keys from the files (plan item 64n): a synthetic main.npdm (META SystemResourceSize 0x1800000, ACI0 program ID, a ThreadInfo
capability of priorities 28-59 among others) gives its program ID, size and [highest, lowest]; --check against both games' hand-made profiles agrees
when their dumps are under DM_ROOT (default the repository's work/, else skipped). The priorities read the wrong way round
(T_NEW_GAME_PRIO_SWAPPED=1, the broken variant): 1 fails, 2 with the dumps."""
import os, struct, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools')); import new_game as ng
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
caps = [0x0000003f, 0x7 | 59 << 4 | 28 << 10 | 3 << 16 | 3 << 24, 0xffffffff]   # another capability, ThreadInfo, a padding word
aci = 0x80; ko = 0x40
d = bytearray(aci + ko + 4 * len(caps)); d[0:4] = b'META'; struct.pack_into('<I', d, 0x14, 0x1800000); struct.pack_into('<I', d, 0x70, aci)
d[aci:aci + 4] = b'ACI0'; struct.pack_into('<Q', d, aci + 0x10, 0x0100F2C0115B6000); struct.pack_into('<II', d, aci + 0x30, ko, 4 * len(caps))
for i, w in enumerate(caps): struct.pack_into('<I', d, aci + ko + 4 * i, w)
with tempfile.TemporaryDirectory() as t:
    p = os.path.join(t, 'main.npdm'); open(p, 'wb').write(d)
    f = ng.npdm_fields(p)
    check('synthetic NPDM: program 0100F2C0115B6000, system resource 0x1800000, priorities [28, 59]',
          ng.program_id(p) == '0100F2C0115B6000' and f == {'system_resource_size': '0x1800000', 'thread_priority': [28, 59]}, f)
env = dict(os.environ); env.setdefault('DM_ROOT', os.path.join(ROOT, 'work'))
r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'new_game.py'), '--check'], env=env, capture_output=True, text=True)
print(r.stdout.rstrip())
if 'agree' in r.stdout or r.returncode:
    check('--check: the keys the files decide agree with every profile whose dump is there', r.returncode == 0, r.stdout + r.stderr)
else: print('--check: no dump under DM_ROOT, skipped')
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
