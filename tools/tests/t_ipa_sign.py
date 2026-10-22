#!/usr/bin/env python3
"""ios/tools/ipa_sign.py (plan item 211): an app bundle (a copy of /usr/bin/true as its executable) signed ad hoc carries the increased memory
limit in its signature and nothing else; the size note says nothing up to 4 GiB and names the 16.6 GB refusal and the RomFS parts above.
Left unsigned (DM_TEST_IPA_UNSIGNED=1, the broken variant): 1 fails."""
import os, plistlib, shutil, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import ipa_sign as s
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
with tempfile.TemporaryDirectory() as d:
    app = os.path.join(d, 'Payload', 'DmIOS.app'); os.makedirs(app)
    shutil.copy('/usr/bin/true', os.path.join(app, 'DmIOS'))
    with open(os.path.join(app, 'Info.plist'), 'wb') as f: plistlib.dump({'CFBundleExecutable': 'DmIOS', 'CFBundleIdentifier': 'test.ipa.sign', 'CFBundlePackageType': 'APPL'}, f)
    rc = s.sign(app); got = s.signed_entitlements(app)
    check('signed ad hoc: the signature carries the increased memory limit, nothing else', rc == 0 and got == {'com.apple.developer.kernel.increased-memory-limit': True}, str(got))
check('the size note: nothing to say at 3 GB', s.size_note(3_000_000_000) == '3.00 GB')
n = s.size_note(16_600_000_000)
check('the size note above 4 GiB: ZIP64, the 16.6 GB refusal and the RomFS parts', 'ZIP64' in n and '16.6 GB' in n and 'romfs_parts.py' in n, n)
print(f't_ipa_sign: {total} checks, {fails} failed'); sys.exit(fails != 0)
