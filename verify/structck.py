# Rewrite structural check: no code is run, the rewriter's build output is checked word by word. Denominators are recounted with an independent
# disassembler (llvm-objdump; Xcode's by default, LLVM_BIN overrides), not relying on the rewriter's own capstone scan.
# Usage: structck.py <build output dir> <module> <NSO>     last output line STRUCT <module> OK|FAIL ..., exit code 0/1
# Checks:
#  ⓪ Cuts (cuts.tsv): build ID matches, original image at that site == original bytes, rewritten image == new bytes, reason non-empty, no overlap with
#     rewrite sites / windows; host calls introduced by cuts (svc #N, N = 0x80–0xff) are rewritten into gates and have entry form (svc ; ret at a function
#     entry) or call-site form (a single bl replaced)
#  ① Non-text segments are byte-for-byte identical to the original image except for cuts; everything from the end of the image to the patch area is 0
#  ② Changed words in text == rewrite site heads (non-J rows of sites.tsv + window starts) ∪ cuts inside text; all rewrite site heads are a B into the patch area
#  ③ Independent denominator: llvm-objdump reads the original text; x18/w18 operands, tpidr_el0 reads/writes, tpidrro_el0 reads, svc, cntpct/cntvct
#     all fall within rewrite sites or windows; conversely, every rewrite site is in the independent set (except window starts)
#  ④ The patch area no longer contains x18/w18, tpidr_el0, svc, cntpct/cntvct
#  ⑤ Host gate (svc / timer) blocks equal the template word for word, and match gates.tsv one to one (return point, kind, svc number / destination register, original address)
#  ⑥ Thread slots: the key page indices in the header are the eight known keys, 0..7; every mrs xN, tpidrro_el0 in the patch area is followed (skipping
#     adrp/add that do not write xN) by the key page indirect slot sequence, and the slot access counts match the sites
#  ⑦ Observation gates (obs.tsv): original image at that site == the registered original instruction word; the original instruction is relocatable (not
#     PC-relative / not a branch / does not involve x18 / does not read or write system registers, judged by capstone); may be in the middle of a function
#     (the gate ABI saves and restores everything, checked by gatefn); no overlap with rewrite sites / windows / cuts; the gate block = the template word
#     for word (the same first half as an svc gate + original instruction + b back to off+4), gates.tsv records an obs row
#  ⑧ Parameter gates (params.tsv): the two original words build a constant with movz/movk, the two new words = trampoline branch / nop, the register is not
#     touched in between, no branch target in the range, key registered, a unique original value per key, no constant mask downstream
#  ⑨ Temporary registers: every non-original destination register in a patch block has a matching red-zone save/restore pair at the same offset
#  ⑩ Trampoline exits: for every b jumping into the patch area, the patch block's exits back to text may only be original site+4 / end of the absorbed
#     span / end of the window / a branch target of an original instruction within the span (cuts are let through and listed)
import sys, os, re, struct, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import nsoimg
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)

def _llvm_bin():
    if os.environ.get('LLVM_BIN'): return os.environ['LLVM_BIN']
    try: return os.path.dirname(subprocess.run(['xcrun', '--find', 'llvm-objdump'], capture_output=True, text=True, check=True).stdout.strip())
    except (OSError, subprocess.CalledProcessError): sys.exit('structck: no llvm-objdump (install Xcode or set LLVM_BIN to a directory holding llvm-objdump)')
LLVM = _llvm_bin()
out_dir, mod, nso = sys.argv[1:4]
orig = nsoimg.load(nso)
h = nsoimg.header(out_dir, mod)
PO, TE = h['PATCH_OFF'], h['TEXT_END']
pat = open(f'{out_dir}/{mod}.patched.bin', 'rb').read()
W = lambda b, o: struct.unpack_from('<I', b, o)[0]
fails = []
def bad(msg):
    fails.append(msg)
    if len(fails) <= 30: print('FAIL', msg)

def elf_wrap(blob, path):
    """Same layout as `llvm-objcopy -I binary -O elf64-littleaarch64 --rename-section .data=.text,code`: one executable .text at address 0.
    Used when only Xcode's llvm-objdump is available (Xcode ships no llvm-objcopy)."""
    shstr = b'\0.text\0.shstrtab\0'
    text_off = 64; str_off = text_off + len(blob); sh_off = (str_off + len(shstr) + 7) & ~7
    ehdr = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9) + struct.pack('<HHIQQQIHHHHHH', 1, 183, 1, 0, 0, sh_off, 0, 64, 0, 0, 64, 3, 2)
    sh = lambda name, typ, flags, off, size, align: struct.pack('<IIQQQQIIQQ', name, typ, flags, 0, off, size, 0, 0, align, 0)
    with open(path, 'wb') as f:
        f.write(ehdr + blob + shstr + bytes(sh_off - str_off - len(shstr)))
        f.write(bytes(64) + sh(1, 1, 6, text_off, len(blob), 4) + sh(7, 3, 0, str_off, len(shstr), 1))
def llvm_dis(blob, base):
    """按原始字节反汇编，返回相关行 {偏移: 小写指令文本}。 | Disassemble from raw bytes and return the relevant lines {offset: lowercase instruction text}."""
    with tempfile.TemporaryDirectory() as td:
        b, e = os.path.join(td, 'x.bin'), os.path.join(td, 'x.elf')
        open(b, 'wb').write(blob)
        if os.path.exists(f'{LLVM}/llvm-objcopy'):
            subprocess.run([f'{LLVM}/llvm-objcopy', '-I', 'binary', '-O', 'elf64-littleaarch64',
                            '--rename-section', '.data=.text,code', b, e], check=True)
        else: elf_wrap(blob, e)
        # 只留可能相关的行（x18/w18、tpidr、svc、cnt?ct），几百万行的筛选交给 grep
        # Keep only potentially relevant lines (x18/w18, tpidr, svc, cnt?ct); filtering millions of lines is left to grep
        od = subprocess.Popen([f'{LLVM}/llvm-objdump', '-d', '--no-show-raw-insn', e], stdout=subprocess.PIPE)
        r = subprocess.run(['/usr/bin/grep', '-iE', r'[xw]18|tpidr|svc|cnt[pv]ct'], stdin=od.stdout, capture_output=True, text=True)
        od.stdout.close(); assert od.wait() == 0
    out = {}
    for line in r.stdout.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+(.*)$', line)
        if m: out[base + int(m.group(1), 16)] = m.group(2).split('//')[0].split('<')[0].strip().lower()
    return out

RX18 = re.compile(r'\b[xw]18\b')
def sysk(t):
    if re.match(r'svc\s', t): return 'svc'
    if re.match(r'mrs\s', t) and 'tpidr_el0' in t: return 'tpidr_r'
    if re.match(r'msr\s', t) and 'tpidr_el0' in t: return 'tpidr_w'
    if re.match(r'mrs\s', t) and 'tpidrro_el0' in t: return 'tpidrro_r'
    if re.match(r'mrs\s', t) and 'cntpct_el0' in t: return 'cntpct'
    if re.match(r'mrs\s', t) and 'cntvct_el0' in t: return 'cntvct'
    return None

# ---- sites.tsv ----
sites = {}; wins = []; jumps = 0; fnptr = 0
for l in open(f'{out_dir}/{mod}.sites.tsv'):
    f = l.split(); a = int(f[0], 16)
    if f[1] == 'J': jumps += 1; continue
    if f[1] == 'T': fnptr += 1; continue   # br / blr x18 through a function pointer: annotation of an x18 site, which has its own row
    if f[1] == 'W': wins.append((a, int(f[2], 16))); sites[a] = 'W'
    else: sites[a] = f[1]
inwin = set(o for lo, hi in wins for o in range(lo, hi, 4))

# ⓪ 已登记剪口
# ⓪ Registered cuts
cuts = []; bid = None
for l in open(f'{out_dir}/{mod}.cuts.tsv'):
    if l.startswith('# BUILD_ID'): bid = l.split()[-1]; continue
    off, ob, nb, why = l.rstrip('\n').split('\t', 3)
    cuts.append((int(off, 16), bytes.fromhex(ob), bytes.fromhex(nb), why))
if bid != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad(f'cuts.tsv 构建号 {bid} 与 NSO 不符')
cutb = set()
for off, ob, nb, why in cuts:
    if orig[off:off + len(ob)] != ob: bad(f'剪口 {off:#x} 原字节不符')
    for k in range(0, len(nb), 4):                               # 新字节里的 svc 字随后被改写成门（头是 B），按改写点核，这里跳过 | The svc words in the new bytes are subsequently rewritten into gates (head is B) and checked as rewrite sites; skipped here
        if int.from_bytes(nb[k:k + 4], 'little') & 0xFFE0001F == 0xD4000001: continue
        if pat[off + k:off + k + len(nb[k:k + 4])] != nb[k:k + 4]: bad(f'剪口 {off:#x}+{k} 改写映像不是新字节'); break
    if not why.strip() or len(ob) != len(nb): bad(f'剪口 {off:#x} 理由为空或长度不等')
    cutb |= set(range(off, off + len(ob)))
# Host-call gates produced by cuts: a word in the cut's new bytes that decodes to svc #N (N = 0x80–0xff; the handler is registered at run time by the
# game's hooks, host/hle.h dm_host_call_register). The form is read from the bytes:
#   entry form: new bytes = svc #N ; ret at an eh_frame function entry - the gate saves and restores x30, ret returns to the original caller;
#   call-site form: the original instruction is a bl and the cut replaces that single word with svc #N; the gate returns to off+4 (checked by the final b of the ⑤ template).
#   Register rationale for the call-site form: at a bl, per AAPCS the callee could clobber x9..x17, x30 and NZCV anyway, so the gate's side effects on
#   these do not change caller-visible behavior; the x0..x7 return values and x19..x28 / sp are preserved by the host gate contract (checked gate by gate by gatefn).
_hostcall = {}
def _bl(w, o): return o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) if w & 0xFC000000 == 0x94000000 else None
_fstarts = None
for off, ob, nb, why in cuts:
    for k in range(0, len(nb), 4):
        w = int.from_bytes(nb[k:k + 4], 'little')
        if w & 0xFFE0001F != 0xD4000001: continue
        n = (w >> 5) & 0xFFFF; a = off + k
        if not 0x80 <= n <= 0xFF: bad(f'cut {off:#x} introduces svc #{n:#x}: host calls are 0x80–0xff (below 0x80 is Horizon\'s range)'); continue
        if sites.get(a) != 'svc': bad(f'cut {off:#x}: svc #{n:#x} was not rewritten into a host gate (sites.tsv has no {a:#x} svc)')
        _hostcall[a] = n
        if nb[k + 4:k + 8] == (0xD65F03C0).to_bytes(4, 'little'):
            if _fstarts is None: _fstarts = set(nsoimg.func_starts(orig, TE))
            if a not in _fstarts: bad(f'host call svc #{n:#x} {a:#x}: entry form (svc ; ret) but not an eh_frame function entry')
        elif _bl(W(orig, a), a) is not None:
            if len(nb) != 4: bad(f'host call svc #{n:#x} {a:#x}: a call-site cut may replace only that one bl')
        else: bad(f'host call svc #{n:#x} {a:#x}: neither entry form (svc ; ret at a function entry) nor call-site form (replaces a bl)')
if cutb & {a + i for a in list(sites) + list(inwin) if a not in _hostcall for i in range(4)}: bad('剪口与改写点 / 窗口重叠')
ncut_code = sum(1 for c in cuts if c[0] < TE); ncut_data = len(cuts) - ncut_code
obs = []
obs_p = f'{out_dir}/{mod}.obs.tsv'
if os.path.exists(obs_p):
    for l in open(obs_p, encoding='utf-8'):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('obs.tsv 构建号与 NSO 不符')
            continue
        off, oid, ow, why = l.rstrip('\n').split('\t', 3); obs.append((int(off, 16), int(oid), int(ow, 16), why))
obs_heads = {o for o, *_ in obs}
params = []                                                          # 参数门（params.tsv，r11 起）：(A 偏移, B 偏移, 寄存器号, 键名) | Parameter gates (params.tsv, since r11): (A offset, B offset, register number, key name)
par_p = f'{out_dir}/{mod}.params.tsv'
if os.path.exists(par_p):
    for l in open(par_p, encoding='utf-8'):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('params.tsv 构建号与 NSO 不符')
            continue
        a, b, r, k, why = l.rstrip('\n').split('\t', 4)
        if not why.strip(): bad(f'参数门 {a} 理由为空')
        params.append((int(a, 16), int(b, 16), int(r.lstrip('x')), k))
par_words = {o for a, b, r, k in params for o in (a, b)}

# ① 非 text 段：先整块比，块不同再逐字节，差异只许落在剪口里（剪口字节已在 ⓪ 按应有值核过）
# ① Non-text segments: compare whole blocks first, then byte by byte where blocks differ; differences may only fall inside cuts (cut bytes were already checked against their expected values in ⓪)
nd = []
for blk in range(TE, len(orig), 4096):
    e = min(blk + 4096, len(orig))
    if pat[blk:e] != orig[blk:e]: nd += [o for o in range(blk, e) if pat[o] != orig[o] and o not in cutb]
if nd: bad(f'非 text 段与原映像不同（剪口以外）{len(nd)} 字节 例 {[hex(x) for x in nd[:5]]}')
if any(pat[len(orig):PO]): bad('映像尾到补丁区之间非 0')

# ② text 改动 == 改写点头
# ② text changes == rewrite site heads
diff = set()
for blk in range(0, TE & ~3, 4096):                    # 先整块比，块不同再逐字 | Compare whole blocks first, then word by word where blocks differ
    e = min(blk + 4096, TE & ~3)
    if orig[blk:e] != pat[blk:e]: diff |= {o for o in range(blk, e, 4) if W(orig, o) != W(pat, o)}
heads = set(sites)
cutw = {o & ~3 for o in cutb if o < TE}
_exp = (heads | obs_heads | par_words) - cutw; _got = diff - cutw                # 剪口里的宿主调用门头在剪口字节里，按剪口核 | Host-call gate heads inside cuts are within the cut bytes and are checked as cuts
if _got != _exp: bad(f'text 改动与改写点不一致：多改 {len(_got - _exp)} 漏改 {len(_exp - _got)} 例 {[hex(x) for x in sorted(_got ^ _exp)[:5]]}')
def btarget(o):
    w = W(pat, o)
    if w & 0xFC000000 != 0x14000000: return None
    imm = w & 0x3FFFFFF
    if imm & 0x2000000: imm -= 0x4000000
    return o + 4 * imm
nb = [a for a in heads | obs_heads if not (btarget(a) is not None and PO <= btarget(a) < len(pat))]
if nb: bad(f'改写点头不是跳进补丁区的 B：{len(nb)} 例 {[hex(x) for x in nb[:5]]}')

# ③ 独立分母
# ③ Independent denominator
def cached_dis(blob):
    """原映像是固定数据：相关行按 sha 缓存（llvm 换版本另起键），只在首次付反汇编的十来秒。 | The original image is fixed data: relevant lines are cached by sha (a new llvm version gets a new key), so the ten-odd seconds of disassembly are paid only the first time."""
    import hashlib, json
    cd = os.environ.get('DM_VCACHE', os.path.join(DM_ROOT, 'build/cache/dm/verify'))
    ver = hashlib.sha256(subprocess.run([f'{LLVM}/llvm-objdump', '--version'], capture_output=True).stdout).hexdigest()[:8]
    p = os.path.join(cd, f'{mod}.{hashlib.sha256(blob).hexdigest()[:16]}.{ver}.json')
    if os.path.exists(p): return {int(k): v for k, v in json.load(open(p)).items()}
    d = llvm_dis(blob, 0)
    os.makedirs(cd, exist_ok=True); tmp = f'{p}.new.{os.getpid()}'; json.dump(d, open(tmp, 'w')); os.replace(tmp, p)
    return d
D = cached_dis(orig[:TE])
ind = {}
for o, t in D.items():
    if RX18.search(t): ind[o] = 'x18'
    k = sysk(t)
    if k: ind[o] = k
miss = sorted(o for o in ind if o not in sites and o not in inwin)
if miss: bad(f'独立反汇编有、改写点没覆盖：{len(miss)} 例 {[(hex(x), D[x]) for x in miss[:5]]}')
extra = sorted(a for a, k in sites.items() if k != 'W' and a not in ind and a not in _hostcall)   # 剪口引入的 svc 不在原映像里 | The svc introduced by a cut is not in the original image
if extra: bad(f'改写点不在独立集合里：{len(extra)} 例 {[(hex(x), D.get(x)) for x in extra[:5]]}')
kmis = sorted(a for a, k in sites.items() if k != 'W' and a in ind and ind[a] != k)
if kmis: bad(f'改写点种类与独立判读不同：{len(kmis)} 例 {[(hex(x), sites[x], D[x]) for x in kmis[:5]]}')
nwin_ref = sum(1 for o in ind if o in inwin)

# ④ 补丁区干净
# ④ Patch area is clean
P = llvm_dis(pat[PO:], PO)
left = sorted(o for o, t in P.items() if RX18.search(t) or sysk(t) not in (None, 'tpidrro_r'))
if left: bad(f'补丁区残留 x18/系统寄存器/svc：{len(left)} 例 {[(hex(x), P[x]) for x in left[:5]]}')

# ⑤ 宿主门
# ⑤ Host gates
KEY = {k[4:-4]: v for k, v in h.items() if k.startswith('KEY_') and k.endswith('_IDX')}
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
_md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); _md.detail = True
def keyseq(at):
    q = list(_md.disasm(pat[at:at + 20], at))
    if len(q) < 5 or q[0].mnemonic != 'mrs' or 'tpidrro_el0' not in q[0].op_str \
            or q[1].mnemonic != 'adrp' or q[1].operands[1].imm != h['KEY_PAGE_OFF'] \
            or q[2].mnemonic != 'ldr' or q[2].operands[1].mem.base != q[1].operands[0].reg \
            or q[3].mnemonic != 'add' or q[3].operands[0].reg != q[0].operands[0].reg \
            or q[3].operands[1].reg != q[0].operands[0].reg:
        return None
    d = q[2].operands[1].mem.disp
    return (q[3].address + 4, d // 8) if d % 8 == 0 and 0 <= d // 8 < 8 else None
def paramseq(at, r):
    q = list(_md.disasm(pat[at:at + 40], at))
    tmp = 17 if r == 16 else 16
    rn = lambda ins, i: ins.reg_name(ins.operands[i].reg)
    if len(q) < 8 or q[0].mnemonic not in ('stur', 'str') or rn(q[0], 0) != f'x{tmp}' \
            or q[0].operands[1].mem.base != capstone.arm64_const.ARM64_REG_SP or q[0].operands[1].mem.disp not in (-16, -24) \
            or q[1].mnemonic != 'mrs' or rn(q[1], 0) != f'x{r}' \
            or 'tpidrro_el0' not in q[1].op_str or q[2].mnemonic != 'adrp' \
            or q[2].operands[1].imm != h['KEY_PAGE_OFF'] or q[3].mnemonic != 'ldr' \
            or q[3].operands[1].mem.base != q[2].operands[0].reg or q[4].mnemonic != 'add' \
            or q[4].operands[0].reg != q[1].operands[0].reg or q[4].operands[1].reg != q[1].operands[0].reg \
            or q[5].mnemonic != 'ldr' or q[5].operands[0].reg != q[1].operands[0].reg \
            or q[5].operands[1].mem.base != q[4].operands[0].reg or q[6].mnemonic not in ('ldur', 'ldr') \
            or rn(q[6], 0) != f'x{tmp}' \
            or q[6].operands[1].mem.base != capstone.arm64_const.ARM64_REG_SP \
            or q[6].operands[1].mem.disp != q[0].operands[1].mem.disp or q[7].mnemonic != 'b':
        return None
    d = q[3].operands[1].mem.disp
    return (q[6].address + 4, d // 8)
gates = {}
for l in open(f'{out_dir}/{mod}.gates.tsv'):
    r, k, arg, oa = l.split(); gates[int(r, 16)] = (k, int(arg), int(oa, 16))
def B(frm, to): return 0x14000000 | (((to - frm) >> 2) & 0x3FFFFFF)
# 门块 blr 前须有 ldr x29,[sp]：键页取槽借用的 x29 还原成客体原值，宿主读到的 x29 才是真帧指针（r26，见 patch_nso emit_gate_call）
# The gate block must have ldr x29,[sp] before blr: the x29 borrowed for fetching the slot via the key page is restored to the guest's original value, so the x29 the host reads is the real frame pointer (r26, see patch_nso emit_gate_call)
LDR_X29_SP = 0xF94003FD
gsites = sorted(a for a, k in sites.items() if k in ('svc', 'cntpct', 'cntvct'))
seen = set()
for a in gsites:
    s = btarget(a); ow = W(orig, a) if a not in _hostcall else (0xD4000001 | _hostcall[a] << 5)
    ks = keyseq(s + 4)
    if W(pat, s) != 0xA9BF7BFD or not ks or ks[1] != KEY.get('KGATE') \
            or W(pat, ks[0]) & 0xFFC00000 != 0xF9400000 or W(pat, ks[0] + 4) != LDR_X29_SP or W(pat, ks[0] + 8) != 0xD63F03C0 \
            or W(pat, ks[0] + 12) != 0xA8C17BFD or W(pat, ks[0] + 16) != B(ks[0] + 16, a + 4):
        bad(f'门块不符键页模板 {a:#x}'); continue
    arg = (ow >> 5) & 0xFFFF if sites[a] == 'svc' else ow & 31
    g = gates.get(ks[0] + 12)
    if g != (sites[a], arg, a): bad(f'gates.tsv 不符 {a:#x}：表 {g} 期望 {(sites[a], arg, a)}')
    seen.add(ks[0] + 12)
# ⑦ 观测门
# ⑦ Observation gates
import bisect as _bs
_starts = set(nsoimg.func_starts(orig, TE))
_busy = {a + i for a in list(sites) + list(inwin) for i in range(4)} | cutb
for off, oid, ow, why in obs:
    if W(orig, off) != ow: bad(f'观测门 {off:#x} 原指令字不符：映像 {W(orig, off):08x} 登记 {ow:08x}'); continue
    ins = next(_md.disasm(struct.pack('<I', ow), off), None)
    m = ins.mnemonic if ins else '?'
    if ins is None or m in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz', 'svc', 'mrs', 'msr', 'eret') or m.startswith('b.') \
       or (m.startswith('ldr') and '[' not in ins.op_str) or RX18.search(ins.op_str or ''):
        bad(f'观测门 {off:#x} 原指令不可搬：{m} {ins.op_str if ins else ""}')
    # 不再要求函数入口：_dm_gate 存回 x0–x17、x19–x30、q0–q31 与 NZCV/FPSR/FPCR，函数中间的活寄存器不受影响；
    # 这条 ABI 由 gatefn 功能核（桩故意破坏 x9–x17 与 NZCV，核全部恢复），不在这里按位置近似
    # Function entry is no longer required: _dm_gate saves and restores x0–x17, x19–x30, q0–q31 and NZCV/FPSR/FPCR, so live registers in the middle of a function are unaffected;
    # this ABI is checked functionally by gatefn (the stub deliberately clobbers x9–x17 and NZCV, and full restoration is checked), not approximated here by position
    if set(range(off, off + 4)) & _busy: bad(f'观测门 {off:#x} 与改写点 / 窗口 / 剪口重叠')
    s_ = btarget(off)
    if s_ is None or not (PO <= s_ < len(pat)): bad(f'观测门头 {off:#x} 不是跳进补丁区的 B'); continue
    ks = keyseq(s_ + 4)
    if W(pat, s_) != 0xA9BF7BFD or not ks or ks[1] != KEY.get('KGATE') \
            or W(pat, ks[0]) & 0xFFC00000 != 0xF9400000 or W(pat, ks[0] + 4) != LDR_X29_SP or W(pat, ks[0] + 8) != 0xD63F03C0 \
            or W(pat, ks[0] + 12) != 0xA8C17BFD or W(pat, ks[0] + 16) != ow \
            or W(pat, ks[0] + 20) != B(ks[0] + 20, off + 4):
        bad(f'观测门块不符键页模板 {off:#x}'); continue
    if gates.get(ks[0] + 12) != ('obs', oid, off): bad(f'观测门行不符 {off:#x}：表 {gates.get(ks[0] + 12)}')
    seen.add(ks[0] + 12)
if set(gates) != seen: bad(f'gates.tsv 有多余行 {len(set(gates) - seen)}')

# ⑥ 线程槽
# ⑥ Thread slots
_base = {'K18', 'KTLS', 'KTLR', 'KGATE'}; _extra = set(KEY) - _base   # 额外键 = 参数门键（KRES* / KASPECT* …；改写器全局键表，三模块表头都列）：须是 K 开头大写名、键号低于基础四键 | Extra keys = parameter-gate keys (KRES* / KASPECT* ...; the rewriter's global key table, listed in the headers of all three modules): must be uppercase names starting with K, with key numbers below the four base keys
if set(KEY) != {'K18', 'KTLS', 'KTLR', 'KGATE', 'KPARAM0', 'KPARAM1', 'KPARAM2', 'KPARAM3'} \
        or not {k for *_, k in params} <= set(KEY) or set(KEY.values()) != set(range(8)):
    bad(f'表头键页下标不合规：{KEY}（须含八个键且下标恰为 0..7）')
kv = {v: k for k, v in KEY.items()}; nk = {}
for o in range(PO, len(pat) - 4, 4):
    w = W(pat, o)
    if w & 0xFFFFFFE0 != 0xD53BD060: continue           # mrs xN, tpidrro_el0
    n = w & 31; ks = keyseq(o); k = kv.get(ks[1]) if ks else None
    if not ks or k is None:
        bad(f'补丁区 {o:#x} 读 tpidrro 后不是键页间接槽序列'); continue
    nk[k] = nk.get(k, 0) + 1
expected = {}
for kind in sites.values():
    key = {'x18': 'K18', 'W': 'K18', 'tpidr_r': 'KTLS', 'tpidr_w': 'KTLS', 'tpidrro_r': 'KTLR'}.get(kind)
    if key: expected[key] = expected.get(key, 0) + 1
expected['KGATE'] = len(gsites) + len(obs)
for _, _, _, key in params: expected[key] = expected.get(key, 0) + 1
if any(nk.get(k, 0) != v for k, v in expected.items()) or any(k not in expected and v for k, v in nk.items()):
    bad(f'键页槽访问计数不符：期望 {expected} 实际 {nk}')

# ⑧ 参数门（PARAMS，仅宿主）：原处 movz xR,#lo / movk xR,#hi,lsl #32 造的 64 位常数，等长换成 mrs xR,tpidrro_el0 / ldr xR,[xR,#键*8]
#   核：A 原字是 64 位 movz（hw=0）写 xR、B 原字是 64 位 movk（hw=2）写 xR；新字逐位 = 模板；A、B 之间的指令不读写 xR；
#   (A, B] 不是任何直接分支的目标、也不是函数入口（否则从中间进来的路径会拿到槽指针而不是常数）；键号在表头登记；不与改写点 / 剪口 / 观测门重叠。
#   宿主给槽原值（宽高打包 = 下列常数）时语义与原两条逐位等价：B 之后 xR 同值，A、B 之间无人读 xR。
# ⑧ Parameter gates (PARAMS, host only): a 64-bit constant originally built by movz xR,#lo / movk xR,#hi,lsl #32 is replaced at equal length by mrs xR,tpidrro_el0 / ldr xR,[xR,#key*8]
#   Checks: original word A is a 64-bit movz (hw=0) writing xR, original word B is a 64-bit movk (hw=2) writing xR; the new words = the template bit for bit; instructions between A and B do not read or write xR;
#   (A, B] is not the target of any direct branch nor a function entry (otherwise a path entering in the middle would get the slot pointer instead of the constant); the key number is registered in the header; no overlap with rewrite sites / cuts / observation gates.
#   When the host gives the slot the original value (packed width/height = the constants below), the semantics are bit-for-bit equivalent to the original two instructions: xR has the same value after B, and nobody reads xR between A and B.
par_const = {}
if params:
    import capstone
    _cs = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM); _cs.detail = True
    _tg = set()
    for o in range(0, TE & ~3, 4):                                  # 全 text 直接分支目标 | Direct branch targets across all of text
        w = W(orig, o)
        if w & 0x7C000000 == 0x14000000: d = ((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000; _tg.add(o + 4 * d)
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: d = (((w >> 5) & 0x7FFFF) ^ 0x40000) - 0x40000; _tg.add(o + 4 * d)
        elif w & 0x7E000000 == 0x36000000: d = (((w >> 5) & 0x3FFF) ^ 0x2000) - 0x2000; _tg.add(o + 4 * d)
    _fs = set(nsoimg.func_starts(orig, TE))
    _busy8 = {a for a in sites} | set(inwin) | obs_heads | {o & ~3 for o in cutb}
    for a, b, r, k in params:
        wa, wb = W(orig, a), W(orig, b)
        # 两种原指令对：64 位 movz xR,#lo / movk xR,#hi,lsl #32（KRES*）；32 位 movz wR,#lo / movk wR,#hi,lsl #16（KASPECT*，原值 = hi<<16|lo，
        # 写 w 寄存器高 32 位清零 ⇒ 宿主槽须按 u32 位模式存、高 32 位为 0（运行期约束，静态核不到，见 platform thread.c）
        # Two kinds of original instruction pair: 64-bit movz xR,#lo / movk xR,#hi,lsl #32 (KRES*); 32-bit movz wR,#lo / movk wR,#hi,lsl #16 (KASPECT*, original value = hi<<16|lo,
        # writing a w register zeroes the upper 32 bits ⇒ the host slot must be stored as a u32 bit pattern with the upper 32 bits 0 (a runtime constraint that cannot be checked statically, see platform thread.c)
        w64 = wa & 0xFFE00000 == 0xD2800000 and wb & 0xFFE00000 == 0xF2C00000
        w32 = wa & 0xFFE00000 == 0x52800000 and wb & 0xFFE00000 == 0x72A00000
        if not (w64 or w32) or wa & 31 != r or wb & 31 != r:
            bad(f'参数门 {a:#x}/{b:#x}：原指令 {wa:#010x}/{wb:#010x} 不是 movz/movk 造常数对（64 位 lsl #32 或 32 位 lsl #16）写 x{r}')
        if not a < b: bad(f'参数门 {a:#x}/{b:#x} 顺序不对')
        if k not in KEY: bad(f'参数门 {a:#x} 键 {k} 未在表头登记'); continue
        s = btarget(a); ks = paramseq(s, r) if s is not None else None
        ok = ks and ks[1] == KEY[k] and W(pat, b) == 0xD503201F
        if not ok: bad(f'参数门 {a:#x}/{b:#x}：键页间接模板不符')
        for o in range(a + 4, b, 4):
            ins = next(_cs.disasm(orig[o:o + 4], o), None)
            if ins is None: bad(f'参数门 {a:#x}：中间 {o:#x} 解不出'); continue
            rr, ww = ins.regs_access()
            names = {ins.reg_name(x) for x in rr + ww}
            if f'x{r}' in names or f'w{r}' in names: bad(f'参数门 {a:#x}：中间 {o:#x} {ins.mnemonic} {ins.op_str} 读写 x{r}')
        bt = sorted(t for t in _tg if a < t <= b) + sorted(t for t in _fs if a < t <= b)
        if bt: bad(f'参数门 {a:#x}..{b:#x}：区间内有分支目标 / 函数入口 {[hex(x) for x in bt[:4]]}')
        if {a, b} & _busy8: bad(f'参数门 {a:#x}/{b:#x} 与改写点 / 窗口 / 观测门 / 剪口重叠')
        par_const[(a, k)] = ((wb >> 5) & 0xFFFF) << (16 if w32 else 32) | ((wa >> 5) & 0xFFFF)
    _kc = {}
    for (a, k), v in par_const.items(): _kc.setdefault(k, set()).add(v)
    for k, vs in _kc.items():
        if len(vs) != 1: bad(f'参数门键 {k} 对应多个原常数 {[hex(v) for v in vs]}（同一槽给不出唯一原值）')
    print('参数门 ' + ', '.join(f'{a:#x}={k}:原值 {v:#x}' for (a, k), v in par_const.items()))
    # 参数寄存器的下游不得再被常数掩码：编译器会把「值只可能是这几个常数」折进后续指令——运行模式通知处理 0xe10e48 的
    # mov w9,#0x740; and x9,x25,x9 就是 1280|1600 推出的掩码，换成参数后宽度被掩成 768 / 64。从每个参数位起按函数内线性顺序做污点传播
    # （mov / csel 类传递，其余写入清除；不跟分支，属保守近似），被污染的寄存器遇到 and / ands / bic 且另一操作数是立即数或 mov 立即数造的寄存器即报
    # Downstream of a parameter register there must be no further masking by a constant: the compiler folds "the value can only be one of these constants" into later instructions - in the operation mode notification handler at 0xe10e48,
    # mov w9,#0x740; and x9,x25,x9 is exactly a mask derived from 1280|1600, and once replaced by a parameter the width gets masked to 768 / 64. From each parameter site, do taint propagation in linear order within the function
    # (mov / csel kinds propagate, other writes clear; branches are not followed, a conservative approximation); report when a tainted register meets and / ands / bic whose other operand is an immediate or a register built by a mov immediate
    _fsl = sorted(_fs)
    def _rn(ins, x): return re.sub(r'^[wx]', '', ins.reg_name(x)) if re.match(r'^[wx]\d+$', ins.reg_name(x) or '') else None
    _donef = set()
    for a, b, r, k in params:
        import bisect as _bi
        i = _bi.bisect_right(_fsl, a); f0 = _fsl[i - 1]; f1 = _fsl[i] if i < len(_fsl) else TE
        if f0 in _donef: continue
        _donef.add(f0)
        seeds = {(pa, str(pr)) for pa, pb, pr, pk in params if f0 <= pa < f1}
        taint, const = set(), {}
        for o in range(f0, f1, 4):
            for pa, pr in seeds:
                if o == pa: taint.add(pr)
            if any(o == pa for pa, _ in seeds): continue
            ins = next(_cs.disasm(pat[o:o + 4], o), None)
            if ins is None: continue
            rr, ww = ins.regs_access(); rd = {x for x in (_rn(ins, q) for q in rr) if x}; wr = {x for x in (_rn(ins, q) for q in ww) if x}
            m = ins.mnemonic
            if m in ('and', 'ands', 'bic', 'bics') and rd & taint:
                imm = any(op.type == capstone.arm64.ARM64_OP_IMM for op in ins.operands)
                if imm or (rd - taint) & set(const): bad(f'参数门下游常数掩码 {o:#x}：{m} {ins.op_str}（函数 {f0:#x}，被参数寄存器污染的值再被常数掩码）')
            if m in ('mov', 'csel', 'csinc', 'csinv', 'csneg') and rd & taint: taint |= wr; [const.pop(x, None) for x in wr]
            else:
                taint -= wr
                isimm = m in ('mov', 'movz') and len(ins.operands) == 2 and ins.operands[1].type == capstone.arm64.ARM64_OP_IMM
                for x in wr:
                    if isimm: const[x] = ins.operands[1].imm
                    else: const.pop(x, None)

# ⑨ 临时寄存器静态检查：每个补丁块的非原指令目的寄存器必须有同偏移红区存取配对。
# ⑨ Static check of temporary registers: every non-original-instruction destination register in a patch block must have a matching red-zone save/restore pair at the same offset.
import capstone
def _writes(ins):
    _, ww = ins.regs_access()
    return {'x29' if ins.reg_name(r) == 'fp' else 'x30' if ins.reg_name(r) == 'lr' else re.sub(r'^[wx]', 'x', ins.reg_name(r))
            for r in ww if ins.reg_name(r) not in ('sp', 'pc')}
def _mem_sp(ins, load):
    if (load and not ins.mnemonic.startswith('ld')) or (not load and not ins.mnemonic.startswith('st')):
        return None
    for op in ins.operands:
        if op.type == capstone.arm64_const.ARM64_OP_MEM and ins.reg_name(op.mem.base) == 'sp':
            if op.mem.disp < 0: return op.mem.disp
            if load and ins.mnemonic == 'ldp' and op.mem.disp == 0: return -16
    return None
def _block(start):
    out = []
    for ins in _md.disasm(pat[start:start + 256], start):
        out.append(ins)
        if ins.mnemonic == 'b':
            t = ins.operands[0].imm
            if not (PO <= t < len(pat)): break
    return out
static_entries = [(a, set()) for a in sites]
for a, b, r, _ in params:
    static_entries.append((a, {f'x{r}'}))
static_seen = 0
# mov sp, x18 (stackalloc) blocks reload x16 / x17 through x17 = old sp, which the red-zone pairing below cannot see: check them word for word instead
def _movsp_ok(a, s):
    q = list(_md.disasm(bytes(pat[s:s + 48]), s)); ks = keyseq(s + 8)
    return (len(q) == 12 and W(pat, s) == 0xF81F03F0 and W(pat, s + 4) == 0xF81E83F1                # stur x16,[sp,#-16]; stur x17,[sp,#-24]
            and q[2].op_str == 'x16, tpidrro_el0' and ks is not None and ks[1] == KEY['K18']
            and f'{q[6].mnemonic} {q[6].op_str}' == 'ldr x16, [x16]'
            and [f'{x.mnemonic} {x.op_str}' for x in q[7:]] == ['mov x17, sp', 'mov sp, x16', 'ldur x16, [x17, #-0x10]', 'ldur x17, [x17, #-0x18]', f'b #{a + 4:#x}'])
n_movsp = 0
for a, allowed in static_entries:
    s = btarget(a)
    if s is None: continue
    if next(_md.disasm(orig[a:a + 4], a), None) is not None and W(orig, a) == 0x9100025F:          # mov sp, x18
        static_seen += 1; n_movsp += 1
        if not _movsp_ok(a, s): bad(f'mov sp, x18 块 {a:#x} 不是模板')
        continue
    block = _block(s); static_seen += 1
    if not allowed:
        end = next((hi for lo, hi in wins if lo == a), a + 4)
        for ins in _md.disasm(orig[a:end], a): allowed |= _writes(ins)
        if next(_md.disasm(orig[a:a + 4], a), None).mnemonic in ('stp', 'ldp'):
            allowed.update(('x0', 'x1'))
    saved = {}
    restored = {}
    for i, ins in enumerate(block):
        d = _mem_sp(ins, False)
        if d is not None:
            regs = [op.reg for op in ins.operands[:2] if op.type == capstone.arm64_const.ARM64_OP_REG]
            for reg in regs:
                name = ins.reg_name(reg); name = 'x29' if name == 'fp' else 'x30' if name == 'lr' else re.sub(r'^[wx]', 'x', name)
                saved.setdefault(name, []).append((d, i))
        d = _mem_sp(ins, True)
        if d is not None:
            regs = [op.reg for op in ins.operands[:2] if op.type == capstone.arm64_const.ARM64_OP_REG]
            for reg in regs:
                name = ins.reg_name(reg); name = 'x29' if name == 'fp' else 'x30' if name == 'lr' else re.sub(r'^[wx]', 'x', name)
                restored.setdefault(name, []).append((d, i))
    writes = set().union(*(_writes(ins) for ins in block)) if block else set()
    for reg in sorted(writes - allowed):
        wi = [i for i, ins in enumerate(block) if reg in _writes(ins) and not (i > 0 and _mem_sp(ins, True) is not None)]
        dead = any(ins.mnemonic in ('br', 'blr') and ins.operands and ins.reg_name(ins.operands[0].reg) in (reg, reg[1:]) for ins in block)
        ok = dead or any(sd == rd and si < min(wi, default=10**9) and ri > max(wi, default=-1)
                 for sd, si in saved.get(reg, []) for rd, ri in restored.get(reg, []))
        if not ok: bad(f'临时寄存器 {a:#x}：块写 {reg}，原目的寄存器外且无同偏移存取配对')
print(f'STATIC {mod} scanned={static_seen} violations={sum(1 for x in fails if x.startswith("临时寄存器"))} mov_sp_template={n_movsp}')

# ⑩ 跳板出口（切掌机崩溃：参数门跳板回到 B+4，越过 A、B 之间未改的原指令和另一个跳板头）：
#   text 里每条跳进补丁区的 b（跳板头 a）沿补丁块走遍全部回 text 的出口（顺序 b 与条件分支，bl 视为返回，ret / br 不算回 text）。
#   合法出口：a+4；越过的字全是被改写吸收的字（≠ 原字且不是别的跳板头，如参数门 B 处的 nop）后的地址；
#   窗口（W 行 [lo, hi)）头：hi 与窗口内原指令自己的分支目标；非窗口头：连续吸收段内原指令自己的分支目标。
# ⑩ Trampoline exits (the handheld-switch crash: the parameter-gate trampoline returned to B+4, skipping the unmodified original instructions between A and B and another trampoline head):
#   for every b in text that jumps into the patch area (trampoline head a), follow the patch block through all exits back to text (sequential b and conditional branches; bl is treated as returning; ret / br do not count as returning to text).
#   Legal exits: a+4; the address after skipped words that are all words absorbed by the rewrite (≠ the original word and not another trampoline head, e.g. the nop at parameter-gate B);
#   window (W row [lo, hi)) heads: hi and the branch targets of the original instructions inside the window; non-window heads: the branch targets of the original instructions inside the contiguous absorbed span.
_heads = {o for o in range(0, TE & ~3, 4) if W(pat, o) & 0xFC000000 == 0x14000000 and btarget(o) is not None and btarget(o) >= PO}
_wend = dict(wins)
def _bt(w, o):
    if w & 0x7C000000 == 0x14000000: return o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000)
    if w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: return o + 4 * ((((w >> 5) & 0x7FFFF) ^ 0x40000) - 0x40000)
    if w & 0x7E000000 == 0x36000000: return o + 4 * ((((w >> 5) & 0x3FFF) ^ 0x2000) - 0x2000)
    return None
def _exits(start):
    ex, seen, st = set(), set(), [start]
    while st:
        o = st.pop()
        while o not in seen and o + 4 <= len(pat):
            seen.add(o); w = W(pat, o)
            if w & 0xFC000000 == 0x14000000:
                t = _bt(w, o)
                if t < PO: ex.add(t); break
                o = t; continue
            if w & 0xFC000000 == 0x94000000: o += 4; continue
            if w == 0xD65F03C0 or w & 0xFFFFFC1F == 0xD61F0000: break
            t = _bt(w, o)
            if t is not None:
                if t < PO: ex.add(t)
                else: st.append(t)
            o += 4
    return ex
nexit = 0; _cutoff = {c[0] for c in cuts}; _cutex = []
for a in sorted(_heads):
    if a in _wend:
        seg = range(a, _wend[a], 4); legal = {_wend[a]}
    else:
        e = a + 4
        while e < TE and W(pat, e) != W(orig, e) and e not in _heads: e += 4
        seg = range(a, e, 4); legal = {a + 4 * k for k in range(1, len(seg) + 1)}
    legal |= {t for o in seg for t in [_bt(W(orig, o), o)] if t is not None}
    for t in _exits(btarget(a)):
        nexit += 1
        if t not in legal and a in _cutoff: _cutex.append(f'{a:#x}→{t:#x}'); continue   # 已登记剪口（带理由，故意改控制流）：放行并列出 | Registered cuts (with a reason, deliberately changing control flow): let through and listed
        if t not in legal: bad(f'跳板出口 {a:#x} → {t:#x}：既不是 {a + 4:#x} / 吸收段尾 / 窗口尾，也不是段内原指令的分支目标（越过未改原指令或别的跳板头）')
print(f'EXITS {mod} 跳板头={len(_heads)} 出口={nexit} 已登记剪口改控制流出口={_cutex}')

cnt = {}
if _hostcall: print(f'剪口引入宿主调用门 {len(_hostcall)} 个：' + ', '.join(f"{a:#x}=svc#{n:#x}" for a, n in _hostcall.items()))
for k in sites.values(): cnt[k] = cnt.get(k, 0) + 1
print(f'STRUCT {mod} {"OK" if not fails else "FAIL"} 改写点={len(sites)} {cnt} 跳转表={jumps} 函数指针跳转={fnptr} 窗口内x18引用={nwin_ref} '
      f'已登记剪口={len(cuts)}(代码{ncut_code}/数据{ncut_data}) 观测门={len(obs)} 参数门={len(params)} '
      f'独立分母={len(ind)} text改动字={len(diff)} 门={len(gsites)} 槽访问={nk} 失败项={len(fails)}')
sys.exit(1 if fails else 0)
