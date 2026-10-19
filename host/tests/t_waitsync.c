// svc 0x18 WaitSynchronization with a pending CancelSynchronization, in Horizon's order (KSynchronizationObject::Wait, checked against Atmosphère:
// termination, a signaled object, timeout 0, then the cancel flag, which is cleared): a signaled handle returns its index and a poll returns
// timed out, both keeping the flag, and only a wait that would block returns cancelled. A thread blocked in the wait when it is cancelled
// returns cancelled and keeps no flag. (LASRecomp 1144aea found the order on the same symptom; ours checked the flag first.)
#include "../dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static int n, fail;
#define EXPECT(name, want, got) do { unsigned long long w_ = (want), g_ = (got); n++; printf("%-74s expected %#-8llx → got %#-8llx %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
enum { R_TIMEOUT = 0xEA01, R_CANCELLED = 0xEC01 };
static uint32_t g_th, g_rd; static _Atomic int g_waiting; static uint64_t g_blocked_rc = 1;
static uint64_t wait1(uint32_t h, int64_t ns, uint64_t *idx) { uint64_t r[8] = {0, (uint64_t)&h, 1, (uint64_t)ns}; svc_thread(0x18, r); if (idx) *idx = r[1]; return r[0]; }
static void cancel(uint32_t th) { uint64_t r[8] = {th}; svc_thread(0x19, r); }
static void *guest(void *a) {
    (void)a; g_th = svc_thread_main_init();
    uint64_t r[8] = {0}; svc_thread(0x45, r); uint32_t wr = (uint32_t)r[1]; g_rd = (uint32_t)r[2];
    cancel(g_th);                                                                            // not waiting: the flag is set
    uint64_t s[8] = {wr}; svc_thread(0x11, s);                                               // signal
    uint64_t idx = 9; EXPECT("cancel pending, handle signaled: success (index 0)", 0, wait1(g_rd, -1, &idx)); EXPECT("... index", 0, idx);
    uint64_t c[8] = {g_rd}; svc_thread(0x12, c);                                             // clear
    EXPECT("cancel pending, nothing signaled, timeout 0: timed out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    EXPECT("cancel pending, a wait that would block: cancelled", R_CANCELLED, wait1(g_rd, 1000000, NULL));
    EXPECT("the flag is used up: the next poll times out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    atomic_store(&g_waiting, 1); g_blocked_rc = wait1(g_rd, -1, NULL);                        // cancelled while blocked (main thread)
    EXPECT("cancelled while blocked: cancelled", R_CANCELLED, g_blocked_rc);
    EXPECT("... and no flag is left: the next poll times out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    return NULL;
}
int main(void) {
    pthread_t t; pthread_create(&t, NULL, guest, NULL);
    while (!atomic_load(&g_waiting)) usleep(1000);
    usleep(50000); cancel(g_th);
    pthread_join(t, NULL);
    printf("t_waitsync: %d checks, %d failed\n", n, fail); return fail != 0;
}
