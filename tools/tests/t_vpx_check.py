#!/usr/bin/env python3
"""ios/tools/vpx_check.py (plan item 62b): the iOS libvpx accepted (arm64, iOS objects for 18.0, the VP9 decoder defined); refused: a Mac one
(platform 1), a simulator one (7), objects for iOS 26.0 when the app is for 18.0, a fat arm64 + x86_64 one, one where the VP9 decoder is only
referenced; and the library ios/tools/build_libvpx.sh built, when it is there. The platform not checked (T_VPX_CHECK_NO_PLATFORM=1, the broken
variant): 2 fail."""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import vpx_check as vc
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def otool(platform, minos, n=2):
    return ''.join(f'libvpx.a(o{i}.o):\nLoad command 1\n      cmd LC_BUILD_VERSION\n  cmdsize 24\n platform {platform}\n    minos {minos}\n      sdk 27.0\n' for i in range(n))
NM = 'libvpx.a(vp9_dx_iface.c.o):\n0000000000001234 T _vpx_codec_vp9_dx\n0000000000000000 T _vpx_codec_vp8_dx\n'
b = vc.check('arm64\n', otool(2, '18.0'), NM, '18.0')
check('arm64, iOS objects for 18.0, the VP9 decoder defined: accepted', b == [], b)
b = vc.check('arm64\n', otool(1, '18.0'), NM, '18.0')
check('a Mac one (platform 1): refused', any('not iOS' in x for x in b), b)
b = vc.check('arm64\n', otool(7, '18.0'), NM, '18.0')
check('a simulator one (platform 7): refused', any('not iOS' in x for x in b), b)
b = vc.check('arm64\n', otool(2, '18.0') + otool(2, '26.0', 1), NM, '18.0')
check('one object for iOS 26.0 when the app is for 18.0: refused', any('26.0' in x for x in b), b)
b = vc.check('x86_64 arm64\n', otool(2, '18.0'), NM, '18.0')
check('fat arm64 + x86_64: refused', any('arm64 alone' in x for x in b), b)
b = vc.check('arm64\n', otool(2, '18.0'), 'libvpx.a(x.o):\n                 U _vpx_codec_vp9_dx\n', '18.0')
check('the VP9 decoder only referenced (U): refused', any('VP9 decoder' in x for x in b), b)
lib = os.path.join(os.environ.get('DM_ROOT', os.path.join(ROOT, 'work')), 'build', 'ios', 'libvpx', 'lib', 'libvpx.a')
if os.path.exists(lib):
    import subprocess
    run = lambda *c: subprocess.run(c, capture_output=True, text=True).stdout
    b = vc.check(run('lipo', '-archs', lib), run('otool', '-l', lib), run('nm', '-g', lib), '18.0')
    check('the library build_libvpx.sh built: accepted', b == [], b)
print(f'== {total} checks, {fails} failed ==')
sys.exit(fails != 0)
