#!/usr/bin/env python3
"""Reads the memory snapshots of DM_MEMSNAP (host/memsnap.c has the file format). Run with the workspace Python (numpy).
  memsnap.py summary A.bin             resident bytes per named range (prefix, alias, stack, heap, <module>.data)
  memsnap.py diff A.bin B.bin          per range: pages only in B (new), only in A (gone), in both with other bytes (changed); the largest new runs
  memsnap.py refs A.bin ADDR [LEN]     every 8-byte aligned word whose value lies in [ADDR, ADDR + LEN) (default LEN 8): who points into an object
  memsnap.py class A.bin ADDR...       the IL2CPP class of the object at each address
IL2CPP class names (plan item 141, as host/il2cpp_obj.h reads them: an object starts with its Il2CppClass *, the name and namespace pointers at
+0x10 and +0x18): refs names the object each hit lies in (the nearest object start up to 0x200 bytes before it, "near": a field holding a class
pointer can look like one), and diff counts, by class, the objects that start in the largest new runs (words that are a class pointer).
Checked by tools/tests/t_memsnap.py"""
import bisect, mmap, re, struct, sys
import numpy as np

RANGE = struct.Struct('<QQ16s')
IDENT = re.compile(r'[A-Za-z_<$][A-Za-z0-9_`<>.=$|+-]*')   # host/il2cpp_obj.h il2cpp_ident

class Snap:
    def __init__(self, path):
        self.f = open(path, 'rb'); self.mm = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        if self.mm[:8] != b'DMSNAP01': raise ValueError(f'{path}: not a DM_MEMSNAP file')
        self.frame, nr, _ = struct.unpack_from('<QII', self.mm, 8)
        self.ranges = [(lo, hi, name.rstrip(b'\0').decode()) for lo, hi, name in (RANGE.unpack_from(self.mm, 24 + i * RANGE.size) for i in range(nr))]
        self.records = []   # (address, length, file offset)
        o = 24 + nr * RANGE.size
        while o < len(self.mm):
            a, n = struct.unpack_from('<QQ', self.mm, o); o += 16
            if o + n > len(self.mm): raise ValueError(f'{path}: record at {a:#x} runs past the end')
            self.records.append((a, n, o)); o += n
        self.records.sort(); self.starts = [a for a, _, _ in self.records]; self.klass = {}
    def read(self, a, n):
        """n bytes at address a from one record, or None"""
        i = bisect.bisect_right(self.starts, a) - 1
        if i < 0: return None
        ra, rn, ro = self.records[i]
        return self.mm[ro + a - ra:ro + a - ra + n] if a + n <= ra + rn else None
    def u64(self, a):
        b = self.read(a, 8); return struct.unpack('<Q', b)[0] if b else None
    def cstr(self, a, n=95):
        out = b''
        while len(out) < n:
            b = self.read(a, 16 - a % 16)
            if b is None: return None
            if b'\0' in b: return (out + b[:b.index(b'\0')]).decode('latin-1')
            out += b; a += len(b)
        return None
    def class_name(self, klass):
        """'Namespace.Name' of an Il2CppClass at klass, or None (cached)"""
        if klass in self.klass: return self.klass[klass]
        r = None
        if klass >= 0x10000 and not klass & 7:
            np_, ns = self.u64(klass + 0x10), self.u64(klass + 0x18)
            nm = self.cstr(np_) if np_ and np_ >= 0x10000 else None
            nsn = (self.cstr(ns) if ns else '') if nm is not None else None
            if nm and nsn is not None and IDENT.fullmatch(nm) and (nsn == '' or IDENT.fullmatch(nsn)): r = f'{nsn}.{nm}' if nsn else nm
        self.klass[klass] = r; return r
    def object_class(self, obj):
        k = self.u64(obj) if obj >= 0x10000 and not obj & 7 else None
        return self.class_name(k) if k else None
    def name(self, a):
        for lo, hi, nm in self.ranges:
            if lo <= a < hi: return nm
        return '?'
    def pages(self, pg):
        """address → file offset, per page"""
        return {a + i: o + i for a, n, o in self.records for i in range(0, n, pg)}

def page_size(*snaps):
    """the smallest record length and alignment seen (the host's page size: 16 KB on Apple silicon)"""
    pg = 1 << 14
    for s in snaps:
        for a, n, _ in s.records:
            while pg > 4096 and (a % pg or n % pg): pg >>= 1
    return pg

def summary(s):
    out = {nm: 0 for _, _, nm in s.ranges}
    for a, n, _ in s.records: out[s.name(a)] = out.get(s.name(a), 0) + n
    return out

def diff(a, b):
    """per range: {'new', 'gone', 'changed', 'same'} in bytes, and the new runs (address, length) largest first"""
    pg = page_size(a, b); pa, pb = a.pages(pg), b.pages(pg)
    out = {}
    def add(nm, k): out.setdefault(nm, {'new': 0, 'gone': 0, 'changed': 0, 'same': 0})[k] += pg
    for p, ob in pb.items():
        oa = pa.get(p)
        if oa is None: add(b.name(p), 'new')
        else: add(b.name(p), 'same' if a.mm[oa:oa + pg] == b.mm[ob:ob + pg] else 'changed')
    for p in pa:
        if p not in pb: add(a.name(p), 'gone')
    runs = []   # [address, length] of adjacent new pages
    for p in sorted(p for p in pb if p not in pa):
        if runs and runs[-1][0] + runs[-1][1] == p: runs[-1][1] += pg
        else: runs.append([p, pg])
    return out, sorted((tuple(r) for r in runs), key=lambda r: -r[1])

def refs(s, lo, n=8):
    """(word address, value) for every aligned word with lo <= value < lo + n"""
    hits = []
    for a, ln, o in s.records:
        v = np.frombuffer(s.mm, dtype='<u8', count=ln // 8, offset=o)
        for i in np.nonzero((v >= np.uint64(lo)) & (v < np.uint64(lo + n)))[0]: hits.append((a + 8 * int(i), int(v[i])))
    return hits

def container(s, w, back=0x200):
    """(object start, class) of the nearest object start at or before word w, within back bytes, or None"""
    for a in range(w - w % 8, max(w - back, 0) - 1, -8):
        c = s.object_class(a)
        if c: return a, c
    return None

def class_census(s, runs, limit=64 << 20):
    """{class: objects} starting in the runs (words that are a class pointer), the first limit bytes of them"""
    out = {}; left = limit
    for a, n in runs:
        n = min(n, left); left -= n
        if n <= 0: break
        for ra, rn, ro in s.records:   # the part of each record inside [a, a + n)
            lo, hi = max(a, ra), min(a + n, ra + rn)
            if lo >= hi: continue
            v, cnt = np.unique(np.frombuffer(s.mm, dtype='<u8', count=(hi - lo) // 8, offset=ro + lo - ra), return_counts=True)
            for k, c in zip(v.tolist(), cnt.tolist()):
                if k < 0x10000 or k & 7: continue
                nm = s.class_name(k)
                if nm: out[nm] = out.get(nm, 0) + c
    return out

def mb(n): return f'{n / 1048576:9.1f} MB'

def main(argv):
    if len(argv) >= 2 and argv[0] == 'summary':
        s = Snap(argv[1]); print(f'frame {s.frame}')
        for nm, n in summary(s).items(): print(f'  {nm:<16} {mb(n)}')
    elif len(argv) >= 3 and argv[0] == 'diff':
        a, b = Snap(argv[1]), Snap(argv[2]); d, runs = diff(a, b)
        print(f'frame {a.frame} → {b.frame}          new         gone      changed         same')
        for nm, v in d.items(): print(f'  {nm:<16} {mb(v["new"])} {mb(v["gone"])} {mb(v["changed"])} {mb(v["same"])}')
        print('largest new runs:'); [print(f'  {r:#x} +{n:#x} ({b.name(r)})') for r, n in runs[:20]]
        cc = class_census(b, runs[:20])
        if cc: print('IL2CPP objects starting in them, by class:'); [print(f'  {c:8d}  {nm}') for nm, c in sorted(cc.items(), key=lambda t: -t[1])[:20]]
    elif len(argv) >= 3 and argv[0] == 'refs':
        s = Snap(argv[1]); lo = int(argv[2], 0); n = int(argv[3], 0) if len(argv) > 3 else 8
        hits = refs(s, lo, n)
        for w, v in hits[:500]:
            o = container(s, w)
            print(f'  {w:#x} ({s.name(w)}) → {v:#x} (+{v - lo:#x})' + (f'  near {o[1]} at {o[0]:#x}+{w - o[0]:#x}' if o else ''))
        print(f'{len(hits)} word(s) point into [{lo:#x}, {lo + n:#x})')
    elif len(argv) >= 3 and argv[0] == 'class':
        s = Snap(argv[1])
        for x in argv[2:]: a = int(x, 0); print(f'  {a:#x}: {s.object_class(a) or "not an IL2CPP object"}')
    else:
        print(__doc__); return 2
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
