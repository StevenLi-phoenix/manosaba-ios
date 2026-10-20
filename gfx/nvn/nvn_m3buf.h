// The Metal 3 build's buffers by GPU address (plan item 117): the older API binds "buffer object + offset" while the replay holds GPU addresses, so
// every registered buffer (memory pools' zero-copy buffers, argument and staging blocks) is looked up by address range. The table grows as needed
// (it stopped at 512 and dropped the rest: Tears of the Kingdom keeps 1,294 pools alive, so the iOS 18 app would have skipped their binds and
// indexed draws; acnh-mobile c2af194 fixed missing houses and bridges this way, MIT) and is indexed like the pool table (nvn_pool_find.h: the
// last hit, an address cache, a binary search). Not thread-safe: nvn_mtl.m holds g_m3_lk. Checked by gfx/tests/t_m3buf.c
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include "nvn_pool_find.h"

typedef struct { uintptr_t lo, hi; void *buf; } m3buf_ent;   // [lo, hi): the buffer's GPU range (pool_find reads lo, hi first)
typedef struct { m3buf_ent *e; int n, cap; pool_find_t f; } m3buf_tab;

static inline int m3buf_add(m3buf_tab *t, uint64_t gpu, uint64_t len, void *buf) {   // 0, or -1 without memory
#ifdef DM_TEST_M3BUF_CAP512   // negative control of t_m3buf only: the old fixed table
    if (t->n >= 512) return -1;
#endif
    if (t->n == t->cap) { int c = t->cap ? t->cap * 2 : 256; m3buf_ent *p = (m3buf_ent *)realloc(t->e, (size_t)c * sizeof *p); if (!p) return -1; t->e = p; t->cap = c; }
    t->e[t->n++] = (m3buf_ent){(uintptr_t)gpu, (uintptr_t)(gpu + len), buf};
    pool_find_reset(&t->f); return 0;
}
static inline void m3buf_del(m3buf_tab *t, void *buf) {
    for (int i = 0; i < t->n; i++) if (t->e[i].buf == buf) { t->e[i] = t->e[--t->n]; pool_find_reset(&t->f); return; }
}
static inline void *m3buf_find(m3buf_tab *t, uint64_t gpu, uint64_t *off) {   // the buffer holding gpu (off = gpu - its start), or NULL
    *off = 0; if (!gpu || !t->n) return NULL;
    int i = pool_find(&t->f, t->e, sizeof *t->e, t->n, gpu, 1);
    if (i < 0) return NULL;
    *off = gpu - t->e[i].lo; return t->e[i].buf;
}
