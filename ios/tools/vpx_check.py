#!/usr/bin/env python3
"""The iOS libvpx checked before it is linked into the app (plan item 62b; ios/tools/build_libvpx.sh builds it): arm64 only, every object built for
iOS (LC_BUILD_VERSION platform 2: not the simulator's 7 or macOS's 1) with a minimum OS no later than the app's, and the VP9 decoder's entry
(_vpx_codec_vp9_dx) defined. A Mac libvpx (Homebrew's) or a simulator one would link with a warning and fail at launch, or not link at all.
Also its NEON code (the VP9 convolution _vpx_convolve8_neon defined) and no assertions (___assert_rtn not referenced): configure once turned both
off silently and the C-only build with assertions decoded a 1080p cutscene at 3 fps on an iPhone 17 Pro (plan item 62b2).
Usage: vpx_check.py <libvpx.a> <deployment target, e.g. 18.0>   exit 0 fine, 1 not, 2 usage."""
import os, re, subprocess, sys

def check(archs, otool, nm, deploy):
    """[problems] from `lipo -archs`, `otool -l` and `nm -g` of the library and the app's deployment target"""
    bad = []
    if archs.split() != ['arm64']: bad.append(f'architectures {archs.strip() or "none"}, not arm64 alone')
    plats = re.findall(r'^\s*platform (\d+)\s*$', otool, re.M)
    mins = re.findall(r'^\s*minos (\d+(?:\.\d+)*)\s*$', otool, re.M)
    if not plats: bad.append('no LC_BUILD_VERSION: not built for a known platform')
    elif set(plats) != {'2'} and not os.environ.get('T_VPX_CHECK_NO_PLATFORM'):   # (the env: the broken variant of tools/tests/t_vpx_check.py)
        bad.append(f'platforms {sorted(set(plats))}, not iOS (2) alone')
    v = lambda s: tuple(int(x) for x in s.split('.'))
    late = sorted({m for m in mins if v(m) > v(deploy)}, key=v) if set(plats) == {'2'} else []
    if late: bad.append(f'objects for iOS {late[-1]}, later than the app\'s {deploy}')
    if not re.search(r'^\S*\s+[TDS] _vpx_codec_vp9_dx$', nm, re.M): bad.append('no _vpx_codec_vp9_dx: the VP9 decoder is not in it')
    if not os.environ.get('T_VPX_CHECK_NO_NEON'):   # (the env: the broken variant of tools/tests/t_vpx_check.py)
        if not re.search(r'^\S*\s+T _vpx_convolve8_neon$', nm, re.M): bad.append('no _vpx_convolve8_neon: built without NEON (C only)')
        if re.search(r'^\s+U ___assert_rtn$', nm, re.M): bad.append('___assert_rtn referenced: built without NDEBUG (assertions compiled in)')
    return bad

if __name__ == '__main__':
    if len(sys.argv) != 3: print(__doc__); sys.exit(2)
    lib, deploy = sys.argv[1:]
    run = lambda *c: subprocess.run(c, capture_output=True, text=True).stdout
    bad = check(run('lipo', '-archs', lib), run('otool', '-l', lib), run('nm', '-g', lib), deploy)
    if bad: print(f'[red] {lib} is not the iOS libvpx the app needs: ' + '; '.join(bad)); sys.exit(1)
    print(f'iOS libvpx checked: {lib} (arm64, iOS {deploy} or earlier, VP9 decoder)')
