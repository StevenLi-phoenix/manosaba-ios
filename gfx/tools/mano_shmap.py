#!/usr/bin/env python3
# 取样目录（宿主 DM_SHDUMP 写出的 <键>.ctl / <键>.data）→ 运行时着色器映射表（只用「键 → 阶段」，列序同 shader_map.tsv）。
# 阶段判读与 dmsh_build.py 取样模式同：data 以代码区标记 78563412 开头 = 图形，读 +0x30 的 SPH 首字（类型位 0–4、着色器类型位 10–13）；否则计算。
# 用法：mano_shmap.py <取样目录> <输出 tsv>
# Capture directory (<key>.ctl / <key>.data written by the host's DM_SHDUMP) → the runtime shader mapping table (only "key → stage" is used; column order as in shader_map.tsv).
# Stage detection as in dmsh_build.py capture mode: data starting with the code area marker 78563412 = graphics, read the first SPH word at +0x30 (type bits 0–4, shader type bits 10–13); otherwise compute.
# Usage: mano_shmap.py <capture dir> <output tsv>
import hashlib, os, struct, sys

cap, out = sys.argv[1:3]
rows = []
for fn in sorted(os.listdir(cap)):
    if not fn.endswith('.ctl'): continue
    key = fn[:-4]
    ctl = open(os.path.join(cap, fn), 'rb').read(); data = open(os.path.join(cap, key + '.data'), 'rb').read()
    if hashlib.sha256(ctl).hexdigest()[:16] != key: sys.exit(f'【红】{key}：控制段哈希不符')
    if data[:4] == bytes.fromhex('78563412'):
        w0 = struct.unpack_from('<I', data, 0x30)[0]
        st = {(1, 1): '顶点', (2, 5): '像素'}.get((w0 & 0x1f, (w0 >> 10) & 0xf))
        if not st: sys.exit(f'【红】{key}：SPH 首字 {w0:#x} 不是顶点 / 像素')
    else: st = '计算'
    rows.append(f'capture\t0\t{st}\t{key}\t-\t-\t-\t-\t-\t-\t-\t-\t-')
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, 'w') as f:
    f.write('# kind: evidence  manosaba 取样着色器 → 阶段（gfx/tools/mano_shmap.py）\n')
    f.write('package\tindex\tstage\tdm_ctl_sha16\tpc_dxbc_sha16\tmetallib\tpc_outputs\tdm_outputs\tdm_ubo\tdm_tex\tdm_samp\tdm_ssbo\tsubset_ok\n')
    f.write('\n'.join(rows) + '\n')
print(f'映射表 {len(rows)} 条 → {out}')
