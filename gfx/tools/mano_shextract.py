#!/usr/bin/env python3
# manosaba 全部 Switch 着色器静态抽出 → 与宿主 DM_SHDUMP 取样目录同格式（<键>.ctl = 控制段 0x880，<键>.data = 代码区），供 mano_shmap.py / dmsh_build.py 取样模式直接用。
# 三个来源（2.0.4b 核对：取样到的 75 个键全部落在这三处，代码区逐字节同取样）：
#   1. Shader 对象（Addressables 包、*.assets、Resources/unity*）：m_ParsedForm 各 pass 各 prog* 的 m_PlayerSubPrograms[*][*].m_BlobIndex
#      → 平台 19（UnityPy ShaderCompilerPlatform.kShaderCompPlatformSwitch）的 LZ4 blob → 子程序条目（版本 202012090：版本、类型、16 字节统计、
#      关键字串数组、m_ProgramCode 字节数组）。
#   2. ComputeShader 对象：variants[*].kernels[*].uniqueVariants[*].code，格式同 m_ProgramCode。
#   3. main 程序本体内嵌（引擎自带的拷贝 / 清屏等）：控制段魔数 34127698 处，代码区紧接控制段之后。
# m_ProgramCode 格式：0x60 字节头 = 4 组 × 6 个 u32（下标 = NVN 着色器阶段：0 顶点、1 片元、2 几何、3 细分控制、4 细分求值、5 计算；−1 = 无），
#   第 2 组 = 记录相对头尾的偏移，第 3 组 = 控制段长（恒 0x880），第 4 组 = 代码区长（= 控制段 +0x704）；记录 = 控制段 + 代码区。第 1 组不用。
# 阶段判读同 mano_shmap.py（代码区 78563412 标记 + SPH 首字）；只收顶点 / 片元 / 计算，其余阶段（翻译器未支持）跳过并列出。
# 另写 index.tsv：键、阶段、来源文件、着色器 / 内核名、pass、关键字（同一键多个来源各一行）。
# 用法（用 dm 的 venv python，要 UnityPy 与 lz4）：mano_shextract.py <RomFS 目录> <main NSO> <输出目录>
# Statically extract every manosaba Switch shader → same format as the host's DM_SHDUMP capture directory (<key>.ctl = 0x880-byte control section, <key>.data = code area), usable directly by mano_shmap.py / dmsh_build.py capture mode.
# Three sources (checked on 2.0.4b: all 75 captured keys fall in these three, code areas byte-identical to the captures):
#   1. Shader objects (Addressables bundles, *.assets, Resources/unity*): m_ParsedForm, every pass, every prog*, m_PlayerSubPrograms[*][*].m_BlobIndex
#      → the LZ4 blob of platform 19 (UnityPy ShaderCompilerPlatform.kShaderCompPlatformSwitch) → subprogram entry (version 202012090: version, type, 16 bytes of stats,
#      keyword string array, m_ProgramCode byte array).
#   2. ComputeShader objects: variants[*].kernels[*].uniqueVariants[*].code, same format as m_ProgramCode.
#   3. Embedded in the main executable (the engine's own copy / clear etc.): at the control section magic 34127698, code area immediately after the control section.
# m_ProgramCode format: 0x60-byte header = 4 groups × 6 u32 (index = NVN shader stage: 0 vertex, 1 fragment, 2 geometry, 3 tess control, 4 tess evaluation, 5 compute; −1 = none),
#   group 2 = record offset relative to the end of the header, group 3 = control section length (always 0x880), group 4 = code area length (= control +0x704); record = control section + code area. Group 1 is unused.
# Stage detection as in mano_shmap.py (code area marker 78563412 + first SPH word); only vertex / fragment / compute are kept, other stages (unsupported by the translator) are skipped and listed.
# Also writes index.tsv: key, stage, source file, shader / kernel name, pass, keywords (one row per source when a key has several).
# Usage (with dm's venv python, needs UnityPy and lz4): mano_shextract.py <RomFS dir> <main NSO> <output dir>
import collections, hashlib, os, struct, sys
import lz4.block, UnityPy
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../verify')); import nsoimg

romfs, main_nso, out = sys.argv[1:4]
CTL_MAGIC, CODE_MAGIC, SUB_VER, SWITCH = bytes.fromhex('34127698'), bytes.fromhex('78563412'), 202012090, 19
recs = {}                                   # 键 → (阶段, 控制段, 代码区) | key → (stage, control, code)
src = collections.defaultdict(list)         # 键 → [来源行] | key → [source rows]
skipped = collections.Counter()

def stage_of(code):
    if code[:4] != CODE_MAGIC: return 'cs'
    w0 = struct.unpack_from('<I', code, 0x30)[0]
    return {(1, 1): 'vs', (2, 5): 'ps'}.get((w0 & 0x1f, (w0 >> 10) & 0xf), f'sph{w0:#x}')

def add(ctl, code, nvn_stage, where, kws=''):
    if len(ctl) != 0x880 or ctl[:4] != CTL_MAGIC: sys.exit(f'【红】{where}：控制段不对（长 {len(ctl):#x}）')
    if struct.unpack_from('<I', ctl, 0x704)[0] != len(code): sys.exit(f'【红】{where}：控制段 +0x704 与代码区长 {len(code):#x} 不符')
    st = stage_of(code)
    if {'vs': 0, 'ps': 1, 'cs': 5}.get(st) != nvn_stage:
        if nvn_stage in (0, 1, 5): sys.exit(f'【红】{where}：头里阶段 {nvn_stage}，代码区判为 {st}')
        skipped[f'阶段 {nvn_stage}（{st}）'] += 1; return
    key = hashlib.sha256(ctl).hexdigest()[:16]
    if key in recs and recs[key][2] != code: sys.exit(f'【红】{where}：键 {key} 已有不同代码区')
    recs[key] = (st, ctl, code); src[key].append(f'{where}\t{nvn_stage}\t{kws}')

def program_code(pc, where, kws):
    if len(pc) < 0x60: sys.exit(f'【红】{where}：程序码只有 {len(pc):#x} 字节')
    g = [struct.unpack_from('<6i', pc, 24 * i) for i in range(4)]
    end = 0x60
    for s in range(6):
        if g[1][s] < 0:
            if g[2][s] >= 0 or g[3][s] >= 0: sys.exit(f'【红】{where}：阶段 {s} 头不一致 {g}')
            continue
        r = 0x60 + g[1][s]; c = r + g[2][s]; e = c + g[3][s]
        if e > len(pc): sys.exit(f'【红】{where}：阶段 {s} 记录越界')
        add(pc[r:c], pc[c:e], s, where, kws); end = max(end, e)
    if end != len(pc): sys.exit(f'【红】{where}：程序码长 {len(pc):#x}，记录止于 {end:#x}')

def subprogram(blob, off, ln, want_type, where):
    ver, typ = struct.unpack_from('<2i', blob, off)
    if ver != SUB_VER or typ != want_type: sys.exit(f'【红】{where}：子程序版本 / 类型 {ver} / {typ}（要 {SUB_VER} / {want_type}）')
    p = off + 24; kws = []
    n = struct.unpack_from('<i', blob, p)[0]; p += 4
    for _ in range(n):
        sl = struct.unpack_from('<i', blob, p)[0]; kws.append(blob[p + 4:p + 4 + sl].decode()); p = (p + 4 + sl + 3) & ~3
    pl = struct.unpack_from('<i', blob, p)[0]; p += 4
    if p + pl > off + ln: sys.exit(f'【红】{where}：程序码越出条目')
    return blob[p:p + pl], kws

def shader(tt, rel):
    name = tt['m_ParsedForm']['m_Name']
    if SWITCH not in tt['platforms']: skipped[f'无 Switch 平台的 Shader'] += 1; return
    i = tt['platforms'].index(SWITCH); cb = bytes(tt['compressedBlob'])
    segs = [lz4.block.decompress(cb[o:o + c], uncompressed_size=d)
            for o, c, d in zip(tt['offsets'][i], tt['compressedLengths'][i], tt['decompressedLengths'][i])]
    blob = segs[0]; n = struct.unpack_from('<i', blob, 0)[0]
    ents = [struct.unpack_from('<3i', blob, 4 + 12 * k) for k in range(n)]
    for si, ss in enumerate(tt['m_ParsedForm']['m_SubShaders']):
        for pi, ps in enumerate(ss['m_Passes']):
            for pk in ('progVertex', 'progFragment', 'progGeometry', 'progHull', 'progDomain', 'progRayTracing'):
                for tier in ps[pk]['m_PlayerSubPrograms']:
                    for sp in tier:
                        off, ln, seg = ents[sp['m_BlobIndex']]
                        where = f'{rel}\t{name}\t{si}.{pi} {ps["m_Name"]} {pk}'
                        pc, kws = subprogram(segs[seg], off, ln, sp['m_GpuProgramType'], where)
                        program_code(pc, where, ' '.join(kws))

def compute(tt, rel):
    for v in tt['variants']:
        for k in v['kernels']:
            for ui, u in enumerate(k['uniqueVariants']):
                kw = ' '.join(n for n, idx in k['variantIndices'] if idx == ui and n)
                program_code(bytes(u['code']), f'{rel}\t{tt["m_Name"]}\t{k["name"]} r{v["targetRenderer"]}', kw)

nfile = 0
for dp, dns, fns in os.walk(romfs, followlinks=True):
    dns.sort()
    for fn in sorted(fns):
        if not (fn.endswith(('.bundle', '.assets')) or fn in ('unity default resources', 'unity_builtin_extra', 'globalgamemanagers') or fn.startswith('level')): continue
        p = os.path.join(dp, fn); rel = os.path.relpath(p, romfs); nfile += 1
        for o in UnityPy.load(p).objects:
            if o.type.name == 'Shader': shader(o.read_typetree(), rel)
            elif o.type.name == 'ComputeShader': compute(o.read_typetree(), rel)

img = nsoimg.load(main_nso); i = 0; nmain = 0
while (i := img.find(CTL_MAGIC, i)) >= 0:
    total = struct.unpack_from('<I', img, i + 0x704)[0]
    add(img[i:i + 0x880], img[i + 0x880:i + 0x880 + total], {'vs': 0, 'ps': 1, 'cs': 5}.get(stage_of(img[i + 0x880:i + 0x880 + total]), -1),
        f'main@{i:#x}\t-\t-'); nmain += 1; i += 0x880

os.makedirs(out, exist_ok=True)
for key, (st, ctl, code) in recs.items():
    open(os.path.join(out, key + '.ctl'), 'wb').write(ctl); open(os.path.join(out, key + '.data'), 'wb').write(code)
with open(os.path.join(out, 'index.tsv'), 'w') as f:
    f.write('# 键\t阶段\t来源\t名\tpass / 内核\tNVN 阶段\t关键字 | key\tstage\tsource\tname\tpass / kernel\tNVN stage\tkeywords\n')
    for key in sorted(recs):
        for w in src[key]: f.write(f'{key}\t{recs[key][0]}\t{w}\n')
c = collections.Counter(st for st, _, _ in recs.values())
print(f'扫 {nfile} 个文件 + main 内嵌 {nmain} 个；独立着色器 {len(recs)}（顶点 {c["vs"]}、片元 {c["ps"]}、计算 {c["cs"]}）→ {out}')
for k, n in skipped.items(): print(f'  跳过 {k}：{n}')
