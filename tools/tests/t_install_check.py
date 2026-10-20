#!/usr/bin/env python3
"""ios/tools/install_check.py (plan item 173): a provisioning profile that expired is refused, one that does not list the device too (unless it
provisions all devices), one expiring within two days passes with a warning; the UDID found by devicectl identifier or by UDID in both JSON shapes
Touchable reads; the app found among the installed ones; the backup manifest holds every file's SHA-256 and not itself. The expiry check skipped
(T_INSTALL_NO_EXPIRY=1, the broken variant): 1 fails."""
import datetime, hashlib, json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import install_check as ic
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
now = datetime.datetime(2026, 10, 20, 12, 0)
day = datetime.timedelta(days=1)
ok, m = ic.check_profile({'ExpirationDate': now - day, 'ProvisionedDevices': ['U1']}, 'U1', now)
check('an expired profile: refused', not ok and 'expired' in m, m)
ok, m = ic.check_profile({'ExpirationDate': now + 5 * day, 'ProvisionedDevices': ['U2']}, 'U1', now)
check('a profile without this device: refused', not ok and 'does not list this device' in m, m)
ok, m = ic.check_profile({'ExpirationDate': now + 5 * day, 'ProvisionsAllDevices': True}, 'U1', now)
check('... one provisioning all devices: fine (5 days)', ok and '5 days' in m, m)
ok, m = ic.check_profile({'ExpirationDate': now + datetime.timedelta(hours=30), 'ProvisionedDevices': ['U1']}, 'U1', now)
check('expiring in 30 hours: passes with a warning', ok and m.startswith('[warning]') and '30 hours' in m, m)
doc = {'result': {'devices': [{'identifier': 'C-1', 'hardwareProperties': {'udid': 'U1'}}, {'identifier': 'C-2', 'properties': {'hardware': {'udid': 'U2'}}}]}}
check('the UDID by devicectl identifier (both shapes) or by UDID; an unknown one: none',
      ic.device_udid(doc, 'C-1') == 'U1' and ic.device_udid(doc, 'C-2') == 'U2' and ic.device_udid(doc, 'U2') == 'U2' and ic.device_udid(doc, 'C-9') is None)
apps = {'result': {'apps': [{'bundleIdentifier': 'org.example.a'}, {'bundleIdentifier': 'org.example.game'}]}}
check('the app among the installed ones', ic.app_installed(apps, 'org.example.game') and not ic.app_installed(apps, 'org.example.other'))
with tempfile.TemporaryDirectory() as d:
    os.makedirs(os.path.join(d, 'save')); open(os.path.join(d, 'save', 'slot.sav'), 'wb').write(b'abc'); open(os.path.join(d, 'env.txt'), 'w').write('x')
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'install_check.py'), 'manifest', d], capture_output=True, text=True)
    m = json.load(open(os.path.join(d, 'manifest.json'))) if r.returncode == 0 else {}
    check('the backup manifest: every file\'s SHA-256, not itself', m == {'env.txt': hashlib.sha256(b'x').hexdigest(), os.path.join('save', 'slot.sav'): hashlib.sha256(b'abc').hexdigest()}, str(m))
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
