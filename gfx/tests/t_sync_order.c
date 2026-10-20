// A sync's fence against its flag across threads (nvn_obj.h sync_fence_store / sync_fence_load): one thread fences a sync again and again (as
// nvnQueueFenceSync on the game's thread), another polls it (as nvnSyncWait on another): once the poller sees the sync fenced it must read the
// fence that came with it, never the 0 of a sync not fenced yet (which mtl_wait reports as signalled). The old order, flag first
// (-DDM_TEST_SYNC_FLAG_FIRST), is seen with fence 0 in the window between the two stores.
#include "../nvn/nvn_obj.h"
#include <pthread.h>
#include <stdio.h>

enum { ROUNDS = 1000000 };
static sync_t g_s;
static uint64_t g_round, g_ack, g_bad, g_seen;
static void *poller(void *arg) {
    (void)arg;
    for (uint64_t r = 1; r <= ROUNDS; r++) {
        while (__atomic_load_n(&g_round, __ATOMIC_ACQUIRE) < r) ;
        uint64_t f;
        while (!sync_fence_load(&g_s, &f)) ;
        g_seen++; if (f != r) g_bad++;
        __atomic_store_n(&g_ack, r, __ATOMIC_RELEASE);
    }
    return NULL;
}
int main(void) {
    pthread_t th; pthread_create(&th, NULL, poller, NULL);
    for (uint64_t r = 1; r <= ROUNDS; r++) {
        __atomic_store_n(&g_s.fenced, 0, __ATOMIC_RELAXED); __atomic_store_n(&g_s.fence, 0, __ATOMIC_RELAXED);   // as nvnSyncInitialize
        __atomic_store_n(&g_round, r, __ATOMIC_RELEASE);
        for (volatile int spin = 0; spin < (int)(r % 64); spin++) ;   // the poller is already polling when the fence comes
        sync_fence_store(&g_s, r);
        while (__atomic_load_n(&g_ack, __ATOMIC_ACQUIRE) < r) ;
    }
    pthread_join(th, NULL);
    int ok = g_seen == ROUNDS && g_bad == 0;
    printf("%-72s %s\n", "every fenced sync seen with its own fence", ok ? "✓" : "✗");
    printf("SYNC_ORDER %s rounds %llu, seen fenced with a wrong fence %llu\n", ok ? "OK" : "FAIL", (unsigned long long)g_seen, (unsigned long long)g_bad);
    return !ok;
}
