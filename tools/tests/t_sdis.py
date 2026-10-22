#!/usr/bin/env python3
"""tools/sdis.py --xref / --callers on a synthetic image (plan item 199, after nxnative 108d483, MIT): the scan reaches the end of the PLT, not only
the end of the last sized export, and an image whose exports are all unsized is scanned instead of stopping on max() of nothing.
  0x100  f, the one sized export (0x10 bytes)
  0x180  adrp x1 / add x1, #0x40 → 0x1040      0x188  bl f      0x200  a PLT stub (adrp x16 / ldr x17 / add x16 / br x17) for "imp"
A second image gives f no size. DM_TEST_SDIS_OLD_END=1 (the sized exports alone, as before): 4 fail (two missed, two stopped).
Needs capstone (the venv: work/build/cache/dm/venv/bin/python)."""
import os, struct, subprocess, sys, tempfile
SDIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sdis.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f"{'ok  ' if ok else 'FAIL'} {name}" + (f'  ({detail})' if detail and not ok else ''))
NOP = 0xD503201F
def ADRP(d, pc, to): imm = (to >> 12) - (pc >> 12); return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | d
def ADDI(d, n, imm): return 0x91000000 | (imm << 10) | (n << 5) | d
def BL(pc, to): return 0x94000000 | (((to - pc) >> 2) & 0x3FFFFFF)
def LDRX(t, n, off): return 0xF9400000 | ((off // 8) << 10) | (n << 5) | t
BR17 = 0xD61F0220
DYN, SYM, JMPREL, GOT = 0x800, 0x900, 0xa00, 0xb00
def image(fsize):
    img = bytearray(0x1100)
    for a in range(0, 0x800, 4): struct.pack_into('<I', img, a, NOP)
    struct.pack_into('<II', img, 0, NOP, 8); img[8:12] = b'MOD0'; struct.pack_into('<i', img, 12, DYN - 8)   # MOD0 + 4: the dynamic section, relative
    struct.pack_into('<II', img, 0x180, ADRP(1, 0x180, 0x1040), ADDI(1, 1, 0x40)); struct.pack_into('<I', img, 0x188, BL(0x188, 0x100))
    struct.pack_into('<IIII', img, 0x200, ADRP(16, 0x200, GOT), LDRX(17, 16, GOT & 0xfff), ADDI(16, 16, GOT & 0xfff), BR17)
    strtab = b'\0f\0imp\0'; STR = SYM + 3 * 24
    struct.pack_into('<IBBHQQ', img, SYM + 24, 1, 0x12, 0, 1, 0x100, fsize)   # f: defined (section 1)
    struct.pack_into('<IBBHQQ', img, SYM + 48, 3, 0x12, 0, 0, 0, 0)           # imp: an import
    img[STR:STR + len(strtab)] = strtab
    struct.pack_into('<QQq', img, JMPREL, GOT, (2 << 32) | 1026, 0)            # R_AARCH64_JUMP_SLOT for symbol 2 at the GOT entry
    o = DYN
    for tag, v in ((6, SYM), (5, STR), (23, JMPREL), (2, 24), (0, 0)): struct.pack_into('<qQ', img, o, tag, v); o += 16
    return bytes(img)
env = dict(os.environ)
with tempfile.TemporaryDirectory() as d:
    for fsize, what in ((0x10, 'f sized 0x10'), (0, 'every export unsized')):
        p = os.path.join(d, 'img.bin'); open(p, 'wb').write(image(fsize))
        x = subprocess.run([sys.executable, '-I', SDIS, p, '--xref', '0x1040'], capture_output=True, text=True, env=env)
        check(f'--xref 0x1040 finds the adrp at 0x180 and its add at 0x184 ({what})', x.returncode == 0 and '0x184' in x.stdout, (x.stdout + x.stderr)[-200:])
        c = subprocess.run([sys.executable, '-I', SDIS, p, '--callers', 'f'], capture_output=True, text=True, env=env)
        check(f'--callers f finds the bl at 0x188 ({what})', c.returncode == 0 and c.stdout.startswith('0x188'), (c.stdout + c.stderr)[-200:])
print(f't_sdis: {total} checks, {fails} failed'); sys.exit(fails != 0)
