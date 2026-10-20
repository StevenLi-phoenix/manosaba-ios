// The pipeline prediction table (nvn_mtl.m pso_pend_claim / pso_pend_reclaim / pso_pend_take): a guess no draw took is freed once its compile has
// finished and more than two submissions have passed since it was kicked; a compile still running keeps its slot however old; a recent guess stays;
// with every slot held the kick is refused and counted; a take frees its slot. CPU only: the slots' compiles are marked finished by hand (no
// pipeline is built). The old table (-DDM_TEST_PP_NO_RECLAIM, a slot freed only by a take) stays full after the stale guesses: 4 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-78s expected %-5lld → got %-5lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static pso_key key(int n) { pso_key k; memset(&k, 0, sizeof k); k.cfmt[0] = (uint32_t)n + 1; k.samples = 1; return k; }
static int claim(int n, uint64_t sub) { pso_key k = key(n); pthread_mutex_lock(&g_pp_mu); int s = pso_pend_claim(&k, sub); pthread_mutex_unlock(&g_pp_mu); return s; }
static int used(void) { int n = 0; for (int i = 0; i < PSO_PEND_MAX; i++) n += g_pp[i].used; return n; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    int ok = 1;
    for (int i = 0; i < PSO_PEND_MAX; i++) ok &= claim(i, 1) >= 0;
    EXPECT("384 guesses kicked in submission 1 all get a slot", 1, ok);
    EXPECT("the same key again: no second slot", -1, claim(0, 1));
    EXPECT("a 385th key: refused, the table is full", -1, claim(PSO_PEND_MAX, 1));
    EXPECT("... and counted", 1, (long long)g_pp_full);
    for (int i = 0; i < PSO_PEND_MAX; i++) g_pp[i].done = 1;   // every compile finished, none taken
    g_pp[7].done = 0;                                           // one still compiling
    EXPECT("submission 3 (two after the kick): nothing freed yet, refused", -1, claim(PSO_PEND_MAX, 3));
    EXPECT("... the table still full", PSO_PEND_MAX, used());
    int s = claim(PSO_PEND_MAX, 4);
    EXPECT("submission 4: the finished stale guesses freed, the new key gets a slot", 1, s >= 0);
    EXPECT("... 383 freed (the one still compiling kept)", PSO_PEND_MAX - 1, (long long)g_pp_freed);
    EXPECT("... the compiling one and the new one in the table", 2, used());
    EXPECT("the compiling slot survives any number of submissions while it runs", 1, (claim(PSO_PEND_MAX + 1, 1000), g_pp[7].used));
    g_pp[7].done = 1; claim(PSO_PEND_MAX + 2, 1001);
    EXPECT("finished, it is freed at the next kick", 0, g_pp[7].used);
    pso_key k = key(PSO_PEND_MAX + 2); g_pp[pso_pend_find(&k)].done = 1;
    EXPECT("a guess kicked in this submission is taken by its draw", 1, (pso_pend_take(&k), pso_pend_find(&k) < 0));
    EXPECT("a recent finished guess stays for the next submission's draws", 1, (claim(PSO_PEND_MAX + 3, 1002), used() >= 2));
    printf("PPRED %s %d/%d\n", fail ? "FAIL" : "OK", count - fail, count);
    return fail != 0;
} }
