// The Metal 3 build's buffer table (nvn_m3buf.h, plan item 117): 1,537 buffers registered (acnh-mobile's count; Tears of the Kingdom keeps 1,294
// pools alive), each address inside one finds that buffer and the offset into it, the first and last byte included and the byte after not; an
// address between buffers finds none; buffers removed are no longer found while the others still are; one registered again after removals is.
// The old table that stopped at 512 (-DDM_TEST_M3BUF_CAP512): 3 fail.
#include "../nvn/nvn_m3buf.h"
#include <stdio.h>
static int n, fail;
static void check(const char *name, int ok) { n++; fail += !ok; printf("%-78s %s\n", name, ok ? "✓" : "✗"); }
static char tag[1537];   // the "buffers": their addresses stand in for the objects
int main(void) {
    m3buf_tab t = {0}; int added = 0;
    for (int i = 0; i < 1537; i++) {   // buffer k at 0x100000000 + k MiB, 4 KiB + (k % 15) * 64 KiB long (no overlap), registered in a shuffled order
        int k = (i * 769) % 1537; added += !m3buf_add(&t, 0x100000000ull + (uint64_t)k * 0x100000, 0x1000 + (uint64_t)(k % 15) * 0x10000, &tag[k]);
    }
    check("1,537 buffers registered", added == 1537 && t.n == 1537);
    int all = 1;
    for (int k = 0; k < 1537; k++) {
        uint64_t lo = 0x100000000ull + (uint64_t)k * 0x100000, len = 0x1000 + (uint64_t)(k % 15) * 0x10000, off;
        for (uint64_t a = lo; a < lo + len; a += len / 7 + 1) if (m3buf_find(&t, a, &off) != &tag[k] || off != a - lo) all = 0;
        if (m3buf_find(&t, lo + len - 1, &off) != &tag[k]) all = 0;
    }
    check("every buffer's addresses find it with the right offset (first and last byte)", all);
    uint64_t off; check("an address before the first buffer finds none", !m3buf_find(&t, 0xfffff000ull, &off));
    check("the byte after a short buffer finds none (buffer 3: 0x31000 long)", !m3buf_find(&t, 0x100000000ull + 3 * 0x100000 + 0x31000, &off));
    for (int k = 0; k < 1537; k += 3) m3buf_del(&t, &tag[k]);
    int gone = 1, kept = 1;
    for (int k = 0; k < 1537; k++) { void *b = m3buf_find(&t, 0x100000000ull + (uint64_t)k * 0x100000 + 8, &off); if (k % 3 == 0) gone &= b == NULL; else kept &= b == &tag[k] && off == 8; }
    check("removed buffers are no longer found", gone);
    check("the others still are", kept);
    m3buf_add(&t, 0x100000000ull + 6 * 0x100000, 0x2000, &tag[6]);
    check("one registered again after the removals is found", m3buf_find(&t, 0x100000000ull + 6 * 0x100000 + 0x1fff, &off) == &tag[6] && off == 0x1fff);
    printf("M3BUF %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
}
