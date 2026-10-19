// svc 0x0B SleepThread lasts what was asked for: the mean oversleep of 5 ms and 33.3 ms sleeps stays under 0.2 ms and no sleep ends more than
// 0.05 ms early. One plain nanosleep overslept by about a quarter on macOS (1.17 ms on 5 ms, 4.27 ms on 33.3 ms on an M5 at the default QoS).
// Timing test: run it on a machine that is not saturated.
#include "../dm.h"
#include <pthread.h>
#include <time.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static void *guest(void *a) {
    (void)a; svc_thread_main_init();
    static const uint64_t want[] = {5000000, 33333333}; static const int reps[] = {40, 10};
    for (int k = 0; k < 2; k++) {
        double over = 0, early = 0;
        for (int i = 0; i < reps[k]; i++) {
            uint64_t r[32] = {want[k]}, t0 = now(); svc_thread(0x0B, r); double d = (double)(now() - t0) - (double)want[k];
            over += d; if (-d > early) early = -d;
        }
        over /= reps[k];
        CHECK(over < 200000, "SleepThread(%.1f ms): mean oversleep %.3f ms (under 0.2 ms)", want[k] / 1e6, over / 1e6);
        CHECK(early <= 50000, "SleepThread(%.1f ms): never more than 0.05 ms early (earliest %.3f ms)", want[k] / 1e6, early / 1e6);
    }
    return NULL;
}
int main(void) {
    pthread_t t; pthread_create(&t, NULL, guest, NULL); pthread_join(t, NULL);
    printf("t_sleep: %d checks, %d failed\n", n, fail); return fail != 0;
}
