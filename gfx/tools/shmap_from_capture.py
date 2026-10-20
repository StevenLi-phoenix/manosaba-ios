#!/usr/bin/env python3
# Capture directory (<key>.ctl / <key>.data: the host's DM_SHDUMP or an engine extractor) → the shader stage map the host loads (DM_NVN_SHMAP):
# one "<key>\t<stage>" line per shader, stage = vs / ps / cs. Stage detection as in dmsh_build.py: data starting with the code area marker 78563412
# is graphics and its first SPH word (+0x30) gives vertex / pixel (type bits 0–4, shader type bits 10–13); anything else is compute.
# Key = the first 16 hex digits of sha256(control section + code area), the code area being the length at control +0x704 (as the host's shader_key
# in hle_nvn.c; the control section alone does not tell shaders apart). A third column holds the functional key (plan item 169, Touchable's
# internal/tools/shader_keys.py, MIT, written anew; the host's shader_fkey): the first 16 hex digits of sha256 of {stage (control +0x714), the constants'
# start (+0x700) - the code's start (+0x708), the end (+0x704) - the constants' start, a compute shader's workgroup (+0x748)} as six u32 and the code
# from its start to its end; empty when those bounds are not sane. The host uses it for a key that is not in the map (nvn_mtl.m shmap_resolve).
# Usage: shmap_from_capture.py <capture dir> <output tsv>
import hashlib, os, struct, sys

def fkey(ctl, data):
    cst, end, start = struct.unpack_from('<III', ctl, 0x700); stage = struct.unpack_from('<I', ctl, 0x714)[0]
    if stage not in (0, 1, 5) or not start < cst <= end <= len(data): return ''
    local = struct.unpack_from('<III', ctl, 0x748) if stage == 5 else (0, 0, 0)
    return hashlib.sha256(struct.pack('<6I', stage, cst - start, end - cst, *local) + data[start:end]).hexdigest()[:16]

cap, out = sys.argv[1:3]
rows = []
for fn in sorted(os.listdir(cap)):
    if not fn.endswith('.ctl'): continue
    key = fn[:-4]
    ctl = open(os.path.join(cap, fn), 'rb').read(); data = open(os.path.join(cap, key + '.data'), 'rb').read()
    n = struct.unpack_from('<I', ctl, 0x704)[0]
    if len(data) < n or hashlib.sha256(ctl + data[:n]).hexdigest()[:16] != key: sys.exit(f'[red] {key}: the hash of control section + code area does not match the name')
    if data[:4] == bytes.fromhex('78563412'):
        w0 = struct.unpack_from('<I', data, 0x30)[0]
        st = {(1, 1): 'vs', (2, 5): 'ps'}.get((w0 & 0x1f, (w0 >> 10) & 0xf))
        if not st: sys.exit(f'[red] {key}: first SPH word {w0:#x} is neither vertex nor pixel')
    else: st = 'cs'
    rows.append(f'{key}\t{st}\t{fkey(ctl, data)}')
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, 'w') as f:
    f.write(f'# shader stage map from {os.path.basename(os.path.abspath(cap))} (gfx/tools/shmap_from_capture.py): key, stage, functional key\n')
    f.write('\n'.join(rows) + '\n')
print(f'{len(rows)} shaders → {out}')
