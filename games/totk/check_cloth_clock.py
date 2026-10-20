#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's cloth and hair clock (rules.py CAVES, plan item 163) in Unicorn after MacNativeGames PR #79's table (MIT; its
# cases and expected values as data, the code ours): the two inlined copies from the start of the code that builds the clock to the end of its
# stores, and the two calls from the call to the update's entry and back, at 15 to 120 fps and time factors 0, 0.25, 1 and 2. The host page
# holds the step as games/totk/frame_step.c writes it (+0 ratio = seconds × 30, +4 ticks, +8 seconds). With the option realtime the clock must
# be {factor / fps, factor × 30 / fps}; the ordinary clock beside it, the time factor, the stack, the flags and a live register stay as they were.
# Negative controls: the original main and the product without realtime both give the game's own clock, 4 seconds per second at 120 fps at all
# four sites; the update's third caller (0x2afac18, measured time already) is left alone. Needs the venv's unicorn and lz4.
# Broken variants: the page's +8 left at 0 (DM_TEST_CLOTH_NO_SECONDS=1, as frame_step.c's negative control): 1 fails; a product from before
# plan item 163 (no caves): 2 fail.
# Usage: check_cloth_clock.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import (UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_X9, UC_ARM64_REG_NZCV, UC_ARM64_REG_S0,
                                 UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_S3, UC_ARM64_REG_S8, UC_ARM64_REG_S9, UC_ARM64_REG_S10)

UPDATE = 0x2a0e40   # the out-of-line update the two calls pass the clock to
SITES = (0x246eac, 0x345904, 0x24944c, 0x28bc98)
BASE, STACK = 0x10000000, 0x90000000
f2b = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]

def clock(img, host_page, hz, factor, site):
    """The clock a site builds at hz with the time factor: (seconds, ratio), the ordinary clock beside it, and what else must stay"""
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (len(img) + 0xFFFF) & ~0xFFFF); uc.mem_write(BASE, bytes(img))
    uc.mem_map(STACK - 0x10000, 0x20000); sp = STACK; uc.reg_write(UC_ARM64_REG_SP, sp)
    if host_page: uc.mem_write(BASE + host_page, struct.pack('<fff', 30 / hz, 60 / hz, 0 if os.environ.get('DM_TEST_CLOTH_NO_SECONDS') else 1 / hz))
    put = lambda r, v: uc.reg_write(r, f2b(v))
    get = lambda r: struct.unpack('<f', struct.pack('<I', uc.reg_read(r) & 0xFFFFFFFF))[0]
    ordinary = (.7 / hz, .7 * 30 / hz)   # already real time (the frame step): must not be scaled again
    uc.reg_write(UC_ARM64_REG_X9, 0x12345678); uc.reg_write(UC_ARM64_REG_NZCV, 0xa0000000)
    kept = []
    if site == 0x246eac:   # 0x246e78: s0 = 1/30, the ordinary clock from sp+0x138; ratio = s10 to sp+0x364, seconds to sp+0x360 at 0x246eb8
        put(UC_ARM64_REG_S10, factor); uc.mem_write(sp + 0x138, struct.pack('<ff', ordinary[1], ordinary[0]))
        uc.emu_start(BASE + 0x246e78, BASE + 0x246ebc, count=100)
        res = struct.unpack('<ff', uc.mem_read(sp + 0x360, 8)); normal = struct.unpack('<ff', uc.mem_read(sp + 0x2e0, 8))
        kept += [('the time factor (s10)', abs(get(UC_ARM64_REG_S10) - factor) < 1e-6), ('x9', uc.reg_read(UC_ARM64_REG_X9) == 0x12345678)]
        back = uc.reg_read(UC_ARM64_REG_PC) == BASE + 0x246ebc
    elif site == 0x345904:   # 0x3458d8: s1 = 1/30, factor s0; ratio to sp+0x114, seconds to sp+0x110 at 0x34590c; the ordinary clock s9 / s8 to sp+0x90
        put(UC_ARM64_REG_S0, factor); put(UC_ARM64_REG_S9, ordinary[0]); put(UC_ARM64_REG_S8, ordinary[1])
        uc.emu_start(BASE + 0x3458d8, BASE + 0x345910, count=100)
        res = struct.unpack('<ff', uc.mem_read(sp + 0x110, 8)); normal = struct.unpack('<ff', uc.mem_read(sp + 0x90, 8))
        kept += [('the time factor (s0)', abs(get(UC_ARM64_REG_S0) - factor) < 1e-6)]   # (w9 is loaded there)
        back = uc.reg_read(UC_ARM64_REG_PC) == BASE + 0x345910
    else:   # the call: s0 / s1 the ordinary clock, s2 = factor / 30, s3 = factor; stopped at the update's entry, then returned from it
        put(UC_ARM64_REG_S0, ordinary[0]); put(UC_ARM64_REG_S1, ordinary[1]); put(UC_ARM64_REG_S2, factor / 30); put(UC_ARM64_REG_S3, factor)
        uc.emu_start(BASE + site, BASE + UPDATE, count=100)
        at_update = uc.reg_read(UC_ARM64_REG_PC) == BASE + UPDATE
        res = (get(UC_ARM64_REG_S2), get(UC_ARM64_REG_S3)); normal = (get(UC_ARM64_REG_S0), get(UC_ARM64_REG_S1))
        kept += [('x9', uc.reg_read(UC_ARM64_REG_X9) == 0x12345678), ('sp at the update', uc.reg_read(UC_ARM64_REG_SP) == sp)]
        uc.emu_start(uc.reg_read(UC_ARM64_REG_LR), BASE + site + 4, count=10)   # the update returns: execution goes on after the call
        back = at_update and uc.reg_read(UC_ARM64_REG_PC) == BASE + site + 4
    kept += [('back in the game\'s code', back), ('sp', uc.reg_read(UC_ARM64_REG_SP) == sp), ('the flags', uc.reg_read(UC_ARM64_REG_NZCV) == 0xa0000000),
             ('the ordinary clock', all(abs(a - b) < 1e-6 for a, b in zip(normal, ordinary)))]
    return res, kept

n = fail = 0
def expect(what, ok, detail=''):
    global n, fail
    n += 1; fail += not ok
    print(f'{what:<100} {"✓" if ok else "✗"}' + (f'  ({detail})' if detail and not ok else ''))

prod_dir = sys.argv[1]
fx = {l.split('\t')[0][2:]: l.split('\t')[1].strip() for l in open(os.path.join(prod_dir, 'main.fixups.tsv')) if l.startswith('# ')}
hp = int(fx['HOST_PAGE_OFF'], 16)
opts = {l.split('\t')[0] for l in open(os.path.join(prod_dir, 'main.options.tsv')) if not l.startswith('#')}
prod = nsoimg.product_image(prod_dir, 'main', ['realtime'])
off = nsoimg.product_image(prod_dir, 'main', sorted(opts - {'realtime'}))
orig = nsoimg.load(sys.argv[2]); orig = orig[0] if isinstance(orig, tuple) else orig
raw = open(os.path.join(prod_dir, 'main.patched.bin'), 'rb').read()
expect('the product keeps the four words; realtime makes them branches into the patch area',
       all(raw[s:s + 4] == orig[s:s + 4] and prod[s:s + 4] != orig[s:s + 4] and struct.unpack_from('<I', prod, s)[0] >> 26 == 5 for s in SITES))
cases = bad = 0; first_bad = ''
for hz in (15, 20, 30, 40, 50, 60, 90, 100, 120):
    for factor in (0, .25, 1, 2):
        for site in SITES:
            (sec, ratio), kept = clock(prod, hp, hz, factor, site); cases += 1
            ok = abs(sec - factor / hz) < 1e-6 and abs(ratio - factor * 30 / hz) < 1e-6 and all(k for _, k in kept)
            if not ok:
                bad += 1; first_bad = first_bad or f'{hz} fps, factor {factor}, {site:#x}: {sec:.6f} s, ratio {ratio:.6f}; not kept: {[w for w, k in kept if not k]}'
expect(f'realtime: {cases} cases (15-120 fps x factors 0, .25, 1, 2 x 4 sites) give {{factor / fps, factor x 30 / fps}}, the rest kept', bad == 0,
       f'{bad} wrong, first {first_bad}')
for name, img in (('the original main', orig), ('the product without realtime', off)):
    res = [clock(img, hp if img is off else 0, 120, 1, s) for s in SITES]
    expect(f'{name}: the game\'s own clock, 4 seconds per second at 120 fps at all four sites',
           all(abs(sec * 120 - 4) < 1e-5 and abs(ratio - 1) < 1e-6 and all(k for _, k in kept) for (sec, ratio), kept in res),
           ', '.join(f'{sec * 120:.3f}' for (sec, _), _ in res))
expect('the update\'s third caller (0x2afac18, measured time already) is left alone', prod[0x2afac18:0x2afac1c] == orig[0x2afac18:0x2afac1c])
print(f'== {n} checks, {fail} failed ==')
sys.exit(fail != 0)
