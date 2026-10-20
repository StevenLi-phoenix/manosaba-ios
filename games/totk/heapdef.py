#!/usr/bin/env python3
# The RomFS overlay a scene render size above the game's own needs (DM_RENDER_RES, games/totk/frame_step.c; plan item 95). The scene's render targets
# come from agl's DynamicTextureAllocator, one fixed heap that cannot grow: at 2560x1440 the game ran out of it within seconds (main+0x1c2f20 reads the
# null block). Its size and the size of the Graphics heap it is carved from are data, System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs, "Heaps" ->
# name -> "Size" in KiB (the layout and the lookup by name as MacNativeGames' scripts/make_output_res_overlay.py reads them, ly05010419/MacNativeGames,
# MIT; the code is ours). Both grow by extra_kib(): the shipped 113,664 KiB scaled by the scene's pixels over the game's docked 1600x900, rounded up
# to whole MiB. For 1920x1080 that gives totk-mobile's values (OJZen/totk-mobile prepare_render_resources.py: 163,840 and 251,904 KiB). The host's
# application pool already has 512 MiB over the console's (hle_svc.c), which covers these up to 3840x2160.
# The output size (DM_OUTPUT_RES, the canvas the game composes its frame in, plan item 63) draws its canvas targets from the same heap: --output
# scales it by the output's pixels over the game's 1920x1080 instead (MacNativeGames' make_output_res_overlay.py, same formula), and with both the
# larger growth is taken (each part of the heap grows by at most the larger ratio).
# Usage: heapdef.py <RomFS dir> <overlay dir> <scene width>x<height> [--output <width>x<height>]
#   → <overlay dir>/System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs (DM_ROMFS_OVERLAY) and beside it .dm_scene, the scene size, up to which
#     frame_step.c takes DM_RENDER_RES, and .dm_output, the output size, up to which it takes DM_OUTPUT_RES (1600x900 as the scene: the output only)
# Python 3.14 (compression.zstd), no other packages. Unit test games/totk/tests/t_heapdef.py
import math, os, struct, sys
import compression.zstd as zstd

HEAPDEF = 'System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs'
DTA_KIB, GRAPHICS_KIB = 113664, 201728   # the shipped sizes (1.4.3), checked
BASE_W, BASE_H = 1600, 900                # the game's own docked scene size
OUT_W, OUT_H = 1920, 1080                 # the game's own output (canvas) size

def extra_kib(w, h):
    """KiB both heaps grow by for a w x h scene (0 up to the game's own size)"""
    ratio = w * h / (BASE_W * BASE_H)
    return 0 if ratio <= 1 else math.ceil(DTA_KIB * ratio / 1024) * 1024 - DTA_KIB

def extra_output_kib(w, h):
    """KiB both heaps grow by for a w x h output (0 up to the game's own 1920x1080)"""
    ratio = w * h / (OUT_W * OUT_H)
    return 0 if ratio <= 1 else math.ceil(DTA_KIB * ratio / 1024) * 1024 - DTA_KIB

def size_fields(byml):
    """Offsets of the two Size values (BYML v7 little endian: hash nodes 0xC1 of {u24 key index, u8 type, u32 value}, ints 0xD1 inline)"""
    if byml[:2] != b'YB' or struct.unpack_from('<H', byml, 2)[0] != 7: raise ValueError('not a little-endian BYML v7')
    key_table, _, root = struct.unpack_from('<III', byml, 4)
    n = struct.unpack_from('<I', byml, key_table)[0] >> 8
    offs = struct.unpack_from(f'<{n}I', byml, key_table + 4)
    keys = [byml[key_table + o:byml.index(0, key_table + o)].decode() for o in offs]
    def field(node, name, want):
        if byml[node] != 0xc1: raise ValueError(f'node at {node:#x} is not a hash')
        for i in range(struct.unpack_from('<I', byml, node)[0] >> 8):
            e = node + 4 + 8 * i
            if keys[struct.unpack_from('<I', byml, e)[0] & 0xffffff] == name:
                if byml[e + 3] != want: raise ValueError(f'{name}: type {byml[e + 3]:#x}, expected {want:#x}')
                return e + 4
        raise KeyError(name)
    child = lambda node, name: struct.unpack_from('<I', byml, field(node, name, 0xc1))[0]
    heaps = child(root, 'Heaps')
    return field(child(heaps, 'DynamicTextureAllocatorMem'), 'Size', 0xd1), field(child(heaps, 'Graphics'), 'Size', 0xd1)

def patch(byml, extra):
    """The BYML with both sizes grown by extra KiB; only those eight bytes differ"""
    out = bytearray(byml); a, g = size_fields(out)
    dta, gfx = struct.unpack_from('<i', out, a)[0], struct.unpack_from('<i', out, g)[0]
    if (dta, gfx) != (DTA_KIB, GRAPHICS_KIB): raise ValueError(f'unexpected heap sizes {dta} / {gfx} KiB (made for 1.4.3)')
    struct.pack_into('<i', out, a, dta + extra); struct.pack_into('<i', out, g, gfx + extra)
    return bytes(out)

def parse_size(v, lo_w, lo_h):
    w, h = (int(x) for x in v.lower().split('x'))
    if not (lo_w <= w <= 3840 and lo_h <= h <= 2160): sys.exit(f'[red] {v}: the size must be from {lo_w}x{lo_h} up to 3840x2160')
    return w, h

def main():
    args = sys.argv[1:]; output = None
    if '--output' in args:
        i = args.index('--output'); output = parse_size(args[i + 1], 640, 360); del args[i:i + 2]
    romfs, out, size = args
    w, h = parse_size(size, 64, 64)
    extra = max(extra_kib(w, h), extra_output_kib(*output) if output else 0)
    if not extra: sys.exit(f'scene {w}x{h}{f" and output {output[0]}x{output[1]}" if output else ""}: not above the game\'s own 1600x900 / 1920x1080, no overlay needed')
    raw = open(os.path.join(romfs, HEAPDEF), 'rb').read()
    info = zstd.get_frame_info(raw)
    if info.dictionary_id: sys.exit('[red] the HeapDef is compressed with a dictionary: not this version\'s')
    data = zstd.compress(patch(zstd.decompress(raw), extra), level=19)
    path = os.path.join(out, HEAPDEF); os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f: f.write(data)
    stem = path[:-len('.byml.zs')]   # the notes beside it: what frame_step.c then allows (the game reads no such file)
    with open(stem + '.dm_scene', 'w') as f: f.write(f'{w}x{h}\n')
    if output:
        with open(stem + '.dm_output', 'w') as f: f.write(f'{output[0]}x{output[1]}\n')
    elif os.path.exists(stem + '.dm_output'): os.remove(stem + '.dm_output')   # (an earlier overlay's)
    print(f'{path}: DynamicTextureAllocatorMem {DTA_KIB} → {DTA_KIB + extra} KiB, Graphics {GRAPHICS_KIB} → {GRAPHICS_KIB + extra} KiB '
          f'(scene {w}x{h}{f", output {output[0]}x{output[1]}" if output else ""})')

if __name__ == '__main__':
    main()
