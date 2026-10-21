#!/usr/bin/env python3
"""The iOS builds' CPU (plan item 108) and their single binary (item 135: ENABLE_DEBUG_DYLIB NO in project.yml and kept by project_m3.py; a Debug
build would move the app's code and the embedded game images into a separate debug dylib): ios/project.yml compiles for apple-a14, the oldest Metal 4 chip (A14 / M1), so one IPA covers every device
iOS 26 runs Metal 4 on; tools/project_m3.py keeps it for the iOS 18 build and adds DM_NVN_M3 and the iOS 18 target; tools/project_free.py leaves it.
tools/project_free.py keeps the increased memory limit, which a personal team signs (item 65), and drops extended virtual addressing, sustained
execution and the three iCloud keys; dropping the memory limit too, as before (DM_TEST_FREE_DROP_MEMORY=1), fails 1.
With an app executable (argument or DM_TEST_APP_BIN, e.g. work/run/ios/dd-ipa/Build/Products/Release-iphoneos/DmIOS.app/DmIOS), its host code
(__TEXT,__text, disassembled with every later extension known to llvm-objdump) is also checked for instructions an A14 lacks: BF16, I8MM, SME,
WFxT (measured before the change: none in the apple-a18 build either). The spec with the old apple-a18 (T_IOS_YML=<copy>, by hand) fails 2;
without the ENABLE_DEBUG_DYLIB line it fails 2."""
import os, re, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
YML = os.environ.get('T_IOS_YML') or os.path.join(ROOT, 'ios', 'project.yml')
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<84} {"✓" if ok else "✗"}')
def cflags(text):
    m = re.search(r'^\s*OTHER_CFLAGS:\s*(.*?)\s*(#.*)?$', text, re.M); return m.group(1) if m else ''
spec = open(YML, encoding='utf-8').read()
check('project.yml: -mcpu=apple-a14', '-mcpu=apple-a14' in cflags(spec).split())
def no_debug_dylib(text): return re.search(r'^\s*ENABLE_DEBUG_DYLIB:\s*NO\b', text, re.M) is not None
check('project.yml: ENABLE_DEBUG_DYLIB NO (one binary in Debug too)', no_debug_dylib(spec))
with tempfile.TemporaryDirectory() as td:
    out = os.path.join(td, 'm3.yml')
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'project_m3.py'), YML, out], capture_output=True, text=True)
    m3 = open(out, encoding='utf-8').read() if r.returncode == 0 else ''
    check('project_m3.py: apple-a14 with DM_NVN_M3=1, iOS 18.0', cflags(m3).split() == ['$(inherited)', '-mcpu=apple-a14', '-DDM_NVN_M3=1'] and 'iOS: "18.0"' in m3)
    check('project_m3.py: ENABLE_DEBUG_DYLIB NO kept', no_debug_dylib(m3))
    out = os.path.join(td, 'free.yml')
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'project_free.py'), YML, out], capture_output=True, text=True)
    free = open(out, encoding='utf-8').read() if r.returncode == 0 else ''
    check('project_free.py: the CPU left as it is', r.returncode == 0 and cflags(free) == cflags(spec))
    ents = lambda text: set(re.findall(r'^ *com\.apple\.developer\.([\w.-]+):', text, re.M))
    gone = {'kernel.extended-virtual-addressing', 'sustained-execution', 'icloud-container-identifiers', 'icloud-services', 'ubiquity-container-identifiers'}
    check('project_free.py: the increased memory limit kept, the other five dropped', 'kernel.increased-memory-limit' in ents(free) and
          ents(spec) - ents(free) == gone)
app = (sys.argv[1] if len(sys.argv) > 1 else None) or os.environ.get('DM_TEST_APP_BIN')
if app:
    d = subprocess.run(['xcrun', 'llvm-objdump', '-d', '--no-show-raw-insn', '--no-leading-addr', '--mattr=+bf16,+i8mm,+sme,+sme2,+wfxt,+v9.2a',
                        '--section=__text', app], capture_output=True, text=True).stdout
    mn = {l.split()[0] for l in d.split('\n') if l.strip() and not l.rstrip().endswith(':') and not l.startswith('Disassembly')}
    LATER = {'bfcvt', 'bfcvtn', 'bfcvtn2', 'bfdot', 'bfmmla', 'bfmlalb', 'bfmlalt',   # BF16 (bfi / bfxil are ARMv8.0 bitfield moves)
             'smmla', 'ummla', 'usmmla', 'usdot', 'sudot',                             # I8MM
             'smstart', 'smstop', 'zero', 'fmopa', 'smopa', 'umopa', 'bfmopa',         # SME
             'wfet', 'wfit'}                                                           # WFxT
    later = sorted(m for m in mn if m.split('.')[0] in LATER)
    check(f'{os.path.basename(app)}: {len(mn)} distinct instructions, none an A14 lacks {later or ""}', len(mn) > 100 and not later)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
