#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's scene render size stores (main 0x585e48-0x585e6c, in 0x585da0) in Unicorn, from the product and from the
# original main (rules.py CUTS + HOSTREFS, plan item 52): with the graphics parameter's size in w11 / w10 (1600x900) and the output size in w9 (1920),
# and 960x540 on the host page +16 / +20. The product stores the page's size at renderer +0xcc4 / +0xcc8; the original stores the parameter's
# (negative control) and ignores the page; both store the output size at +0xcc0 (w9 is left alone) and leave x3 and the argument registers as they
# were. Plan item 240: the product also keeps the renderer (x19) at the host page +48, and 0x580920 (the display state's flags, ldrh w8, [x23, #8])
# gets the page +56 ORed in: 0x80 there makes w8 0x81 from 0x1, 0 leaves it 0x1, and the original ignores the page. Needs the venv's unicorn and lz4.
# Usage: check_render_size.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5,
                                 UC_ARM64_REG_X6, UC_ARM64_REG_X7, UC_ARM64_REG_X8, UC_ARM64_REG_X9, UC_ARM64_REG_X10, UC_ARM64_REG_X11,
                                 UC_ARM64_REG_X19, UC_ARM64_REG_X23, UC_ARM64_REG_SP)

BASE, REND, OBJ, STACK = 0x10000000, 0x80000000, 0x80100000, 0x81000000
START, END = 0x585e48, 0x585e6c   # (0x585e6c: ldr x8, [x8, #0x150], the first instruction after the stores)
ARGS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6, UC_ARM64_REG_X7)

def run(img, page_off, page_size):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (max(len(img), (page_off or 0) + 0x4000) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img))
    for a in (REND, OBJ, STACK - 0x10000): uc.mem_map(a, 0x10000)
    if page_off: uc.mem_write(BASE + page_off + 16, struct.pack('<ff', *page_size))
    uc.mem_write(OBJ + 0x40, struct.pack('<i', 7))           # ldr w2, [x8, #0x40]
    uc.mem_write(REND + 0xc38, struct.pack('<Q', OBJ))       # ldr x8, [x19, #0xc38]
    uc.mem_write(REND + 0x9f0, struct.pack('<Q', 0x1234))    # ldr x0, [x19, #0x9f0]
    for i, r in enumerate(ARGS): uc.reg_write(r, 0xA0 + i)
    uc.reg_write(UC_ARM64_REG_X8, OBJ); uc.reg_write(UC_ARM64_REG_X19, REND); uc.reg_write(UC_ARM64_REG_SP, STACK - 0x100)
    uc.reg_write(UC_ARM64_REG_X11, 1600); uc.reg_write(UC_ARM64_REG_X10, 900); uc.reg_write(UC_ARM64_REG_X9, 1920)
    uc.emu_start(BASE + START, BASE + END)
    f = lambda o: struct.unpack('<f', uc.mem_read(REND + o, 4))[0]
    regs = [uc.reg_read(r) for r in ARGS]
    run.kept = struct.unpack('<Q', uc.mem_read(BASE + page_off + 48, 8))[0] if page_off else None
    return (f(0xcc4), f(0xcc8), f(0xcc0)), regs, uc.reg_read(UC_ARM64_REG_X9)
def flags(img, page_off, kick):   # 0x580920 → 0x580924 with the display state's flags 0x1 at x23 +8 and kick at the page +56: w8
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (max(len(img), page_off + 0x4000) + 0xFFFF) & ~0xFFFF); uc.mem_write(BASE, bytes(img)); uc.mem_map(OBJ, 0x10000)
    uc.mem_write(BASE + page_off + 56, struct.pack('<I', kick)); uc.mem_write(OBJ + 8, struct.pack('<H', 1))
    uc.reg_write(UC_ARM64_REG_X23, OBJ); uc.emu_start(BASE + 0x580920, BASE + 0x580924)
    return uc.reg_read(UC_ARM64_REG_X8) & 0xFFFFFFFF

n = fail = 0
def expect(what, want, got):
    global n, fail
    n += 1; ok = want == got; fail += not ok
    print(f'{what:<92} expected {want!s:<26} got {got!s:<26} {"✓" if ok else "✗"}')

fx = {l.split()[1]: int(l.split()[2], 0) for l in open(os.path.join(sys.argv[1], 'main.fixups.tsv')) if l.startswith('# ')}
prod = nsoimg.product_image(sys.argv[1], 'main')   # (its options applied: rules.py OPTIONS)
orig = nsoimg.load(sys.argv[2])
expect('the product has a host page after the key page', fx['KEY_PAGE_OFF'] + 0x4000, fx.get('HOST_PAGE_OFF'))
p, pr, p9 = run(prod, fx['HOST_PAGE_OFF'], (960.0, 540.0))
o, orr, o9 = run(orig, fx['HOST_PAGE_OFF'], (960.0, 540.0))
expect('product: the scene size from the host page (renderer +0xcc4 / +0xcc8)', (960.0, 540.0), p[:2])
expect('product: the output size from w9 at +0xcc0, as before', 1920.0, p[2])
expect('original (negative control): the parameter\'s size, the page ignored', (1600.0, 900.0), o[:2])
expect('original: the same output size', 1920.0, o[2])
expect('product: x9 left alone', 1920, p9)
expect('product: x3 and the other argument registers but x0 / x2 as the original leaves them (x0 / x2 are loaded there)',
       [r for i, r in enumerate(orr) if i not in (0, 2)], [r for i, r in enumerate(pr) if i not in (0, 2)])
p2, _, _ = run(prod, fx['HOST_PAGE_OFF'], (1280.0, 720.0))
expect('product: another size on the page is the size stored', (1280.0, 720.0), p2[:2])
expect('240 product: the renderer (x19) kept at the host page +48', hex(REND), hex(run.kept or 0))
run(orig, fx['HOST_PAGE_OFF'], (960.0, 540.0))
expect('240 original (negative control): nothing at +48', '0x0', hex(run.kept or 0))
expect('240 product: 0x580920 with 0x80 at the page +56: flags 0x1 | 0x80', hex(0x81), hex(flags(prod, fx['HOST_PAGE_OFF'], 0x80)))
expect('240 product: with 0 there the flags as loaded', hex(0x1), hex(flags(prod, fx['HOST_PAGE_OFF'], 0)))
expect('240 original (negative control): the page ignored', hex(0x1), hex(flags(orig, fx['HOST_PAGE_OFF'], 0x80)))
print(f'== {n} checks, {fail} failed =='); sys.exit(fail != 0)
