#!/usr/bin/env python3
"""Public-content audit (plan item 111; the repository is public; bayonetta3-mobile tools/audit_public.py's idea, MIT, the code is ours): every file
git tracks (the index; --history also every blob reachable from any ref) is checked before it can be published. Refused: the user's own game data,
keys and console backups by path (keys/, dump/, work/, sd_backup_*, RomFS / ExeFS folders, *.keys, PRODINFO, BISKEY) and by extension (game
containers, executables, signing material, media, archives, Metal libraries); content that starts like a game container, an executable or an
archive; key and token patterns (a "name_key = <32+ hex digits>" line, a title key "rights id = title key" line, private keys, GitHub / AWS
tokens); binary content (not UTF-8, or a NUL); a file over 3 MiB; a symlink or submodule. Findings name the file and the rule, never the value.
Usage: audit_public.py [--history] [--repo <dir>]   (exit 1 with findings)   Unit test: tools/tests/t_audit_public.py"""
import argparse, os, re, subprocess, sys
from pathlib import PurePosixPath

PRIVATE_DIRS = {'keys', 'dump', 'work', 'extracted', 'analysis', 'romfs', 'exefs'}
PRIVATE_NAME = re.compile(r'(?i)^(sd_backup_.*|prod\.keys|title\.keys|dev\.keys|.*\.keys|prodinfo.*|biskey.*|\.env(?!\.example$).*)$')
FORBIDDEN_EXT = set('nsp nsz xci xcz nca ncz tik cert nso nro npdm pfs0 hfs0 bin img sav zs zst sarc pack bfres bntx bnsh byml bars bfstm bwav '
                    'metallib air ipa o a dylib so dll exe p12 pfx pem key cer mobileprovision provisionprofile mp4 webm mov wav mp3 ogg opus '
                    'zip 7z rar gz xz png jpg jpeg log'.split())
MAGIC = (b'PFS0', b'HFS0', b'NSO0', b'NRO0', b'PK\x03\x04', b'\x28\xb5\x2f\xfd', b'\x1f\x8b', b'7z\xbc\xaf\x27\x1c', b'Rar!',
         b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\x7fELF', b'MZ', b'BNTX', b'BNSH', b'FRES', b'SARC', b'Yaz0', b'MTLB')
SECRETS = (
    re.compile(rb'(?im)^\s*[a-z0-9_]*(?:key|kek|seed)[a-z0-9_]*\s*=\s*[0-9a-f]{32,}\s*$'),   # prod.keys lines (master_key_00 = ..., header_key = ...)
    re.compile(rb'(?im)^\s*[0-9a-f]{32}\s*=\s*[0-9a-f]{32}\s*$'),                              # title.keys lines (rights id = title key)
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----'),
    re.compile(rb'\bgh[pousr]_[A-Za-z0-9]{30,}\b'), re.compile(rb'\bgithub_pat_[A-Za-z0-9_]{50,}\b'), re.compile(rb'\bAKIA[0-9A-Z]{16}\b'),
)
MAX = 3 << 20

def path_error(path):
    p = PurePosixPath(path)
    if any(part.lower() in PRIVATE_DIRS for part in p.parts[:-1]): return 'a private folder (game data, keys, workspace)'
    if PRIVATE_NAME.match(p.name): return 'a private file name (keys, console backup, local environment)'
    if p.suffix.lower().lstrip('.') in FORBIDDEN_EXT: return f'a file type never published ({p.suffix})'
    return None

def content_error(data):
    if os.environ.get('DM_TEST_AUDIT_NO_CONTENT') == '1': return None   # broken variant for t_audit_public only
    if data.startswith(MAGIC) or data[0x200:0x204] in (b'NCA2', b'NCA3'): return 'starts like a game container, executable or archive'
    if any(r.search(data) for r in SECRETS): return 'a key or token pattern (value not shown)'
    if b'\0' in data: return 'binary content (a NUL byte)'
    try: data.decode('utf-8')
    except UnicodeDecodeError: return 'binary content (not UTF-8)'
    return None

def audit(repo, history=False):
    """[(path, reason)], number of blobs read, bytes read"""
    git = lambda *a, **k: subprocess.run(['git', '-C', repo, *a], capture_output=True, check=True, **k).stdout
    found, blobs = [], set()
    for e in filter(None, git('ls-files', '--stage', '-z').split(b'\0')):
        meta, path = e.split(b'\t', 1); mode, oid, stage = meta.decode().split(); path = path.decode('utf-8', 'replace')
        if mode not in ('100644', '100755') or stage != '0': found.append((path, 'a symlink, submodule or unmerged entry')); continue
        blobs.add((oid, path))
    if history:
        for line in git('rev-list', '--objects', '--all').decode('utf-8', 'replace').splitlines():
            oid, _, path = line.partition(' ')
            if path: blobs.add((oid, path))
    order = sorted(blobs)
    info = git('cat-file', '--batch-check', input=''.join(o + '\n' for o, _ in order).encode()).decode().splitlines()
    read = []
    for (oid, path), inf in zip(order, info):
        f = inf.split()
        if len(f) != 3 or f[1] != 'blob': continue
        why = path_error(path) or (f'over {MAX >> 20} MiB' if int(f[2]) > MAX else None)
        if why: found.append((path, why))
        else: read.append((oid, path))
    total = 0
    if read:
        out = git('cat-file', '--batch', input=''.join(o + '\n' for o, _ in read).encode()); at = 0
        for oid, path in read:
            nl = out.index(b'\n', at); size = int(out[at:nl].split()[2]); data = out[nl + 1:nl + 1 + size]; at = nl + 2 + size; total += size
            why = content_error(data)
            if why: found.append((path, why))
    return sorted(set(found)), len(read), total

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--history', action='store_true', help='also every blob reachable from any ref'); ap.add_argument('--repo', default='.')
    a = ap.parse_args()
    found, n, total = audit(a.repo, a.history)
    for path, why in found: print(f'[red] refused: {path}: {why}')
    if found: sys.exit(f'public-content audit: {len(found)} finding(s); no value printed')
    print(f'public-content audit: {n} files, {total / 1048576:.1f} MiB, nothing refused ({"the index and all history" if a.history else "the index"})')

if __name__ == '__main__':
    main()
