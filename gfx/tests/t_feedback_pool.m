// The Metal 4 feedback queue's autorelease pool (nvn_mtl.m g_feedback_q, plan item 159; lordran-native 6145d1d's test, MIT, written anew): an object
// autoreleased in one work item is released before the next work item runs. A queue without a pool per work item (-DDM_TEST_FEEDBACK_NO_POOL): 1
// fails. Metal 3 has no feedback queue: nothing to check there
#include "../nvn/nvn_mtl.m"
#include <dlfcn.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
#if DM_NVN_M3
    printf("Metal 3: no feedback queue\n== 0 checks, 0 failed ==\n"); return 0;
#else
    mtl_init();
    __block __weak id probe = nil; __block int alive_next = -1;
    void *(*autorelease_raw)(void *) = dlsym(RTLD_DEFAULT, "objc_autorelease");   // ARC forbids -autorelease; the runtime entry does the same
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(g_feedback_q, ^{
        id o = [NSObject new]; probe = o; autorelease_raw((void *)CFBridgingRetain(o));
        dispatch_async(g_feedback_q, ^{ alive_next = probe != nil; dispatch_semaphore_signal(done); });   // the next work item
    });
    int ok = !dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) && alive_next == 0;
    printf("  %s an object autoreleased in one feedback work item is released before the next runs (%s)\n", ok ? "✓" : "✗", alive_next < 0 ? "no answer" : alive_next ? "still alive" : "released");
    printf("== 1 checks, %d failed ==\n", !ok); return !ok;
#endif
} }
