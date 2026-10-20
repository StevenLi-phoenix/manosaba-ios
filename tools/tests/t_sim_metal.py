#!/usr/bin/env python3
"""ios/tools/sim_metal.py (plan item 114): the Simulator's compile flags are iOS's (gfx/tools/ios_metal.py) with the SDK iphonesimulator and
-mios-simulator-version-min, nothing else changed; on a synthetic Mac directory (two shaders of the map and one integer variant of fout.tsv) it builds
three libraries for the iOS Simulator (the metallib header's platform byte, bit 7 masked, is the Mach-O platform: 7 the Simulator, 2 a device, 1
macOS; seen on Xcode 27's output) and copies the two reflection jsons; run again it keeps them all; a key of the map without MSL ends it with 1.
Needs Xcode's metal compiler (skipped without). The device SDK left in place (T_SIM_METAL_DEVICE=1, the broken variant): 2 fail."""
import contextlib, io, os, subprocess, sys, tempfile, types
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); sys.path.insert(0, os.path.join(ROOT, 'gfx', 'tools'))
import ios_metal, sim_metal
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
if subprocess.run(['xcrun', '-sdk', 'iphonesimulator', '--find', 'metal'], capture_output=True).returncode:
    print('skipped: no metal compiler for the iOS Simulator SDK'); sys.exit(0)
if os.environ.get('T_SIM_METAL_DEVICE'):   # the broken variant: the device's SDK and flags left as they are
    sim_metal.METAL, sim_metal.METALLIB = list(ios_metal.METAL), list(ios_metal.METALLIB)
want = ['iphonesimulator' if a == 'iphoneos' else a.replace('-mios-version-min=', '-mios-simulator-version-min=') for a in ios_metal.METAL]
check('flags: iOS\'s with the SDK iphonesimulator and -mios-simulator-version-min, nothing else changed', sim_metal.METAL == want and
      sim_metal.METALLIB == ['iphonesimulator' if a == 'iphoneos' else a for a in ios_metal.METALLIB], ' '.join(sim_metal.METAL))
MSL = '#include <metal_stdlib>\nusing namespace metal;\nfragment float4 main0() { return float4(%s); }\n'
platform = lambda p: open(p, 'rb').read()[11] & 0x7f
with tempfile.TemporaryDirectory() as d:
    mac, out = os.path.join(d, 'b-x'), os.path.join(d, 'sim', 'b-x'); os.makedirs(mac)
    for i, k in enumerate(('00000000000000a1', '00000000000000a2')):
        open(os.path.join(mac, f'{k}_b.metal'), 'w').write(MSL % i); open(os.path.join(mac, f'{k}_b.json'), 'w').write('{"chain": "x", "stage": "ps"}')
    open(os.path.join(mac, '00000000000000a1_i_b.metal'), 'w').write(MSL % 3)
    shmap, fout = os.path.join(d, 'map.tsv'), os.path.join(d, 'fout.tsv')
    open(shmap, 'w').write('# map\n00000000000000a1\tps\tx\n00000000000000a2\tps\ty\n'); open(fout, 'w').write('00000000000000a1 0\n')
    def run(m=shmap):   # sim_metal.main() in this process (its METAL as set above), its exit status and output
        buf = io.StringIO(); sys.argv = ['sim_metal.py', m, mac, out, fout]
        with contextlib.redirect_stdout(buf):
            try: sim_metal.main(); code = 0
            except SystemExit as e: code = e.code or 0
        return types.SimpleNamespace(returncode=code, stdout=buf.getvalue(), stderr='')
    r = run()
    libs = sorted(f for f in os.listdir(out) if f.endswith('.metallib')) if os.path.isdir(out) else []
    check('built: two libraries of the map, one integer variant, two jsons', r.returncode == 0 and libs == ['00000000000000a1_b.metallib', '00000000000000a1_i_b.metallib',
          '00000000000000a2_b.metallib'] and sorted(f for f in os.listdir(out) if f.endswith('.json')) == ['00000000000000a1_b.json', '00000000000000a2_b.json'], r.stdout + r.stderr)
    plats = [platform(os.path.join(out, f)) for f in libs]
    check(f'each library is the iOS Simulator\'s (platform byte 7; got {plats})', plats == [7, 7, 7])
    r = run()
    check('run again: all three kept', r.returncode == 0 and '(0 built, 3 kept, 0 failed)' in r.stdout, r.stdout + r.stderr)
    bad = os.path.join(d, 'bad.tsv'); open(bad, 'w').write('00000000000000a1\tps\tx\n00000000000000ff\tps\tz\n')
    r = run(bad)
    check('a key of the map without MSL: exit 1, named', r.returncode == 1 and '00000000000000ff' in r.stdout, r.stdout + r.stderr)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
