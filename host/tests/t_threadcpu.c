// Guest-thread CPU by Switch priority for the power line (hle_thread.c dm_thread_cpu, nxnative 451e118): a guest thread at priority 44 busy for 300 ms
// shows about 1000 ms/s in the <=44 band; after SetThreadPriority 50 (svc 0x0D) the next busy 300 ms land in 45-59; 300 ms asleep at 63 show about
// nothing (each report counts only the CPU since the last one). The thread is listed as the busiest with its priority.
#include "../dm.h"
#include <time.h>
#include <unistd.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void dm_thread_cpu(char *out, size_t n, double secs);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static double now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e9; }
static void busy(double s) { double t0 = now(); volatile uint64_t x = 0; while (now() - t0 < s) x++; }
static void bands(const char *line, double b[3]) { b[0] = b[1] = b[2] = -1; sscanf(line, "by Switch priority <=44 %lf / 45-59 %lf / >=60 %lf", &b[0], &b[1], &b[2]); }
int main(void) {
    uint32_t me = svc_thread_main_init(); char line[400]; double b[3];
    dm_thread_cpu(line, sizeof line, 1);   // baseline
    double t0 = now(); busy(0.3); dm_thread_cpu(line, sizeof line, now() - t0); bands(line, b);
    char want[32]; snprintf(want, sizeof want, "%#x", me);
    CHECK(b[0] > 700 && b[0] < 1100 && b[1] == 0 && b[2] == 0, "busy 300 ms at priority 44: <=44 %.0f ms/s (700-1100), the others 0 [%s]", b[0], line);
    CHECK(strstr(line, want) && strstr(line, "priority 44"), "the busiest thread is this one, at priority 44");
    { uint64_t r[32] = {0xFFFF8000u, 50}; svc_thread(0x0D, r); }
    t0 = now(); busy(0.3); dm_thread_cpu(line, sizeof line, now() - t0); bands(line, b);
    CHECK(b[1] > 700 && b[1] < 1100 && b[0] == 0, "after SetThreadPriority 50: 45-59 %.0f ms/s, <=44 %.0f", b[1], b[0]);
    { uint64_t r[32] = {0xFFFF8000u, 63}; svc_thread(0x0D, r); }
    t0 = now(); usleep(300000); dm_thread_cpu(line, sizeof line, now() - t0); bands(line, b);
    CHECK(b[0] >= 0 && b[0] + b[1] + b[2] < 50, "asleep 300 ms at 63: %.0f ms/s in all (under 50)", b[0] + b[1] + b[2]);
    printf("t_threadcpu: %d checks, %d failed\n", n, fail); return fail != 0;
}
