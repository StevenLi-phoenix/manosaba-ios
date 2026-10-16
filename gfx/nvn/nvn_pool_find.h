// Address → memory pool lookup (unit test tests/t_pool_find.c), after nxnative d6cd8a8 (seedds, MIT): the last hit first, then a binary search
// over pool indices sorted by start address, and the old full scan only when both miss (pools that overlap). Breath of the Wild keeps about
// 1,760 pools alive and looks one up for every buffer binding of every draw; nxnative measured its submit time going from 21 to 7 ms per frame.
// The pools are an array of entries that begin with uintptr_t lo, hi (stride = entry size). Adding or removing a pool must call
// pool_find_reset (the index is sorted again when next used). Not thread-safe: the caller holds the pool table's lock.
#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef struct { int *idx; int cap, sorted, last; uint64_t rescued; } pool_find_t;   // rescued: lookups only the full scan answered (overlapping pools)

static inline uintptr_t pool_find_lo_(const void *pools, size_t stride, int i) { return ((const uintptr_t *)((const char *)pools + stride * (size_t)i))[0]; }
static inline uintptr_t pool_find_hi_(const void *pools, size_t stride, int i) { return ((const uintptr_t *)((const char *)pools + stride * (size_t)i))[1]; }
typedef struct { const void *pools; size_t stride; } pool_find_ctx_;
static inline int pool_find_cmp_(void *ctx, const void *a, const void *b) {
    const pool_find_ctx_ *c = ctx; uintptr_t x = pool_find_lo_(c->pools, c->stride, *(const int *)a), y = pool_find_lo_(c->pools, c->stride, *(const int *)b);
    return x < y ? -1 : x > y;
}
static inline void pool_find_reset(pool_find_t *f) { f->sorted = 0; f->last = -1; }
static inline int pool_find_contains_(const void *pools, size_t stride, int i, uint64_t addr, uint64_t len) {
    return addr >= pool_find_lo_(pools, stride, i) && addr + len <= pool_find_hi_(pools, stride, i);
}
// The index of a pool holding [addr, addr + len), or -1
static inline int pool_find(pool_find_t *f, const void *pools, size_t stride, int n, uint64_t addr, uint64_t len) {
    if (f->last >= 0 && f->last < n && pool_find_contains_(pools, stride, f->last, addr, len)) return f->last;
    if (!f->sorted) {
        if (f->cap < n) { int *p = realloc(f->idx, (size_t)n * 2 * sizeof *p); if (!p) goto scan; f->idx = p; f->cap = n * 2; }
        for (int i = 0; i < n; i++) f->idx[i] = i;
        pool_find_ctx_ c = {pools, stride}; qsort_r(f->idx, (size_t)n, sizeof *f->idx, &c, pool_find_cmp_); f->sorted = 1;
    }
    {   // the last pool whose start ≤ addr
        int lo = 0, hi = n - 1, hit = -1;
        while (lo <= hi) { int m = lo + (hi - lo) / 2; if (pool_find_lo_(pools, stride, f->idx[m]) <= addr) { hit = m; lo = m + 1; } else hi = m - 1; }
        if (hit >= 0 && pool_find_contains_(pools, stride, f->idx[hit], addr, len)) return f->last = f->idx[hit];
    }
scan:
    for (int i = 0; i < n; i++) if (pool_find_contains_(pools, stride, i, addr, len)) { f->rescued++; return f->last = i; }
    return -1;
}
