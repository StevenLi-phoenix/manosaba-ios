#!/usr/bin/env python3
"""RED_ONCE tags (gfx/nvn/nvn_mtl.m, plan item 125): a red line printed once per tag, so a tag shared by two unrelated messages silenced the second
for good once the first fired (13 of 74 tags were). Every RED_ONCE in gfx/nvn has a numeric tag and a message literal, each tag one message (the
same message may repeat), and every tag fits the g_red_once array. A copy with a duplicated tag (T_RED_ONCE_SRC=<file>, by hand) fails 1."""
import glob, os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
srcs = [os.environ['T_RED_ONCE_SRC']] if os.environ.get('T_RED_ONCE_SRC') else sorted(glob.glob(os.path.join(ROOT, 'gfx', 'nvn', '*.[cmh]')))
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<84} {"✓" if ok else "✗"}')
tags, bad, size = {}, [], None
for p in srcs:
    s = open(p, encoding='utf-8').read()
    m = re.search(r'static int g_red_once\[(\d+)\]', s)
    if m: size = int(m.group(1))
    for c in re.finditer(r'RED_ONCE\(', s):
        if s[c.start() - 8:c.start()] == '#define ': continue
        m = re.match(r'RED_ONCE\((\d+),\s*"((?:[^"\\]|\\.)*)"', s[c.start():])
        line = s.count('\n', 0, c.start()) + 1
        if not m: bad.append(f'{os.path.basename(p)}:{line}'); continue
        tags.setdefault(int(m.group(1)), set()).add(m.group(2))
check(f'every RED_ONCE has a numeric tag and a message literal ({sum(1 for _ in tags)} tags) {bad or ""}', not bad and len(tags) > 50)
shared = {k: v for k, v in tags.items() if len(v) > 1}
check(f'each tag one message {sorted(shared) or ""}', not shared)
check(f'every tag fits g_red_once[{size}] (highest {max(tags) if tags else -1})', size is not None and tags and max(tags) < size)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
