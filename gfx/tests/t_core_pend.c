// Pending buffer writes without a GPU (gfx/nvn/core/pend.c, plan item 264): writes stay until their submission completes; a range a pending write
// reaches is seen (all of them after an overflow, until the overflowing submission completes); a read applies the writes in order: an inline
// update's bytes, a GPU copy as what its source held at that copy (nested copies too); a copy without a known source, an overflow and copies
// nested deeper than four read as unknown.
// A copy reading its source as it is now (-DDM_TEST_PEND_COPY_NOW, the broken variant): 2 fail.
#include "../nvn/core/pend.h"
#include "core_test.h"
#include <stdio.h>
#include <string.h>

static uint32_t mem[64];   // guest memory: words 0, 8, 16, 24 are A, B, C, D
static uint64_t W(int i) { return (uint64_t)(uintptr_t)&mem[i]; }
static uint32_t rd(uint64_t a, int *ok) { uint32_t v = 0; *ok = pend_read_x(a, &v, 4, g_pend.n, 0); return v; }

int main(void) {
    for (int i = 0; i < 64; i++) mem[i] = 0x100 + (uint32_t)i;
    uint32_t x = 0xaaaa, y = 0xbbbb;
    pend_add(W(0), 4, (const uint8_t *)&x, 0, 11);       // submission 11: inline update A = x
    pend_add(W(8), 4, NULL, W(0), 11);                    // GPU copy A -> B
    pend_add(W(0), 4, (const uint8_t *)&y, 0, 12);       // submission 12: A = y, after the copy
    pend_add(W(16), 4, NULL, W(8), 12);                   // copy B -> C
    int ok; uint32_t v;
    v = rd(W(0), &ok); check("A reads the newest update, y", 0xbbbb, ok ? v : 0);
    v = rd(W(8), &ok); check("B reads what A held at the copy, x (not the later y)", 0xaaaa, ok ? v : 0);
    v = rd(W(16), &ok); check("C, copied from B: x through the nested copy", 0xaaaa, ok ? v : 0);
    v = rd(W(24), &ok); check("D, no pending write: memory", 0x118, ok ? v : 0);
    check("a range a pending write reaches is seen; one none reaches is not", 1, pend_hits(W(8), 4) && !pend_hits(W(24), 4) && pend_hits(W(6), 12));
    pend_begin(10); check("submission 11 not complete: its writes stay", 4, g_pend.n);
    pend_begin(11); check("11 complete: 12's two stay", 2, g_pend.n);
    pend_begin(12); check("12 complete: none", 0, g_pend.n);
    pend_add(W(24), 4, NULL, 0, 13); v = rd(W(24), &ok); check("a copy without a known source reads as unknown", 0, ok);
    pend_begin(13);
    for (int i = 0; i <= PEND_MAX; i++) pend_add(W(32), 4, (const uint8_t *)&x, 0, 14);
    check("more writes than the table holds: every range is seen", 1, g_pend.over && pend_hits(W(40), 4));
    v = rd(W(32), &ok); check("... and reads as unknown", 0, ok);
    pend_begin(13); check("... until the overflowing submission completes", 1, g_pend.over);
    pend_begin(14); check("... then not", 0, g_pend.over || g_pend.n);
    for (int i = 0; i < 6; i++) pend_add(W(40 + i), 4, NULL, W(39 + i), 15);   // a chain of six copies
    v = rd(W(41), &ok); check("the first copy of a chain reads its source", 1, ok);
    v = rd(W(45), &ok); check("copies nested deeper than four read as unknown", 0, ok);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
