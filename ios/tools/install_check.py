#!/usr/bin/env python3
"""Checks before ios/run.sh install (plan item 173, after Touchable's internal/tools/launcher.py install, MIT, written anew):
  install_check.py profile <app> <udid>   the app's embedded.mobileprovision (security cms -D): refused when it has expired or does not list the
                                          device (unless it provisions all devices); a warning when it expires within two days (free accounts: 7 days)
  install_check.py udid <devicectl json> <identifier or udid>   the device's hardware UDID from `devicectl list devices --json-output`
  install_check.py installed <devicectl json> <bundle id>        exit 0 when `devicectl device info apps --json-output` lists the app
  install_check.py manifest <dir>         <dir>/manifest.json: the SHA-256 of every file below it (the save backup run.sh takes first)
Exit 0 when fine, 1 when refused or not found, 2 on a usage error."""
import datetime, hashlib, json, os, plistlib, subprocess, sys

def check_profile(profile, udid, now):
    """(ok, message) for a decoded provisioning profile (a dict as plistlib gives it), the device's UDID and now (naive UTC)"""
    exp = profile.get('ExpirationDate')
    if not isinstance(exp, datetime.datetime): return False, 'the provisioning profile has no expiration date'
    if exp <= now and not os.environ.get('T_INSTALL_NO_EXPIRY'):   # (the env: the broken variant of tools/tests/t_install_check.py)
        return False, f'the signature expired on {exp:%Y-%m-%d %H:%M} UTC: build again'
    if not profile.get('ProvisionsAllDevices') and udid not in profile.get('ProvisionedDevices', []):
        return False, f'the provisioning profile does not list this device ({udid or "no UDID"}): register it in Xcode, then build again'
    left = exp - now
    if left < datetime.timedelta(days=2): return True, f'[warning] the signature expires in {left.total_seconds() / 3600:.0f} hours ({exp:%Y-%m-%d %H:%M} UTC)'
    return True, f'the signature is valid until {exp:%Y-%m-%d} ({left.days} days)'

def device_udid(doc, ident):
    for dev in doc.get('result', {}).get('devices', []):
        hw = dev.get('properties', {}).get('hardware', dev.get('hardwareProperties', {})); udid = hw.get('udid')   # (both shapes, as Touchable reads them)
        if ident in (dev.get('identifier'), udid): return udid
    return None

def app_installed(doc, bundle):
    return any(a.get('bundleIdentifier') == bundle for a in doc.get('result', {}).get('apps', []))

def manifest(d):
    out = {}
    for root, _, files in os.walk(d):
        for f in files:
            p = os.path.join(root, f)
            if os.path.relpath(p, d) == 'manifest.json': continue
            h = hashlib.sha256()
            with open(p, 'rb') as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b''): h.update(chunk)
            out[os.path.relpath(p, d)] = h.hexdigest()
    return out

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == 'profile':
        p = os.path.join(a[1], 'embedded.mobileprovision')
        r = subprocess.run(['security', 'cms', '-D', '-i', p], capture_output=True)
        if r.returncode: print(f'[red] cannot read {p}'); sys.exit(1)
        ok, msg = check_profile(plistlib.loads(r.stdout), a[2], datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None))
        print(msg if ok else f'[red] {msg}'); sys.exit(0 if ok else 1)
    if len(a) == 3 and a[0] in ('udid', 'installed'):
        doc = json.load(open(a[1]))
        if a[0] == 'udid':
            u = device_udid(doc, a[2]); print(u or ''); sys.exit(0 if u else 1)
        sys.exit(0 if app_installed(doc, a[2]) else 1)
    if len(a) == 2 and a[0] == 'manifest':
        m = manifest(a[1]); json.dump(m, open(os.path.join(a[1], 'manifest.json'), 'w'), indent=1, sort_keys=True); print(f'{len(m)} files'); sys.exit(0)
    print(__doc__); sys.exit(2)
