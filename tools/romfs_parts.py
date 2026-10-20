#!/usr/bin/env python3
"""A RomFS tree as parts of at most 1 GiB with an index, to carry to the iOS app (plan item 62e; the idea from totk-mobile and acnh-mobile, which
move their game data as a few large files: Tears of the Kingdom's RomFS is tens of thousands of files, 17 GB, slow and fragile to copy one by
one). The app unpacks them into Application Support/romfs at its next launch, checking each part's SHA-256 first and deleting each part once its
files are written (host/romfs_parts.c; an unpacking cut short goes on at the next launch), then validates the tree as before (plan item 175).

  romfs_parts.py pack <tree> <out dir> [--part-mb N]   writes romfs.000.dmrp ... and romfs.dmri (N: 1024 by default; --part-bytes N for tests)
  romfs_parts.py check <out dir>                       the parts against the index

A part is a run of records: "DMRF", the path's length (u32 LE), the file's size (u64 LE), the path (UTF-8, '/' separated, relative), the bytes.
Files go in sorted order; a part ends before the file that would take it past N MiB (a larger file is a part by itself). The index is text:
  dmri 1 <files> <bytes> <parts>
  <part name> <bytes> <files> <sha256>        one line per part, in order
Exit 0 fine, 1 a problem (named), 2 usage."""
import hashlib, os, struct, sys

MAGIC = b'DMRF'

def files_of(tree):
    out = []
    for root, dirs, files in os.walk(tree):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f); rel = os.path.relpath(p, tree).replace(os.sep, '/')
            if os.path.islink(p) or not os.path.isfile(p): continue
            out.append((rel, os.path.getsize(p)))
    return sorted(out)

def pack(tree, out, limit=1024 << 20):
    files = files_of(tree)
    os.makedirs(out, exist_ok=True)
    parts, cur, cur_n, cur_bytes, h = [], None, 0, 0, None
    def close():
        nonlocal cur
        if cur: cur.close(); parts.append((name, cur_bytes, cur_n, h.hexdigest())); cur = None
    for rel, size in files:
        rec = len(MAGIC) + 12 + len(rel.encode()) + size
        if cur and cur_bytes + rec > limit: close()
        if not cur:
            name = f'romfs.{len(parts):03d}.dmrp'; cur = open(os.path.join(out, name), 'wb'); cur_n = cur_bytes = 0; h = hashlib.sha256()
        head = MAGIC + struct.pack('<IQ', len(rel.encode()), size) + rel.encode()
        cur.write(head); h.update(head)
        with open(os.path.join(tree, rel), 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 22), b''): cur.write(chunk); h.update(chunk)
        cur_n += 1; cur_bytes += rec
    close()
    with open(os.path.join(out, 'romfs.dmri'), 'w') as f:
        f.write(f'dmri 1 {len(files)} {sum(s for _, s in files)} {len(parts)}\n')
        for p in parts: f.write(f'{p[0]} {p[1]} {p[2]} {p[3]}\n')
    return files, parts

def check(out):
    lines = open(os.path.join(out, 'romfs.dmri')).read().split('\n')
    head = lines[0].split()
    if len(head) != 5 or head[:2] != ['dmri', '1']: return [f'not an index: {lines[0]!r}']
    bad = []
    for line in lines[1:1 + int(head[4])]:
        name, size, n, sha = line.split()
        p = os.path.join(out, name)
        if not os.path.exists(p): bad.append(f'{name}: missing'); continue
        if os.path.getsize(p) != int(size): bad.append(f'{name}: {os.path.getsize(p)} bytes, the index says {size}'); continue
        h = hashlib.sha256()
        with open(p, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 22), b''): h.update(chunk)
        if h.hexdigest() != sha: bad.append(f'{name}: SHA-256 differs')
    return bad

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) in (3, 5) and a[0] == 'pack' and (len(a) == 3 or a[3] in ('--part-mb', '--part-bytes')):
        files, parts = pack(a[1], a[2], (int(a[4]) << 20 if a[3] == '--part-mb' else int(a[4])) if len(a) == 5 else 1024 << 20)
        print(f'{len(files)} files, {sum(s for _, s in files) / 1e9:.2f} GB in {len(parts)} parts: {a[2]}'); sys.exit(0)
    if len(a) == 2 and a[0] == 'check':
        bad = check(a[1])
        if bad: print('[red] ' + '; '.join(bad)); sys.exit(1)
        print(f'parts match the index: {a[1]}'); sys.exit(0)
    print(__doc__); sys.exit(2)
