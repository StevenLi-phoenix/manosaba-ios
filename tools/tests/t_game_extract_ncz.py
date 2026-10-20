#!/usr/bin/env python3
"""tools/game_extract's NCZ restore (Ncz.cs, plan item 146) through game_extract.sh --selftest-ncz: a synthetic NCA of three sections (none,
AES-CTR, none) made into an NCZ comes back byte for byte; one named after other bytes (the hash check), a block-compressed one and one with a gap
between sections are refused. Needs the .NET SDK (brew install dotnet): without it the test says so and passes.
The hash check skipped (DM_TEST_NCZ_NO_HASH=1, the broken variant): 1 fails."""
import os, shutil, subprocess, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
dn = shutil.which('dotnet') or ('/opt/homebrew/bin/dotnet' if os.path.exists('/opt/homebrew/bin/dotnet') else None)
if not dn:
    print('no .NET SDK: skipped'); sys.exit(0)
env = dict(os.environ)
if 'DOTNET_ROOT' not in env:   # Homebrew's (game_extract.sh's default), else the folder of the dotnet found
    r = subprocess.run(['brew', '--prefix', 'dotnet'], capture_output=True, text=True) if shutil.which('brew') else None
    lib = r.stdout.strip() + '/libexec' if r and r.returncode == 0 else ''
    env['DOTNET_ROOT'] = lib if lib and os.path.isdir(lib) else os.path.dirname(os.path.realpath(dn))
r = subprocess.run(['bash', os.path.join(ROOT, 'tools', 'game_extract.sh'), '--selftest-ncz'], capture_output=True, text=True, env=env)
print(r.stdout, end=''); print(r.stderr, end='', file=sys.stderr)
ok = r.returncode == 0 and '== 4 checks, 0 failed ==' in r.stdout
print(f'== game_extract --selftest-ncz: {"passed" if ok else "failed"} ==')
sys.exit(0 if ok else 1)
