#!/usr/bin/env python3
"""gfx/nvn/core (plan item 264): the replay's decisions in plain C, shared by every backend. Each .c there compiles as plain C on its own and each
header compiles alone (no Objective-C, so no Metal); no file there includes Apple's frameworks, Grand Central Dispatch or the Metal backend's parts;
both Makefiles build the modules into the host and the graphics tests (GFX) and rebuild when a header changes (GFX_H); every module has its test
that needs no GPU (gfx/tests/t_core_<module>.c), built and run by make run. A module that imports Metal (T_CORE_BROKEN=1, the broken variant): 2 fail."""
import glob, os, re, subprocess, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
CORE = os.path.join(ROOT, 'gfx', 'nvn', 'core'); INC = os.path.join(ROOT, 'gfx', 'nvn', 'include')
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}')

files = {os.path.basename(p): open(p, encoding='utf-8').read() for p in sorted(glob.glob(os.path.join(CORE, '*.[ch]')))}
if os.environ.get('T_CORE_BROKEN'): files['store_index.c'] = '#import <Metal/Metal.h>\n' + files['store_index.c']
mods = sorted(f[:-2] for f in files if f.endswith('.c') and f != 'core.c')
check(f'gfx/nvn/core has core.h, core.c and modules ({" ".join(mods)})', 'core.h' in files and 'core.c' in files and len(mods) >= 2)
def cc(src):
    r = subprocess.run(['clang', '-x', 'c', '-std=gnu17', '-fsyntax-only', '-Wall', '-Werror', '-Wno-unused-function', '-iquote', CORE, '-I', INC, '-'],
                       input=src, capture_output=True, text=True)
    return r.returncode == 0, r.stderr.strip().split('\n')[0] if r.returncode else ''
bad = []
for f, t in files.items():
    ok, err = cc(t if f.endswith('.c') else f'#include "{f}"\n')
    if not ok: bad.append(f'{f}: {err}')
check(f'each .c compiles as plain C, each header alone {bad or ""}', not bad)
FORBID = re.compile(r'^\s*#\s*(?:import\b|include\s*[<"](?:Metal|MetalFX|Foundation|QuartzCore|CoreFoundation|dispatch|objc)/|include\s*"(?:\.\./)*mtl/|include\s*"(?:\.\./)*nvn_mtl\.)', re.M)
hits = [f'{f}:{t.count(chr(10), 0, m.start()) + 1}' for f, t in files.items() for m in FORBID.finditer(t)]
check(f'no file includes Apple frameworks, Grand Central Dispatch, Objective-C or the Metal backend {hits or ""}', not hits)
mk = {p: open(os.path.join(ROOT, p), encoding='utf-8').read() for p in ('host/Makefile', 'gfx/tests/Makefile')}
check('host/Makefile builds the modules (GFX) and rebuilds on their headers (GFX_H)',
      re.search(r'^GFX\s*=.*\.\./gfx/nvn/core/\*\.c', mk['host/Makefile'], re.M) is not None and re.search(r'^GFX_H\s*=.*\.\./gfx/nvn/core/\*\.h', mk['host/Makefile'], re.M) is not None)
check('gfx/tests/Makefile the same for the tests that link the backend',
      re.search(r'^GFX\s*=.*\.\./nvn/core/\*\.c', mk['gfx/tests/Makefile'], re.M) is not None and re.search(r'^GFX_H\s*=.*\.\./nvn/core/\*\.h', mk['gfx/tests/Makefile'], re.M) is not None)
run = mk['gfx/tests/Makefile'].split('\nrun:', 1)[1] if '\nrun:' in mk['gfx/tests/Makefile'] else ''
miss = [m for m in mods if not os.path.exists(os.path.join(ROOT, 'gfx', 'tests', f't_core_{m}.c')) or not re.search(rf'^\t\./t_core_{m}\b', run, re.M)
        or not re.search(rf'^t_core_{m}:', mk['gfx/tests/Makefile'], re.M)]
check(f'every module has gfx/tests/t_core_<module>.c, built and run by make run {miss or ""}', not miss)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
