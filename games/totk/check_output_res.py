#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's output size code (rules.py option output, plan item 63) in Unicorn, from the product (its options applied) and
# from the original main (negative control):
#  - the swapchain size, main 0xbdb578-0xbdb58c: the product stores the host page +24 at x20 +0x14 (2560x1440 and 1280x720 on the page), x9 (the
#    adrp / add between) as the original's; the original stores 1920x1080 whatever the page holds
#  - the canvas, from 0x3dcb28 through the code cave to 0x3dcb30 (after the crop store), with the display configuration x19 holding a 1920x1080
#    canvas at +0x10 and x3 the size that path lays out: with 2560x1440 at +32 the product makes x3, x4 (the height), the canvas +0x10 and the
#    crop +0x508 that size; with 0 there it leaves x3 (also a 1280x720 one, as bit 20's path brings), the canvas and the crop as the original
#    does; the registers but x3 / x4 / x9-x11 as the original leaves them
# With MacNativeGames' unconditional store in the cave (ldr x3, [x9, #32]; str x3, [x19, #0x10]), 0 on the page makes the canvas 0x0: 2 fail.
# Needs the venv's unicorn and lz4.
# Usage: check_output_res.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP, UC_ARM64_REG_X0

BASE, OBJ, STACK = 0x10000000, 0x80000000, 0x81000000
X = lambda i: UC_ARM64_REG_X0 + i   # (x0-x28 are consecutive in unicorn's arm64 register numbering)
pack = lambda w, h: h << 32 | w
HD, QHD, HD7 = pack(1920, 1080), pack(2560, 1440), pack(1280, 720)

def machine(img, page_off, page):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (max(len(img), (page_off or 0) + 0x4000) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img))
    for a in (OBJ, STACK - 0x10000): uc.mem_map(a, 0x10000)
    if page_off: uc.mem_write(BASE + page_off, page)
    for i in range(29): uc.reg_write(X(i), 0xA0 + i)
    uc.reg_write(UC_ARM64_REG_SP, STACK - 0x100)
    return uc

def swapchain(img, page_off, size):
    uc = machine(img, page_off, struct.pack('<24xQ', size)); uc.reg_write(X(20), OBJ)
    uc.emu_start(BASE + 0xbdb578, BASE + 0xbdb58c)   # (0xbdb58c: mov x8, the first instruction after the store)
    return struct.unpack('<Q', uc.mem_read(OBJ + 0x14, 8))[0], uc.reg_read(X(9)) - BASE

def canvas(img, page_off, canvas_page, x3):
    uc = machine(img, page_off, struct.pack('<32xQ', canvas_page)); uc.reg_write(X(19), OBJ); uc.reg_write(X(3), x3)
    uc.mem_write(OBJ + 0x10, struct.pack('<Q', HD)); uc.mem_write(OBJ + 0x508, struct.pack('<Q', 0x5555))
    uc.emu_start(BASE + 0x3dcb28, BASE + 0x3dcb30)   # (0x3dcb30: adrp x8, after the crop store at 0x3dcb2c)
    q = lambda o: struct.unpack('<Q', uc.mem_read(OBJ + o, 8))[0]
    regs = [uc.reg_read(X(i)) for i in range(29)]
    return (regs[3], regs[4], q(0x10), q(0x508)), [r for i, r in enumerate(regs) if i not in (3, 4, 9, 10, 11)]

n = fail = 0
def expect(what, want, got):
    global n, fail
    n += 1; ok = want == got; fail += not ok
    fmt = lambda v: tuple(f'{x:#x}' for x in v) if isinstance(v, tuple) else f'{v:#x}' if isinstance(v, int) else f'{len(v)} registers' if isinstance(v, list) else str(v)
    print(f'{what:<96} expected {fmt(want)!s:<40} got {fmt(got)!s:<40} {"✓" if ok else "✗"}')

fx = {l.split()[1]: int(l.split()[2], 0) for l in open(os.path.join(sys.argv[1], 'main.fixups.tsv')) if l.startswith('# ')}
prod = nsoimg.product_image(sys.argv[1], 'main')
orig = nsoimg.load(sys.argv[2])
po = fx.get('HOST_PAGE_OFF')
expect('the product has a host page after the key page', fx['KEY_PAGE_OFF'] + 0x4000, po)
ps, px9 = swapchain(prod, po, QHD)
os_, ox9 = swapchain(orig, po, QHD)
expect('product: the swapchain size from the host page +24 (2560x1440)', QHD, ps)
expect('product: another size there (1280x720) is the size stored', HD7, swapchain(prod, po, HD7)[0])
expect('product: x9 (the adrp / add between) as the original\'s', ox9, px9)
expect('original (negative control): 1920x1080, the page ignored', HD, os_)
p, pr = canvas(prod, po, QHD, HD)
o, orr = canvas(orig, po, QHD, HD)
expect('product, +32 = 2560x1440: x3, x4, the canvas +0x10 and the crop +0x508 that size', (QHD, 1440, QHD, QHD), p)
expect('original (negative control): the page ignored, x3 / the canvas 1920x1080', (HD, 1080, HD, HD), o)
expect('product: the other registers as the original leaves them (x9-x11 scratch)', orr, pr)
p0, pr0 = canvas(prod, po, 0, HD7)
o0, or0 = canvas(orig, po, 0, HD7)
expect('product, +32 = 0, x3 = 1280x720 (bit 20\'s path): x3, the canvas 1920x1080 and the crop as the original', o0, p0)
expect('... which are x3 = 1280x720, x4 = 720, the canvas kept, the crop 1280x720', (HD7, 720, HD, HD7), p0)
expect('... the other registers as the original leaves them', or0, pr0)
print(f'== {n} checks, {fail} failed =='); sys.exit(fail != 0)
