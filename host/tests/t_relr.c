// DT_RELR checked before it is applied (loader.c relr_each, plan item 144; totk-mobile's tests/test_relr.c vectors, Apache-2.0, as data; the code is
// ours): a module of 128 words whose table {16, 1 | 1 << 1 | 1 << 3, 640} relocates words 2, 3, 5 and 80 (4 slots) and leaves word 4; then tables
// that must be refused whole, each in a child (a refused table leaves word 2 as it was; "touches nothing"): a bitmap first, an address past the
// module, one at UINT64_MAX - 1, an unaligned one (12), a valid address followed by a bitmap reaching past the end, and a table outside the module.
// Without the checks (-DDM_TEST_RELR_UNCHECKED, as before) 5 fail, one of them a child that dies writing far away.
#include "../loader.c"
#include <sys/wait.h>
struct dm_flags g_flag;
uint64_t unimpl_stubs[4096];
void *hlecount_wrap(const char *kind, const char *name, void *function) { (void)kind; (void)name; return function; }
void *dm_hwrap(void *fn) { return fn; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static _Alignas(16) uint64_t image[128];
static Elf64_Dyn dyn[3];
static void table(const uint64_t *e, int ne, uint64_t at) {   // the RELR table at byte offset at of the image
    memcpy((uint8_t *)image + at, e, 8 * (size_t)ne);
    dyn[0] = (Elf64_Dyn){.d_tag = 36, .d_un.d_ptr = at}; dyn[1] = (Elf64_Dyn){.d_tag = 35, .d_un.d_val = 8 * (uint64_t)ne}; dyn[2] = (Elf64_Dyn){0};
    g_mod[0] = (module_t){.base = (uint8_t *)image, .size = sizeof image, .dyn = dyn, .name = "test"}; g_nmod = 1;
}
static int refused(const char *name, const uint64_t *e, int ne, uint64_t at) {   // in a child: refused and nothing written
    pid_t p = fork();
    if (!p) { memset(image, 0, sizeof image); image[2] = 100; table(e, ne, at); int k = 0; uint64_t bad = relr_each(&g_mod[0], relr_apply, &k); _exit(bad && image[2] == 100 && !k ? 0 : 1); }
    int st; waitpid(p, &st, 0); int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    CHECK(ok, "refused whole: %s (%s)", name, WIFEXITED(st) ? (WEXITSTATUS(st) ? "applied" : "refused") : "the child died writing far away");
    return ok;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    image[2] = 100; image[3] = 200; image[5] = 300; image[80] = 400;
    const uint64_t good[] = {16, 1 | 1ull << 1 | 1ull << 3, 640}; table(good, 3, 800);
    int k = 0; uint64_t bad = relr_each(&g_mod[0], relr_apply, &k); uint64_t b = (uint64_t)(uintptr_t)image;
    CHECK(!bad && k == 4, "totk-mobile's table: 4 slots applied (bad %llu, %d)", (unsigned long long)bad, k);
    CHECK(image[2] == b + 100 && image[3] == b + 200 && image[5] == b + 300 && image[80] == b + 400 && image[4] == 0, "words 2, 3, 5 and 80 relocated, word 4 left");
    const uint64_t first[] = {3}; refused("a bitmap before any address", first, 1, 800);
    const uint64_t past[] = {sizeof image}; refused("an address at the module's end", past, 1, 800);
    const uint64_t huge[] = {UINT64_MAX - 1}; refused("an address at UINT64_MAX - 1", huge, 1, 800);
    const uint64_t odd[] = {12}; refused("an unaligned address (12)", odd, 1, 800);
    const uint64_t tail[] = {16, 1ull << 63, 1008}; refused("a valid address, then a bitmap reaching past the end (and word 2 untouched)", tail, 3, 800);
    const uint64_t out[] = {16}; { pid_t p = fork();
        if (!p) { memset(image, 0, sizeof image); image[2] = 100; table(out, 1, 800); dyn[0].d_un.d_ptr = sizeof image + 64; int kk = 0; _exit(relr_each(&g_mod[0], relr_apply, &kk) && image[2] == 100 ? 0 : 1); }
        int st; waitpid(p, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "refused whole: a table outside the module"); }
    printf("t_relr: %d checks, %d failed\n", n, fail); return fail != 0;
}
