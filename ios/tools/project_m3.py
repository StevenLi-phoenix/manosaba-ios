#!/usr/bin/env python3
# Older-API build of project.yml: deployment target iOS 18 and the graphics backend without Metal 4 (DM_NVN_M3, gfx/nvn/nvn_mtl.m), built for A14.
# Usage: project_m3.py <in yml> <out yml>
import sys
src, dst = sys.argv[1:3]; s = open(src, encoding='utf-8').read()
def one(old, new):
    global s
    if s.count(old) != 1: sys.exit(f'project_m3.py: "{old[:40]}" should occur exactly once, found {s.count(old)}')
    s = s.replace(old, new)
one('    iOS: "26.0"', '    iOS: "18.0"')
# -Werror=unguarded-availability-new (plan item 212; acnh-mobile a98d127, MIT): an API newer than iOS 18 used without an @available check stops
# the build instead of a warning among the others (the iOS 18 build of 2026-10-22 had 66 warnings, none of this kind)
import os
WERR = '' if os.environ.get('DM_TEST_M3_NO_WERROR') == '1' else '-Werror=unguarded-availability-new '   # (DM_TEST_M3_NO_WERROR=1: t_ios_cpu's broken variant)
one('OTHER_CFLAGS: $(inherited) -mcpu=apple-a14 ', 'OTHER_CFLAGS: $(inherited) -mcpu=apple-a14 -DDM_NVN_M3=1 ' + WERR)
open(dst, 'w', encoding='utf-8').write(s)
