// The player's pipeline list written off the calling thread (nvn_mtl.m pso_user_record, plan item 246, after AnySwitch c483c04): with the writer's
// queue held, a recorded line is not in the file yet (the submit thread did no file work); once the queue runs it is there once; the same line
// again is not repeated; 200 lines from two threads all arrive. Written on the calling thread (-DDM_TEST_PSO_USER_INLINE, as before): 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-84s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static int lines_of(const char *path, const char *prefix) {
    FILE *f = fopen(path, "r"); if (!f) return 0; char b[256]; int n = 0;
    while (fgets(b, sizeof b, f)) n += !strncmp(b, prefix, strlen(prefix)); fclose(f); return n;
}
static void *rec_many(void *a) { int base = (int)(intptr_t)a; for (int i = 0; i < 100; i++) pso_user_record("S\tk%d\t%d\n", base + i, 0); return NULL; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[512]; snprintf(path, sizeof path, "%s/t_psouser_%d.tsv", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); unlink(path); setenv("DM_PSO_USER", path, 1);
    dispatch_suspend(pso_user_queue());
    pso_user_record("R\t%s\n", "abc");
    EXPECT("the writer's queue held: the line is not in the file yet [-DDM_TEST_PSO_USER_INLINE: 1]", 0, lines_of(path, "R\tabc"));
    dispatch_resume(pso_user_queue()); mtl_pso_user_sync();
    EXPECT("... once it runs: there, once", 1, lines_of(path, "R\tabc"));
    pso_user_record("R\t%s\n", "abc"); mtl_pso_user_sync();
    EXPECT("the same line again: not repeated", 1, lines_of(path, "R\tabc"));
    pthread_t t1, t2; pthread_create(&t1, NULL, rec_many, (void *)0); pthread_create(&t2, NULL, rec_many, (void *)100); pthread_join(t1, NULL); pthread_join(t2, NULL);
    mtl_pso_user_sync();
    EXPECT("200 lines from two threads: all there", 200, lines_of(path, "S\tk"));
    unlink(path);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
