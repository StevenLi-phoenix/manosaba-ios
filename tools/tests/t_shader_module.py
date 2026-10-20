#!/usr/bin/env python3
"""engines/bnsh/shader_extract.py module: a synthetic module image with a BNSH file (one program: a vertex and a pixel shader) embedded at an
unaligned-looking offset among other bytes, as Tears of the Kingdom's main carries two. Both shaders are found with the module and the offset as
their source, and their pair is noted; an image without the magic adds nothing. The RomFS walk's visit in place of the scan (by hand: it takes
only files that start with a container magic) fails 3. Run with Python 3.14 (compression.zstd)."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'engines', 'bnsh'))
import shader_extract as se
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<74} {"✓" if ok else "✗"}')
LN = 0x80
def shader(nvn, sph, fill):
    ctl = bytearray(0x880); ctl[0:4] = se.CTL_MAGIC; struct.pack_into('<II', ctl, 0x700, LN, LN); struct.pack_into('<I', ctl, 0x714, nvn)
    code = bytearray([fill]) * LN; code[0:4] = se.CODE_MAGIC; struct.pack_into('<I', code, 0x30, sph)
    return bytes(ctl), bytes(code)
# BNSH: header, one variation record at 0x100, its program record at 0x140, stage records at 0x180 / 0x1c0, control sections and code after
b = bytearray(0x3000)
b[0:8] = b'BNSH\0\0\0\0'; b[12:14] = b'\xff\xfe'; struct.pack_into('<I', b, 28, len(b)); b[96:100] = b'grsc'
struct.pack_into('<IQ', b, 124, 1, 0x100); struct.pack_into('<Q', b, 0x100 + 16, 0x140)
struct.pack_into('<Q', b, 0x140 + 8, 0x180); struct.pack_into('<Q', b, 0x140 + 40, 0x1c0)
for rec, (ctl, code), co, do in ((0x180, shader(0, 1 | 1 << 10, 0x11), 0x200, 0x1400), (0x1c0, shader(1, 2 | 5 << 10, 0x22), 0xb00, 0x1500)):
    struct.pack_into('<QQII', b, rec + 8, co, do, LN, 0x880); b[co:co + 0x880] = ctl; b[do:do + LN] = code
img = bytes(range(256)) * 37 + bytes(b) + b'\x5a' * 0x1000   # the BNSH at 0x2500 among other data
se.recs.clear(); se.src.clear(); se.pairs.clear(); se.skipped.clear()
se.module(img, 'main')
keys = sorted(se.recs)
check(f'the embedded BNSH: two shaders found ({len(keys)})', len(keys) == 2)
check('a vertex and a pixel shader', sorted(se.recs[k][0] for k in keys) == ['ps', 'vs'])
check('their source is the module and the BNSH offset', all(se.src[k] == ['module:main@0x2500'] for k in keys))
check('their pair is noted', len(se.pairs) == 1)
se.recs.clear(); se.src.clear(); se.pairs.clear()
se.module(bytes(range(256)) * 64, 'sdk')
check('an image without the magic adds nothing', not se.recs and not se.pairs)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
