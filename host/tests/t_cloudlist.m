// The first iCloud listing from the main thread before any run loop (cloud.m listing_query, as the Mac app calls dm_cloud_boot from main.c): the
// query must start before the call returns and the call must not wait for a block it sent to the main queue it is blocking. No iCloud account is
// needed: an unsigned process's query fails to start or gathers nothing, and either way it has started (here it answered in 0.07 s; the time is
// printed, not checked, as a query nobody answers runs to the limit). Sent to the main queue from the main thread (-DDM_TEST_CLOUD_ALWAYS_DISPATCH)
// it never starts and the call waits the whole limit: 1 fails.
#include "../cloud/cloud.m"
#include <stdio.h>
module_t g_mod[MAX_MOD]; int g_nmod;
uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
void (*g_dm_save_commit_hook)(const char *commit_dir);
static int checks, fails;
#define CHECK(c, s) do { checks++; int ok = !!(c); fails += !ok; printf("%-74s %s\n", s, ok ? "✓" : "✗"); } while (0)
int main(void) { @autoreleasepool {
    int n = -1, pr = -1; uint64_t t0 = now_ns();
    listing_query(1.0, &n, &pr);
    double w = (now_ns() - t0) / 1e9;
    CHECK(atomic_load(&g_listing_started) == 1, "the query started on the main thread before the call returned");
    CHECK(n >= 0 && pr >= 0 && pr <= n, "the counts are filled in (packs listed, on this device)");
    printf("t_cloudlist: %d checks, %d failed (%.2f s, %d listed)\n", checks, fails, w, n);
    return fails != 0;
} }
