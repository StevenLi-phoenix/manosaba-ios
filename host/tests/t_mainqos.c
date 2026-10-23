// The guest main thread's QoS class (hle_thread.c svc_thread_main_init, plan item 229): started on a thread made without one, as the iOS app's boot
// thread, it keeps the class it had unless DM_MAIN_QOS=ui, which makes it user-interactive, as every other guest thread. Run twice by make run: as
// it is and with --ui. The switch ignored (-DDM_TEST_MAIN_QOS_IGNORED, with --ui) fails 1.
#include "../dm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
static qos_class_t before, after;
static void *boot(void *a) { (void)a; int rp = 0; pthread_get_qos_class_np(pthread_self(), &before, &rp); svc_thread_main_init(); pthread_get_qos_class_np(pthread_self(), &after, &rp); return NULL; }
int main(int argc, char **argv) {
    int ui = argc > 1 && !strcmp(argv[1], "--ui"), fail = 0;
    if (ui) setenv("DM_MAIN_QOS", "ui", 1); else unsetenv("DM_MAIN_QOS");
    pthread_t t; if (pthread_create(&t, NULL, boot, NULL)) return 2; pthread_join(t, NULL);
    unsigned want = ui ? QOS_CLASS_USER_INTERACTIVE : before; int ok = (unsigned)after == want; fail += !ok;
    printf("%-84s expected %#-6x → got %#-6x %s\n", ui ? "DM_MAIN_QOS=ui: the main thread user-interactive [-DDM_TEST_MAIN_QOS_IGNORED: as it was]"
                                                     : "no switch: the main thread keeps the class of the thread it runs on", want, (unsigned)after, ok ? "✓" : "✗");
    printf("== 1 checks, %d failed ==\n", fail); return fail != 0;
}
