#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's four aspect ratio sites (rules.py option aspect, plan item 154) in Unicorn, each from its instruction to the one
# after it (through the code cave), from the product (its options applied) and from the original main (negative control):
#  - the camera update 0x628c74 (the ratio at x19 +0x140) and setup 0xb43460 (at x22 +0x68) loading 16:9, the viewport routines 0x1ff04f4 /
#    0x253fbb0 dividing 1920 by 1080
#  - with 0 at the host page +40 (off) the product leaves s0 as the original does; with 1.3125 there (21:9 over 16:9) s0 is the original's × 1.3125,
#    rounded once as a float multiply; the original ignores the page; the product without the option aspect applied ignores it too
#  - x0-x28, sp, the whole of q16 and the flags as the original leaves them (the routine's scratch is restored)
# Multiplying by the page as it is, without taking 0 as 1 (the four routines' words from neg w11 to orr w10 made nops): the four "off" cases fail.
# Needs the venv's unicorn and lz4.
# Usage: check_aspect.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_Q16, UC_ARM64_REG_NZCV

BASE, OBJ, STACK = 0x10000000, 0x80000000, 0x81000000
X = lambda i: UC_ARM64_REG_X0 + i   # (x0-x28 are consecutive in unicorn's arm64 register numbering)
f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
bits = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
Q16, FLAGS = 0x0123456789abcdef_fedcba9876543210, 0x60000000   # (Z and C set)
SITES = [(0x628c74, 'camera update', 19, 0x140), (0xb43460, 'camera setup', 22, 0x68), (0x1ff04f4, 'viewport', None, None), (0x253fbb0, 'viewport', None, None)]

def run(img, page_off, site, factor):
    off, _, reg, field = site
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (max(len(img), page_off + 0x4000) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img))
    for a in (OBJ, STACK - 0x10000): uc.mem_map(a, 0x10000)
    uc.mem_write(BASE + page_off, struct.pack('<40xf', factor))
    for i in range(29): uc.reg_write(X(i), 0xA0 + i)
    uc.reg_write(UC_ARM64_REG_SP, STACK - 0x100)
    uc.reg_write(UC_ARM64_REG_Q16, Q16); uc.reg_write(UC_ARM64_REG_NZCV, FLAGS)
    if reg is not None: uc.reg_write(X(reg), OBJ); uc.mem_write(OBJ + field, struct.pack('<f', 16 / 9))
    else: uc.reg_write(UC_ARM64_REG_S0, bits(1920.0)); uc.reg_write(UC_ARM64_REG_S1, bits(1080.0))
    uc.emu_start(BASE + off, BASE + off + 4)
    s0 = struct.unpack('<f', struct.pack('<I', uc.reg_read(UC_ARM64_REG_S0) & 0xffffffff))[0]
    regs = [uc.reg_read(X(i)) for i in range(29)] + [uc.reg_read(UC_ARM64_REG_SP), uc.reg_read(UC_ARM64_REG_Q16), uc.reg_read(UC_ARM64_REG_NZCV)]
    return s0, regs

n = fail = 0
def expect(what, want, got):
    global n, fail
    n += 1; ok = want == got; fail += not ok
    fmt = lambda v: f'{len(v)} registers' if isinstance(v, list) else f'{v!r}'
    print(f'{what:<96} expected {fmt(want):<22} got {fmt(got):<22} {"✓" if ok else "✗"}')

fx = {l.split()[1]: int(l.split()[2], 0) for l in open(os.path.join(sys.argv[1], 'main.fixups.tsv')) if l.startswith('# ')}
po = fx['HOST_PAGE_OFF']
prod = nsoimg.product_image(sys.argv[1], 'main')
opts = sorted({l.split('\t')[0] for l in open(os.path.join(sys.argv[1], 'main.options.tsv')) if not l.startswith('#')})
expect('the product lists the option aspect', True, 'aspect' in opts)
plain = nsoimg.product_image(sys.argv[1], 'main', [o for o in opts if o != 'aspect'])
orig = nsoimg.load(sys.argv[2])
WIDE = (21 / 9) / (16 / 9)   # 1.3125
for site in SITES:
    name = f'{site[1]} {site[0]:#x}'
    o0, oregs = run(orig, po, site, 0.0)
    ow, _ = run(orig, po, site, WIDE)
    p0, pregs0 = run(prod, po, site, 0.0)
    pw, pregsw = run(prod, po, site, WIDE)
    nw, _ = run(plain, po, site, WIDE)
    expect(f'{name}: the original\'s ratio (16:9)', f32(16 / 9), o0)
    expect(f'{name}: product, +40 = 0 (off): the ratio as the original\'s', o0, p0)
    expect(f'{name}: product, +40 = 1.3125: the ratio x 1.3125 (21:9)', f32(o0 * f32(WIDE)), pw)
    expect(f'{name}: original (negative control) and the product without the option: the page ignored', (o0, o0), (ow, nw))
    expect(f'{name}: product: x0-x28, sp, q16 and the flags as the original leaves them (+40 = 0)', oregs, pregs0)
    expect(f'{name}: ... and with +40 = 1.3125', oregs, pregsw)
print(f'== {n} checks, {fail} failed =='); sys.exit(fail != 0)
