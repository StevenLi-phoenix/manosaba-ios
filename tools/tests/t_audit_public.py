#!/usr/bin/env python3
"""tools/audit_public.py on a scratch git repository: plain sources pass; refused are a prod.keys file, a file under keys/ and under work/, a
.nso, a source with a "master_key_00 = <64 hex>" line, one with a title key line, a file that starts like an NSO, a file with a NUL, a symlink,
and (with --history) a key file that was committed and then deleted. No refusal prints the key value. Then the real repository's index passes.
Without the content rules (DM_TEST_AUDIT_NO_CONTENT=1, the broken variant) 4 fail."""
import os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TOOL = os.path.join(ROOT, 'tools', 'audit_public.py')
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<84} {"✓" if ok else "✗"}')
KEY = 'ab' * 32   # made up: 64 hex digits that are no key
def run(repo, *a):
    r = subprocess.run([sys.executable, '-I', TOOL, '--repo', repo, *a], capture_output=True, text=True); return r.returncode, r.stdout + r.stderr
with tempfile.TemporaryDirectory() as td:
    g = lambda *a: subprocess.run(['git', '-C', td, *a], capture_output=True, check=True)
    g('init', '-q'); g('config', 'user.email', 't@example.invalid'); g('config', 'user.name', 't')
    def put(path, data):
        p = os.path.join(td, path); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data if isinstance(data, bytes) else data.encode())
    put('src/a.c', 'int main(void) { return 0; }\n'); put('README.md', 'Ünïcode text is fine.\n')
    g('add', '-A'); code, out = run(td)
    check('plain sources pass', code == 0 and 'nothing refused' in out)
    put('prod.keys', 'x\n'); put('keys/notes.txt', 'x\n'); put('work/build.txt', 'x\n'); put('game/main.nso', 'x\n')
    put('src/keys.c', f'// a pasted key file\nmaster_key_00 = {KEY}\n'); put('src/title.txt', f'{KEY[:32]} = {KEY[32:]}\n')
    put('src/blob.txt', b'NSO0' + b'\0' * 60); put('src/nul.c', b'int x;\0\n'); os.symlink('a.c', os.path.join(td, 'src', 'link.c'))
    g('add', '-A'); code, out = run(td)
    for f, why in [('prod.keys', 'private file name'), ('keys/notes.txt', 'private folder'), ('work/build.txt', 'private folder'), ('game/main.nso', 'file type'),
                   ('src/link.c', 'symlink')]:
        check(f'refused by path: {f} ({why})', f'refused: {f}: ' in out and why in out.split(f'refused: {f}: ')[1].split('\n')[0])
    check('refused by content: a "master_key_00 = <hex>" line', 'refused: src/keys.c: a key or token pattern' in out)
    check('refused by content: a title key line', 'refused: src/title.txt: a key or token pattern' in out)
    check('refused by content: starts like an NSO, a NUL byte', 'refused: src/blob.txt: starts like' in out and 'refused: src/nul.c: binary content' in out)
    check('no key value printed, exit 1', code == 1 and KEY not in out and KEY[:32] not in out)
    for f in ('prod.keys', 'keys/notes.txt', 'work/build.txt', 'game/main.nso', 'src/keys.c', 'src/title.txt', 'src/blob.txt', 'src/nul.c', 'src/link.c'): os.remove(os.path.join(td, f))
    put('src/old.c', f'header_key = {KEY}\n'); g('add', '-A'); g('commit', '-qm', 'a key by mistake'); os.remove(os.path.join(td, 'src', 'old.c')); g('add', '-A'); g('commit', '-qm', 'removed')
    code, out = run(td); code_h, out_h = run(td, '--history')
    check('a key committed then deleted: the index passes, --history refuses it', code == 0 and code_h == 1 and 'refused: src/old.c: a key or token pattern' in out_h)
code, out = run(ROOT)
check(f'this repository\'s index passes ({out.strip().split(chr(10))[-1][:60]})', code == 0)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
