#!/usr/bin/env python3
"""games/totk/heapdef.py: the heap growth for a scene size (0 up to the game's 1600x900; 1920x1080 gives totk-mobile's 163,840 / 251,904 KiB) and
the patch of a synthetic HeapDef BYML (v7, Heaps -> DynamicTextureAllocatorMem / Graphics -> Size): only the two sizes' eight bytes change, by
the same amount, found by name wherever they sit; other sizes than 1.4.3's are refused. 1.4.3's fixed offsets (2732 / 2920, totk-mobile's) in place
of the lookup by name (by hand) end the test in an error. The output size (--output, plan item 63): its growth against the game's 1920x1080, the
larger of the two taken, the notes .dm_scene / .dm_output written (and an earlier .dm_output removed without --output), through main() on a
synthetic RomFS; the output's growth left out of main() (by hand) fails 1. Run with Python 3.14 (compression.zstd)."""
import os, struct, sys, tempfile
import compression.zstd as zstd
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'games', 'totk'))
import heapdef as hd
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<74} {"✓" if ok else "✗"}')
def byml(dta, gfx, other_first=False):
    """A BYML v7: root {Heaps: {[Other: {Size}], DynamicTextureAllocatorMem: {Size}, Graphics: {Size}}}"""
    keys = sorted(['DynamicTextureAllocatorMem', 'Graphics', 'Heaps', 'Other', 'Size'])
    k = {s: i for i, s in enumerate(keys)}
    kt = bytearray(); offs = []; strs = bytearray()
    base = 4 + 4 * (len(keys) + 1)
    for s in keys: offs.append(base + len(strs)); strs += s.encode() + b'\0'
    offs.append(base + len(strs))
    kt = struct.pack('<I', 0xc2 | len(keys) << 8) + struct.pack(f'<{len(offs)}I', *offs) + bytes(strs)
    kt += b'\0' * (-len(kt) % 4)
    hdr = 16; kt_off = hdr
    nodes = bytearray(); start = kt_off + len(kt)
    def hashnode(entries):   # entries: [(key, type, value)] sorted by key
        nonlocal nodes
        at = start + len(nodes)
        nodes += struct.pack('<I', 0xc1 | len(entries) << 8)
        for key, t, v in entries: nodes += struct.pack('<I', k[key] | t << 24) + struct.pack('<i' if t == 0xd1 else '<I', v)
        return at
    sizes = {name: hashnode([('Size', 0xd1, val)]) for name, val in (('Other', 777), ('DynamicTextureAllocatorMem', dta), ('Graphics', gfx))}
    heaps = hashnode(sorted([(n, 0xc1, sizes[n]) for n in (['Other'] if other_first else []) + ['DynamicTextureAllocatorMem', 'Graphics']]))
    root = hashnode([('Heaps', 0xc1, heaps)])
    return bytes(b'YB' + struct.pack('<H', 7) + struct.pack('<III', kt_off, 0, root) + kt + nodes)
check('the game\'s own 1600x900: no growth', hd.extra_kib(1600, 900) == 0)
check('960x540: no growth', hd.extra_kib(960, 540) == 0)
check('1920x1080: +50,176 KiB (totk-mobile\'s 163,840 / 251,904)', hd.extra_kib(1920, 1080) == 50176 and 113664 + 50176 == 163840 and 201728 + 50176 == 251904)
check('2560x1440: whole MiB', hd.extra_kib(2560, 1440) % 1024 == 0 and hd.extra_kib(2560, 1440) > 50176)
for other in (False, True):
    b = byml(113664, 201728, other); p = hd.patch(b, 50176)
    diff = [i for i in range(len(b)) if b[i] != p[i]]; a, g = hd.size_fields(p)
    check(f'patched{" (another heap first)" if other else ""}: the sizes 163,840 / 251,904 KiB', struct.unpack_from('<i', p, a)[0] == 163840 and struct.unpack_from('<i', p, g)[0] == 251904)
    check(f'patched{" (another heap first)" if other else ""}: only the two sizes\' bytes changed', diff and all(a <= i < a + 4 or g <= i < g + 4 for i in diff))
try: hd.patch(byml(100000, 201728), 50176); refused = False
except ValueError: refused = True
check('another version\'s sizes: refused', refused)
check('output 1920x1080 (the game\'s own): no growth', hd.extra_output_kib(1920, 1080) == 0)
check('output 2560x1440: +89,088 KiB (MacNativeGames\' formula: 113,664 KiB scaled by 2560x1440 / 1920x1080)', hd.extra_output_kib(2560, 1440) == 89088)
with tempfile.TemporaryDirectory() as td:
    romfs, out = os.path.join(td, 'romfs'), os.path.join(td, 'overlay')
    os.makedirs(os.path.dirname(os.path.join(romfs, hd.HEAPDEF)))
    with open(os.path.join(romfs, hd.HEAPDEF), 'wb') as f: f.write(zstd.compress(byml(113664, 201728)))
    stem = os.path.join(out, hd.HEAPDEF)[:-len('.byml.zs')]
    def run(*argv):
        sys.argv = ['heapdef.py', romfs, out, *argv]; hd.main()
        b = zstd.decompress(open(os.path.join(out, hd.HEAPDEF), 'rb').read()); a, g = hd.size_fields(b)
        note = lambda e: open(stem + e).read().strip() if os.path.exists(stem + e) else None
        return struct.unpack_from('<i', b, a)[0], struct.unpack_from('<i', b, g)[0], note('.dm_scene'), note('.dm_output')
    r = run('1600x900', '--output', '2560x1440')
    check('scene 1600x900, output 2560x1440: the output\'s growth, notes 1600x900 / 2560x1440', r == (113664 + 89088, 201728 + 89088, '1600x900', '2560x1440'))
    r = run('2560x1440', '--output', '2560x1440')
    check('scene and output 2560x1440: the larger growth (the scene\'s, +178,176 KiB)', r == (113664 + 178176, 201728 + 178176, '2560x1440', '2560x1440'))
    r = run('1920x1080')
    check('scene 1920x1080 alone: its growth, the earlier .dm_output removed', r == (163840, 251904, '1920x1080', None))
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
