// Which cheats are written into main's code at load (host/cheats.c code_cheat, plan item 150b), with main's text ending at 0x10000 and a word the
// rewriter changed at 0x2000: stores only into the code, 4 and 8 bytes aligned: written (1); one over the changed word, a 2-byte or a misaligned
// one: refused (-1); stores only into data, or a program that starts otherwise: run on the thread (0); stores into the code and elsewhere, or code
// stores then another instruction: refused (-1). The rewriter's words not checked (-DDM_TEST_CHEATS_NO_REWRITE_CHECK): 1 fails.
#include "../cheats.c"
module_t g_mod[MAX_MOD]; int g_nmod;
int dm_rewrite_touches(const char *dir, const char *module, uint64_t lo, uint64_t hi) { (void)dir; (void)module; return lo < 0x2004 && 0x2000 < hi; }
int hle_hid_last(uint64_t *b, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) { *b = 0; *lx = *ly = *rx = *ry = 0; return 0; }
void dm_guest_bases(uint64_t out[4]) { memset(out, 0, 32); }
const char *dm_main_build(void) { return "277178b7dba1b6d4949a84778d4abc58"; }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int cls(const uint32_t *w, unsigned k) { Cheat c = {0}; memcpy(c.w, w, k * 4); c.n = k; char why[160]; return code_cheat(&c, 0x10000, "x", why, sizeof why); }
#define C(...) ({ static const uint32_t w_[] = {__VA_ARGS__}; cls(w_, sizeof w_ / 4); })
int main(void) {
    check("4- and 8-byte aligned stores into the code: written (1)", C(0x04000000, 0x00001000, 0xD503201F, 0x08000000, 0x00001008, 0x11111111, 0x22222222) == 1);
    check("a store over the rewritten word at 0x2000: refused (-1)", C(0x04000000, 0x00002000, 0xD503201F) == -1);
    check("a 2-byte store, or a misaligned 4-byte one, in the code: refused (-1)", C(0x02000000, 0x00001000, 0x00001234) == -1 && C(0x04000000, 0x00001002, 0x0) == -1);
    check("stores into data only (past 0x10000): run on the thread (0)", C(0x04000000, 0x00020000, 0x00000063) == 0);
    check("a program that starts with a conditional: run on the thread (0)", C(0x14050000, 0x00020000, 0x00000001, 0x04000000, 0x00001000, 0x0, 0x20000000) == 0);
    check("stores into the code, then into data: refused (-1)", C(0x04000000, 0x00001000, 0x0, 0x04000000, 0x00020000, 0x0) == -1);
    check("stores into the code, then another instruction: refused (-1)", C(0x04000000, 0x00001000, 0x0, 0x40010000, 0x0, 0x1) == -1);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
