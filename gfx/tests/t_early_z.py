#!/usr/bin/env python3
"""Early fragment tests (plan item 239): a fragment shader whose NVN control section sets +0x718 bit 0 (the driver's EarlyZForce) is translated with
[[early_fragment_tests]]; one without the bit is not.
  1. The translator: a tiny pixel program (SPH + EXIT) as a job with early_z true and false (dmsh_build.py job_for's field), through
     gfx/tools/dmsh_xlate and spirv-cross with dmsh_build.py's MSL options: the attribute in the first only.
  2. The built library (when the workspace has Tears of the Kingdom's capture and the chain's Mac library, b-<chain> from nvn/dmsh_chain.h): for
     every pixel shader of the capture, the .metal has the attribute exactly when its control section sets the bit (1,677 of them in 1.4.3).
DM_TEST_EARLY_Z_OFF=1 translates part 1's first job without the field, as before: 1 fails.
Usage: t_early_z.py; XLATE = the translator (default $DM_ROOT/build/gfx/dmsh_xlate/dmsh_xlate), needs spirv-cross."""
import glob, json, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
XLATE = os.environ.get('XLATE', f'{DM_ROOT}/build/gfx/dmsh_xlate/dmsh_xlate')
MSL = ['--msl', '--msl-version', '20300', '--msl-texture-buffer-native', '--msl-pad-fragment-output']
EXIT = 0xE30000000007000F
ATTR = re.compile(r'\[\[\s*early_fragment_tests\s*\]\]')   # spirv-cross writes [[ early_fragment_tests ]]
fails = checks = 0

def check(what, ok):
    global fails, checks
    checks += 1; fails += not ok; print(f'{what:<100} {"✓" if ok else "✗"}')

def program():
    sph = [0] * 20; sph[0] = 2 | 3 << 5 | 5 << 10; sph[18] = 0x1   # pixel shader, version 3, colour 0 red
    code = [0x001FC400FE2007F6, EXIT, 0x50B0000000070F00, 0x50B0000000070F00]
    return struct.pack('<20I', *sph) + struct.pack(f'<{len(code)}Q', *code)

def part1():
    work = tempfile.mkdtemp(prefix='t_early_z.')
    p = os.path.join(work, 'p.bin'); open(p, 'wb').write(program())
    jobs = []
    for k, ez in (('on', True), ('off', False)):
        j = {'key': k, 'stage': 'ps', 'code': p, 'fout': {}, 'depth': 'near_is_zero', 'c2': {'tex': [0x20, 1], 'img': [0x120, 1]}, 'debug': False, 'reduced': False}
        if ez and os.environ.get('DM_TEST_EARLY_Z_OFF') != '1': j['early_z'] = True
        jobs.append(j)
    jp = os.path.join(work, 'jobs.json'); json.dump(jobs, open(jp, 'w'))
    env = dict(os.environ)
    if 'DOTNET_ROOT' not in env:
        env['DOTNET_ROOT'] = subprocess.run(['brew', '--prefix', 'dotnet'], capture_output=True, text=True).stdout.strip() + '/libexec'
    r = subprocess.run([XLATE, jp, work], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stdout + r.stderr)
    src = {}
    for k in ('on', 'off'):
        spv, msl = os.path.join(work, k + '.spv'), os.path.join(work, k + '.metal')
        if os.path.exists(spv): subprocess.run(['spirv-cross', *MSL, spv, '--output', msl], check=True, capture_output=True)
        src[k] = open(msl).read() if os.path.exists(msl) else ''
    check('the flag set: the fragment entry gets [[early_fragment_tests]] [DM_TEST_EARLY_Z_OFF=1: none]', bool(ATTR.search(src['on'])))
    check('the flag clear: no [[early_fragment_tests]]', bool(src['off']) and not ATTR.search(src['off']))

def part2():
    gj = os.path.join(os.path.dirname(HERE), '..', 'games', 'totk', 'game.json')
    ws = json.load(open(gj)).get('workspace', {})
    cap = os.path.join(DM_ROOT, ws.get('shader_capture', ''))
    chain = re.search(r'"([0-9a-f]{12})"', open(os.path.join(os.path.dirname(HERE), 'nvn', 'dmsh_chain.h')).read()).group(1)
    lib = os.path.join(DM_ROOT, ws.get('msl_mac', ''), 'b-' + chain)
    if not os.path.isdir(cap) or not os.path.isdir(lib):
        print(f't_early_z: part 2 skipped (no capture at {cap} or no library at {lib})'); return
    want = got = wrong = 0
    for ctl in glob.glob(os.path.join(cap, '*.ctl')):
        key = os.path.basename(ctl)[:-4]; data = open(os.path.join(cap, key + '.data'), 'rb').read(4 + 0x34)
        if data[:4] != bytes.fromhex('78563412'): continue
        w0 = struct.unpack_from('<I', data, 0x30)[0]
        if (w0 & 0x1f, (w0 >> 10) & 0xf) != (2, 5): continue   # pixel shaders only
        m = os.path.join(lib, key + '_b.metal')
        if not os.path.exists(m): continue
        flag = bool(open(ctl, 'rb').read()[0x718] & 1); has = bool(ATTR.search(open(m).read()))
        want += flag; got += has; wrong += flag != has
    check(f'the library b-{chain}: every pixel shader has the attribute exactly when its control section asks ({want} asked, {got} have it)', wrong == 0 and want > 0)

if __name__ == '__main__':
    if not os.path.exists(XLATE): print(f't_early_z: skipped, no translator at {XLATE} (set DM_ROOT or XLATE)'); sys.exit(0)
    part1(); part2()
    print(f'== {checks} checks, {fails} failed =='); sys.exit(fails != 0)
