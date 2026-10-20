#!/usr/bin/env python3
"""The iOS Simulator's shader directory (plan item 114): a metallib built for the device does not load in the Simulator ("library was not compiled
for the simulator"), so the same MSL is compiled again with the Simulator's SDK. The flags are iOS's (gfx/tools/ios_metal.py, the single compile
convention; not edited here, since dmsh_build.py fingerprints it) with the SDK and the minimum version option made the Simulator's. For every key of
the shader stage map: <key>_b.metal from the Mac directory (dmsh_build.py's output, b-<chain>) → <key>_b.metallib, and <key>_b.json copied; for every
key of the game's fout.tsv also <key>_i_b. Readable-tile variants (_bf) are left out: ios/run.sh sim-run turns the TBDR rewrites off
(DM_NVN_TBDR=0), whose pass merges are their only users. A library newer than its source is kept; J jobs (default the CPU count).
Usage: sim_metal.py <shader map> <Mac dir b-<chain>> <out dir b-<chain>> [fout.tsv]   exit 0 done, 1 a source missing or a compile failed"""
import concurrent.futures, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gfx', 'tools'))
import ios_metal

def sim(cmd):
    """iOS's command with the Simulator's SDK and minimum version option"""
    out = ['iphonesimulator' if a == 'iphoneos' else a for a in cmd]
    return [a.replace('-mios-version-min=', '-mios-simulator-version-min=') for a in out]
METAL, METALLIB = sim(ios_metal.METAL), sim(ios_metal.METALLIB)

def keys_of(shmap, fout):
    ks, iks = [], []
    for line in open(shmap, encoding='utf-8'):
        if line.startswith('#') or not line.strip(): continue
        ks.append(line.split('\t')[0].strip())
    if fout and os.path.exists(fout):
        have = set(ks)
        for line in open(fout, encoding='utf-8'):
            if line.startswith('#') or not line.strip(): continue
            k = line.split()[0]
            if k in have: iks.append(k)
    return ks, iks

def build(src, lib):
    """0 kept, 1 built; raises on a failed compile"""
    if os.path.exists(lib) and os.path.getmtime(lib) >= os.path.getmtime(src): return 0
    with tempfile.TemporaryDirectory() as td:
        air = os.path.join(td, 'x.air')
        subprocess.run([*METAL, src, '-o', air], check=True, capture_output=True)
        subprocess.run([*METALLIB, air, '-o', lib + '.part'], check=True, capture_output=True)
    os.replace(lib + '.part', lib); return 1

def main():
    if len(sys.argv) not in (4, 5): print(__doc__); sys.exit(2)
    shmap, mac, out = sys.argv[1:4]; fout = sys.argv[4] if len(sys.argv) == 5 else None
    os.makedirs(out, exist_ok=True)
    ks, iks = keys_of(shmap, fout)
    jobs, missing = [], []
    for k in ks:
        src, js = os.path.join(mac, f'{k}_b.metal'), os.path.join(mac, f'{k}_b.json')
        if not (os.path.exists(src) and os.path.exists(js)): missing.append(k); continue
        jobs.append((src, os.path.join(out, f'{k}_b.metallib')))
        if not os.path.exists(os.path.join(out, f'{k}_b.json')) or os.path.getmtime(os.path.join(out, f'{k}_b.json')) < os.path.getmtime(js): shutil.copy2(js, out)
    for k in iks:
        src = os.path.join(mac, f'{k}_i_b.metal')
        if not os.path.exists(src): missing.append(f'{k}_i_b'); continue
        jobs.append((src, os.path.join(out, f'{k}_i_b.metallib')))
    if missing: print(f'[red] {len(missing)} shaders of the map have no MSL in {mac}, e.g. {", ".join(missing[:5])} (dmsh_build.py first)'); sys.exit(1)
    built = failed = 0
    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('J', os.cpu_count() or 8))) as ex:
        for (src, lib), f in zip(jobs, ex.map(lambda j: _try(*j), jobs)):
            if f is None: failed += 1; print(f'[red] {os.path.basename(src)}: compile failed') if failed <= 5 else None
            else: built += f
    print(f'Simulator shaders in {out}: {len(jobs)} libraries ({built} built, {len(jobs) - built - failed} kept, {failed} failed); {" ".join(METAL[:4])}')
    sys.exit(1 if failed else 0)

def _try(src, lib):
    try: return build(src, lib)
    except subprocess.CalledProcessError: return None

if __name__ == '__main__':
    main()
