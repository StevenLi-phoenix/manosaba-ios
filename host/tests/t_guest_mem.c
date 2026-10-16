// Physical memory in the alias region (hle_svc.c alias_phys, svc MapPhysicalMemory / UnmapPhysicalMemory): 4 KB guest pages on 16 KB host pages.
//   (1) hand-written cases: a host page opens with its first mapped subpage and stays open while any subpage is mapped; an unmapped subpage reads
//       zero while its neighbours keep their contents; a host page with no mapped subpage left is replaced, so mapping it again reads zero; mapping a
//       page twice or unmapping a page that is not mapped changes nothing;
//   (2) the counters behind dm_alias_stat (mapped 4 KB pages, host pages in use) against a reference model over 20000 random map / unmap calls,
//       with ranges that start and end inside host pages, plus the accessibility of every host page the model says is in use;
//   (3) everything unmapped: both counters back to zero.
// Stack region (svc MapMemory / UnmapMemory, the SDK moving a thread's stack from the heap):
//   (4) the contents move in and back; after UnmapMemory a host page no other mapping touches is inaccessible and no longer resident (it used to stay
//       a resident copy: about 6 MB per scene load in manosaba), a host page shared with a neighbouring mapping stays open with the neighbour's data;
//   (5) two threads mapping and unmapping neighbours that share a host page, 3000 times each: every thread always sees its own data.
// Physical memory limit (svc MapPhysicalMemory, as mesosphere KPageTableBase::MapPhysicalMemory): with an application pool of 285 MB
//   (6) mapping up to the pool succeeds; one page more is LimitReached (0x10801) and maps nothing; pages already mapped do not count again;
//       after an unmap the freed amount can be mapped again.
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hle_svc.c"

struct dm_flags g_flag; uint64_t g_t0_ns; module_t g_mod[MAX_MOD]; int g_nmod;
uint64_t now_ns(void) { return 0; }
uint64_t guest_ns(void) { return 0; }
int svc_thread(int num, uint64_t *regs) { (void)num; (void)regs; return 0; }
module_t *module_of(const void *a) { (void)a; return NULL; }
void *module_sym(module_t *m, const char *n) { (void)m; (void)n; return NULL; }
void dm_backtrace(uint64_t fp) { (void)fp; }
void hlecount_svc(int n) { (void)n; }
uint64_t dm_entropy64(void) { return 0; }
void thr_counts(int *a, int *b, int *c) { *a = *b = *c = 0; }

static int checks, fails;
#define CHECK(c, ...) do { checks++; int ok_ = !!(c); fails += !ok_; if (!ok_ || verbose) { printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int verbose = 1;
static sigjmp_buf g_jb; static volatile int g_probing;
static void on_fault(int s) { (void)s; if (g_probing) siglongjmp(g_jb, 1); signal(s, SIG_DFL); }
static int readable(volatile uint8_t *p) {   // 1 = the byte can be read (the host page is open)
    g_probing = 1; int ok = 0;
    if (!sigsetjmp(g_jb, 1)) { (void)*p; ok = 1; }
    g_probing = 0; return ok;
}
static void map(size_t page, size_t n) { alias_phys(g_alias + page * 0x1000, n * 0x1000, 1, SIZE_MAX); }
static void unmap(size_t page, size_t n) { alias_phys(g_alias + page * 0x1000, n * 0x1000, 0, SIZE_MAX); }

static uint64_t svc(int num, uint64_t a0, uint64_t a1, uint64_t a2) { uint64_t r[32] = {a0, a1, a2}; svc_dispatch(num, r, 0); return r[0]; }
static int resident(void *p) { char v = 0; return mincore(p, 1, &v) == 0 && (v & MINCORE_INCORE); }
static _Atomic int g_mt_bad;
static void *mapper(void *arg) {   // thread 0: 0x40000..0x45000 (host pages 0x40000, 0x44000); thread 1: 0x45000..0x48000 (host page 0x44000)
    int me = (int)(intptr_t)arg; uint8_t *d = g_stack + (me ? 0x45000 : 0x40000); size_t n = me ? 0x3000 : 0x5000;
    uint8_t *s = aligned_alloc(16384, 0x8000);
    for (int it = 0; it < 3000; it++) {
        memset(s, me * 0x40 + (it & 0x3f), n);
        if (svc(0x04, (uint64_t)d, (uint64_t)s, n)) { g_mt_bad++; continue; }
        for (int r = 0; r < 4; r++) { if (d[0] != s[0] || d[n - 1] != s[0]) { g_mt_bad++; break; } d[n / 2] = s[0]; }
        if (svc(0x05, (uint64_t)d, (uint64_t)s, n)) g_mt_bad++;
    }
    free(s); return NULL;
}
enum { NP = 4096 };   // pages of the alias region the random test uses (16 MB)
static uint8_t ref[NP];
static size_t ref_mapped(void) { size_t n = 0; for (int i = 0; i < NP; i++) n += ref[i]; return n; }
static size_t ref_hpages(size_t sub) { size_t n = 0; for (size_t h = 0; h < NP; h += sub) { int u = 0; for (size_t k = h; k < h + sub; k++) u |= ref[k]; n += u; } return n; }

int main(void) {
    signal(SIGBUS, on_fault); signal(SIGSEGV, on_fault);
    mem_init();
    size_t sub = vm_page_size / 0x1000;
    printf("host page %zu KB, %zu guest pages each\n", (size_t)vm_page_size >> 10, sub);

    printf("1 hand-written cases\n");
    map(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "map page 0: 1 page mapped, 1 host page in use (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    memset(g_alias, 0xa1, 0x1000);
    map(1, 1);
    CHECK(g_alias_mapped == 2 && g_alias_hpages == 1, "map page 1 (same host page): 2 mapped, still 1 host page (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    memset(g_alias + 0x1000, 0xb2, 0x1000);
    map(0, 1);
    CHECK(g_alias_mapped == 2 && g_alias_hpages == 1, "map page 0 again: no change (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    unmap(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "unmap page 0: 1 mapped, the host page stays in use for page 1 (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    CHECK(g_alias[0] == 0 && g_alias[0xfff] == 0 && g_alias[0x1000] == 0xb2 && g_alias[0x1fff] == 0xb2, "page 0 reads zero, page 1 kept its contents");
    unmap(0, 1);
    CHECK(g_alias_mapped == 1 && g_alias_hpages == 1, "unmap page 0 again (not mapped): no change (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    unmap(1, 1);
    CHECK(g_alias_mapped == 0 && g_alias_hpages == 0, "unmap page 1: nothing mapped, no host page in use (%zu, %zu)", g_alias_mapped, g_alias_hpages);
    CHECK(!readable(g_alias), "the host page is closed again");
    map(1, 1);
    CHECK(readable(g_alias + 0x1000) && g_alias[0x1000] == 0 && g_alias[0x1fff] == 0, "mapping page 1 again reads zero (fresh memory)");
    unmap(1, 1);
    if (sub > 1) {   // a range that starts and ends inside host pages: pages sub-1 .. 2*sub (3 host pages)
        map(sub - 1, sub + 2);
        CHECK(g_alias_mapped == sub + 2 && g_alias_hpages == 3, "map %zu pages from page %zu: 3 host pages in use (%zu, %zu)", sub + 2, sub - 1, g_alias_mapped, g_alias_hpages);
        unmap(sub, sub);
        CHECK(g_alias_mapped == 2 && g_alias_hpages == 2, "unmap the middle host page's pages: 2 mapped in 2 host pages (%zu, %zu)", g_alias_mapped, g_alias_hpages);
        CHECK(!readable(g_alias + sub * 0x1000) && readable(g_alias + (sub - 1) * 0x1000) && readable(g_alias + 2 * sub * 0x1000), "the middle host page is closed, its neighbours open");
        unmap(0, 3 * sub);
    }
    size_t mb, hmb; map(0, 1024); dm_alias_stat(&mb, &hmb);
    CHECK(mb == 4 && hmb == 4, "dm_alias_stat after mapping 4 MB: %zu MB mapped in %zu MB of host pages", mb, hmb);
    unmap(0, 1024);

    printf("2 random map / unmap against a reference model\n");
    srand(12345); int bad = 0, closed_bad = 0; verbose = 0;
    for (int it = 0; it < 20000; it++) {
        size_t p = (size_t)rand() % NP, n = 1 + (size_t)rand() % (rand() % 8 ? 8 : 96); if (p + n > NP) n = NP - p;
        int m = rand() % 2;
        alias_phys(g_alias + p * 0x1000, n * 0x1000, m, SIZE_MAX);
        for (size_t i = p; i < p + n; i++) ref[i] = (uint8_t)m;
        if (g_alias_mapped != ref_mapped() || g_alias_hpages != ref_hpages(sub)) { if (!bad++) printf("first mismatch at call %d: mapped %zu (model %zu), host pages %zu (model %zu)\n", it, g_alias_mapped, ref_mapped(), g_alias_hpages, ref_hpages(sub)); }
        if (it % 1000 == 999) for (size_t h = 0; h < NP; h += sub) { int u = 0; for (size_t k = h; k < h + sub; k++) u |= ref[k]; if (readable(g_alias + h * 0x1000) != u) closed_bad++; }
    }
    verbose = 1;
    CHECK(!bad, "counters equal the model after every call (%d mismatches)", bad);
    CHECK(!closed_bad, "every host page is open exactly when the model has a mapped subpage in it (%d wrong)", closed_bad);

    printf("3 everything unmapped\n");
    unmap(0, NP);
    CHECK(g_alias_mapped == 0 && g_alias_hpages == 0, "both counters back to zero (%zu, %zu)", g_alias_mapped, g_alias_hpages);

    printf("4 MapMemory / UnmapMemory\n");
    uint8_t *src = aligned_alloc(16384, 0x20000), *srcb = aligned_alloc(16384, 0x4000); for (int i = 0; i < 0x20000; i++) src[i] = (uint8_t)(i * 7 + 1);
    uint8_t *dst = g_stack + 0x10000;
    CHECK(svc(0x04, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && !memcmp(dst, src, 0x10000), "map 64 KB: the stack region holds the source's contents");
    memset(dst, 0x5a, 0x10000);
    CHECK(svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && src[0] == 0x5a && src[0xffff] == 0x5a, "unmap: the contents moved back to the source");
    CHECK(!readable(dst) && !readable(dst + 0xc000) && !resident(dst) && !resident(dst + 0xc000), "unmap: the stack region's pages are inaccessible and not resident");
    CHECK(svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000) == 0xD401, "unmapping it again: InvalidAddress, nothing touched");
    for (int i = 0; i < 0x10000; i++) src[i] = (uint8_t)(i * 3 + 2);
    CHECK(svc(0x04, (uint64_t)dst, (uint64_t)src, 0x10000) == 0 && !memcmp(dst, src, 0x10000), "map again at the same place: the new contents, nothing stale");
    svc(0x05, (uint64_t)dst, (uint64_t)src, 0x10000);
    uint8_t *a = g_stack + 0x20000, *b = g_stack + 0x25000;   // a = 0x20000..0x25000 (host pages 0x20000 and 0x24000), b = 0x25000..0x28000 (host page 0x24000)
    memset(srcb, 0xbb, 0x3000);
    CHECK(svc(0x04, (uint64_t)a, (uint64_t)src, 0x5000) == 0 && svc(0x04, (uint64_t)b, (uint64_t)srcb, 0x3000) == 0, "map two neighbours that share a host page");
    CHECK(svc(0x05, (uint64_t)a, (uint64_t)src, 0x5000) == 0, "unmap the first");
    CHECK(!readable(a) && readable(b) && b[0] == 0xbb && b[0x2fff] == 0xbb, "its own host page is closed; the shared one stays open with the neighbour's data");
    CHECK(svc(0x05, (uint64_t)b, (uint64_t)srcb, 0x3000) == 0 && !readable(b) && !readable(g_stack + 0x24000), "unmap the neighbour: the shared host page is closed too");

    printf("5 two threads on neighbours sharing a host page\n");
    pthread_t th[2]; for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, mapper, (void *)(intptr_t)i);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    CHECK(!g_mt_bad, "3000 map / write / check / unmap rounds on each thread: %d wrong", g_mt_bad);

    printf("6 physical memory limit\n");
    setenv("DM_APP_POOL_ADD_MB", "-3000", 1);   // pool 3285 - 3000 = 285 MB; no images or heap in this test: the alias region may hold 285 MB
    uint64_t A = (uint64_t)g_alias;
    CHECK(svc(0x2C, A, 280ull << 20, 0) == 0 && g_alias_mapped == (280u << 8), "map 280 MB: success (%zu pages)", g_alias_mapped);
    CHECK(svc(0x2C, A + (280ull << 20), 8ull << 20, 0) == 0x10801 && g_alias_mapped == (280u << 8), "8 MB more: LimitReached, nothing mapped (%zu pages)", g_alias_mapped);
    CHECK(svc(0x2C, A + (276ull << 20), 10ull << 20, 0) == 0x10801 && g_alias_mapped == (280u << 8), "10 MB of which 4 MB are already mapped (6 MB new, 286 MB in all): LimitReached");
    CHECK(svc(0x2C, A + (277ull << 20), 8ull << 20, 0) == 0 && g_alias_mapped == (285u << 8), "8 MB of which 3 MB are already mapped: exactly the limit, success");
    CHECK(svc(0x2C, A, 16ull << 20, 0) == 0, "mapping pages that are all mapped already: success");
    CHECK(svc(0x2C, A + (285ull << 20), 0x1000, 0) == 0x10801, "one page past the limit: LimitReached");
    CHECK(svc(0x2D, A, 100ull << 20, 0) == 0 && svc(0x2C, A + (285ull << 20), 100ull << 20, 0) == 0 && g_alias_mapped == (285u << 8), "unmap 100 MB, then 100 MB elsewhere fits again");
    svc(0x2D, A, 400ull << 20, 0);
    CHECK(g_alias_mapped == 0, "everything unmapped");

    printf("t_guest_mem: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
