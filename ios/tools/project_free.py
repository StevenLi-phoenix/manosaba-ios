#!/usr/bin/env python3
# project.yml for a free (personal team) developer account: drop the five entitlements such a team cannot sign (extended virtual addressing,
# sustained execution, the three iCloud keys). The increased memory limit stays: a personal team's profile carries it (plan item 65, 2026-10-21:
# Xcode made one with it, and the iPhone 17 Pro's limit went from about 3.4 to 6.1 GB), while Apple refuses extended virtual addressing to
# personal teams ("do not support the Extended Virtual Addressing capability"). Every one must be found, or the spec changed and this script
# must follow. DM_TEST_FREE_DROP_MEMORY=1 (negative control of tools/tests/t_ios_cpu.py only) drops the memory limit too, as before.
# Usage: project_free.py <in yml> <out yml>
import os, re, sys
src, dst = sys.argv[1:3]
DROP = [r'kernel\.extended-virtual-addressing', 'sustained-execution', 'icloud-container-identifiers', 'icloud-services', 'ubiquity-container-identifiers']
if os.environ.get('DM_TEST_FREE_DROP_MEMORY'): DROP.append(r'kernel\.increased-memory-limit')
ENT = re.compile(r'^ *com\.apple\.developer\.(' + '|'.join(DROP) + r'):')
L = open(src, encoding='utf-8').read().split('\n')
out = [l for l in L if not ENT.match(l)]
if len(L) - len(out) != len(DROP): sys.exit(f'project_free.py: removed {len(L) - len(out)} entitlement lines, expected {len(DROP)} (did project.yml change?)')
open(dst, 'w', encoding='utf-8').write('\n'.join(out))
