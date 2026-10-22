#!/usr/bin/env python3
"""The unsigned IPA's app signed ad hoc with the entitlements a sideloading signer should ask for (plan item 211; LNPANDY/nxnative-romfs-archive
c3bccb5, MIT, written anew): AltStore takes the App ID features it turns on from the entitlements in the app's own signature (AltSign's
ALTApplication.entitlementsString); unsigned, the app had none and kept the default memory limit (LNPANDY: about 3,375 MB on a 12 GB iPhone with
AltStore Classic 2.3; Hades II ran with the entitlement). Only the increased memory limit: a free (personal team) account signs it (plan item
65), not extended virtual addressing, sustained execution or iCloud. A signer that brings its own profile replaces this signature.
  ipa_sign.py sign <App.app>    codesign --sign - with ENTITLEMENTS, then read them back from the signature; exit 1 if either fails
  ipa_sign.py size <file.ipa>   its size, and a note above 4 GiB: such a zip needs ZIP64, and iOS refused a 16.6 GB IPA with the whole of Tears
                                of the Kingdom's RomFS (LNPANDY 13d9191: PackageExtractionFailed); tools/romfs_parts.py carries it instead
Exit 0 fine, 1 a problem (named), 2 usage."""
import os, plistlib, subprocess, sys, tempfile

ENTITLEMENTS = {'com.apple.developer.kernel.increased-memory-limit': True}
NOTE_BYTES = 4 << 30

def signed_entitlements(app):   # the entitlements in the app's signature, {} when none
    r = subprocess.run(['codesign', '-d', '--entitlements', '-', '--xml', app], capture_output=True)
    if r.returncode or not r.stdout.strip(): return {}
    try: return plistlib.loads(r.stdout)
    except Exception: return {}

def sign(app):
    if os.environ.get('DM_TEST_IPA_UNSIGNED') == '1': return 0   # negative control of tools/tests/t_ipa_sign.py only: left unsigned, as before
    with tempfile.NamedTemporaryFile('wb', suffix='.plist', delete=False) as f: plistlib.dump(ENTITLEMENTS, f); ent = f.name
    try:
        r = subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', '--entitlements', ent, app], capture_output=True, text=True)
    finally: os.unlink(ent)
    if r.returncode: print(f'[red] ipa_sign: codesign failed: {r.stderr.strip()[-300:]}'); return 1
    got = signed_entitlements(app)
    if any(got.get(k) != v for k, v in ENTITLEMENTS.items()): print(f'[red] ipa_sign: the signature does not carry {sorted(ENTITLEMENTS)}: {got}'); return 1
    print(f'signed ad hoc with {", ".join(sorted(ENTITLEMENTS))}'); return 0

def size_note(n):
    gb = n / 1e9
    if n <= NOTE_BYTES: return f'{gb:.2f} GB'
    return (f'{gb:.2f} GB: above 4 GiB (a ZIP64 archive); iOS refused a 16.6 GB IPA holding the whole RomFS (PackageExtractionFailed, LNPANDY '
            f'13d9191): if the install fails, build without DM_IPA_DATA and carry the RomFS as parts (tools/romfs_parts.py, ios/README.md)')

if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('sign', 'size'): print(__doc__); sys.exit(2)
    if sys.argv[1] == 'sign': sys.exit(sign(sys.argv[2]))
    print(size_note(os.path.getsize(sys.argv[2]))); sys.exit(0)
