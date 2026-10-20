// A pool registered on a host page that another pool has already write-protected (nvn_dirty.c, plan item 118; acnh-mobile c2af194's
// t_dirty_shared.c, MIT, as is but for the checks being named): pool A takes a whole host page and is protected, a pool registered in a
// recycled slot shares that page (adj, at +4 KiB); the game writes into adj: both A and adj must see the write (adj's range started "not
// protected" on a page that was, so the fault stamped only A's). Then 5,000 rounds of protect / remove / add on the shared page while another
// thread keeps writing into it: no hang, no crash, nothing left registered. The fault stamping only the range that held the protection
// (-DDM_TEST_DIRTY_STAMP_ONE, as before): 2 fail.
#include "../nvn/nvn_dirty.h"
#include "../../host/dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
module_t g_mod[MAX_MOD]; int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);
static volatile unsigned char *mem;
static _Atomic int done;
static int n, fail;
static void check(const char *name, int ok) { n++; fail += !ok; printf("%-80s %s\n", name, ok ? "✓" : "✗"); }
static void *writer(void *p) { (void)p; unsigned char x = 0; while (!atomic_load(&done)) *mem = ++x; return NULL; }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_install(); size_t pg = (size_t)getpagesize();
    mem = mmap(0, pg * 3, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    void *a = (void *)mem, *tmp = (void *)(mem + 2 * pg), *adj = (void *)(mem + 4096);
    nvn_dirty_add_range(tmp, pg); nvn_dirty_add_range(a, pg);
    nvn_dirty_protect(a, pg); nvn_dirty_remove_range(tmp); nvn_dirty_add_range(adj, 4096);   // adj takes tmp's slot, on A's protected page
    uint64_t stamp = nvn_dirty_now(); mem[4096] = 42;
    check("a write into adj on A's protected page: A sees it", nvn_dirty_since(a, pg, stamp));
    check("... and adj sees it too", nvn_dirty_since(adj, 4096, stamp));
    nvn_dirty_remove_range(adj);
    check("adj removed: A is still tracked", nvn_dirty_tracked(a, pg));
    nvn_dirty_add_range(tmp, pg);
    pthread_t t; pthread_create(&t, 0, writer, 0);
    for (int i = 0; i < 5000; i++) {
        nvn_dirty_protect(a, pg); nvn_dirty_remove_range(tmp); nvn_dirty_add_range(adj, 4096);
        nvn_dirty_protect(adj, 4096); nvn_dirty_remove_range(adj); nvn_dirty_add_range(tmp, pg);
    }
    atomic_store(&done, 1); pthread_join(t, 0); nvn_dirty_remove_range(a); nvn_dirty_remove_range(tmp);
    int live, peak, cap; nvn_dirty_stat(&live, &peak, &cap);
    check("5,000 rounds of protect / remove / add with a writer on the page: none left registered", live == 0);
    printf("DIRTY_SHARED %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
}
