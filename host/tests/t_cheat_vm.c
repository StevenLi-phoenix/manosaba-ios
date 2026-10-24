// Atmosphère's cheat programs (host/game/cheat_vm.c, plan item 150b), on four regions of a test buffer (main, heap, alias, ASLR, 64 KiB each); every
// program written here by hand from Atmosphère's documented encodings:
//   the file format: [name] and {master} sections, words of 8 hex digits; a bad word or words before any section refused
//   0: a 4-byte and an 8-byte store; 1 / 2: if-else-end on a heap value, both ways, and a nested conditional skipped whole; 3: a loop of 4 adding 3
//   (12, stored through A); 5: a pointer chain (main holds a heap address, then its word); 6: a store through a register with the increment;
//   8: a key condition held / not held; 9: a float add (1.5 + 2.25); C0: a register against a value; C1: save and restore; C3: the host's static
//   register read, the program's written and kept to the next run; C4 r 0: once while held, again after a release
//   a read outside the regions stops the program (0) before its next store; an unknown opcode stops it
// The conditional's skip ignoring nesting (-DDM_TEST_CHEAT_FLAT_SKIP): 1 fails.
#include "../game/cheat_vm.c"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static uint8_t mem[4][0x10000];
static int in_mem(uint64_t a, unsigned sz) { for (int m = 0; m < 4; m++) { uint64_t b = (uint64_t)(uintptr_t)mem[m]; if (a >= b && a + sz <= b + sizeof mem[m]) return 1; } return 0; }
static int rd_cb(void *c, uint64_t a, void *o, unsigned sz) { (void)c; if (!in_mem(a, sz)) return 0; memcpy(o, (void *)(uintptr_t)a, sz); return 1; }
static int wr_cb(void *c, uint64_t a, const void *i, unsigned sz) { (void)c; if (!in_mem(a, sz)) return 0; memcpy((void *)(uintptr_t)a, i, sz); return 1; }
static uint64_t statics[256];
static CheatEnv env(uint64_t keys) { CheatEnv e = {.keys = keys, .statics = statics, .read = rd_cb, .write = wr_cb}; for (int m = 0; m < 4; m++) e.base[m] = (uint64_t)(uintptr_t)mem[m]; return e; }
static Cheat prog(const char *name, const uint32_t *w, unsigned k) { Cheat c = {0}; snprintf(c.name, sizeof c.name, "%s", name); memcpy(c.w, w, k * 4); c.n = k; return c; }
#define P(...) ({ static const uint32_t w_[] = {__VA_ARGS__}; prog("t", w_, sizeof w_ / 4); })
static uint32_t u32(int m, unsigned off) { uint32_t v; memcpy(&v, mem[m] + off, 4); return v; }
static uint64_t u64(int m, unsigned off) { uint64_t v; memcpy(&v, mem[m] + off, 8); return v; }
int main(void) {
    static Cheat cs[8]; char err[200];
    int k = cheat_parse("[Infinite hearts]\n04000000 00001000 00000063\n\n{Master Code}\n 04100000 00000010 00000001 \n[Notes only]\n", cs, 8, err, sizeof err);
    check("parse: 3 sections, the master marked, words read, a section without words kept", k == 3 && !cs[0].master && cs[1].master && cs[0].n == 3 && cs[0].w[2] == 0x63 && cs[1].w[0] == 0x04100000 && cs[2].n == 0 && !strcmp(cs[0].name, "Infinite hearts"));
    int b1 = cheat_parse("[x]\n0400000 1\n", cs, 8, err, sizeof err), b2 = cheat_parse("04000000 00000000 00000001\n", cs, 8, err, sizeof err);
    check("parse: a word of 7 digits, or words before any section: refused", b1 == -1 && b2 == -1);
    CheatEnv e = env(0); Cheat c;
    c = P(0x04000000, 0x00001000, 0x00000063); cheat_run(&c, &e, err, sizeof err);
    Cheat c8 = P(0x08100000, 0x00000020, 0x11223344, 0x55667788); cheat_run(&c8, &e, err, sizeof err);
    check("0: a 4-byte store at main+0x1000, an 8-byte one at heap+0x20", u32(0, 0x1000) == 0x63 && u64(1, 0x20) == 0x1122334455667788ull);
    // if heap+0x10 == 5 { main+0x40 = 1 } else { main+0x40 = 2 }
    Cheat ce = P(0x14150000, 0x00000010, 0x00000005, 0x04000000, 0x00000040, 0x00000001, 0x21000000, 0x04000000, 0x00000040, 0x00000002, 0x20000000);
    uint32_t five = 5, six = 6; memcpy(mem[1] + 0x10, &five, 4); cheat_run(&ce, &e, err, sizeof err); uint32_t r1 = u32(0, 0x40);
    memcpy(mem[1] + 0x10, &six, 4); cheat_run(&ce, &e, err, sizeof err);
    check("1 / 2: if heap value == 5 then 1 else 2: 1 for 5, 2 for 6", r1 == 1 && u32(0, 0x40) == 2);
    // if heap+0x10 == 5 { if heap+0x14 == 0 { main+0x44 = 7 } else { main+0x4c = 8 } end } end; main+0x48 = 9  (heap+0x10 is 6: the inner block,
    // its else too, skipped with it)
    Cheat cn = P(0x14150000, 0x00000010, 0x00000005, 0x14150000, 0x00000014, 0x00000000, 0x04000000, 0x00000044, 0x00000007, 0x21000000,
                 0x04000000, 0x0000004c, 0x00000008, 0x20000000, 0x20000000, 0x04000000, 0x00000048, 0x00000009);
    cheat_run(&cn, &e, err, sizeof err);
    check("a failed conditional skips a nested one whole, its else too: the store after both ends done, neither inner one", u32(0, 0x44) == 0 && u32(0, 0x4c) == 0 && u32(0, 0x48) == 9);
    // R1 = 0; loop R0 4 times { R1 += 3 }; main+0x50 = R1 (A, region 0 + 0x50)
    Cheat cl = P(0x40010000, 0x00000000, 0x00000000, 0x30000000, 0x00000004, 0x78010000, 0x00000003, 0x31000000, 0xA8100400, 0x00000050);
    int ok = cheat_run(&cl, &e, err, sizeof err);
    check("3 / 7 / A: a loop of 4 adding 3: 12 stored at main+0x50", ok && u64(0, 0x50) == 12);
    uint64_t ptr = (uint64_t)(uintptr_t)(mem[1] + 0x200); memcpy(mem[0] + 0x30, &ptr, 8); uint32_t hp = 444; memcpy(mem[1] + 0x200, &hp, 4);
    // R2 = [main+0x30] (8 bytes); R2 = [R2 + 0] (4 bytes); main+0x60 = R2 via A
    Cheat cp = P(0x58020000, 0x00000030, 0x54021000, 0x00000000, 0xA4200400, 0x00000060);
    ok = cheat_run(&cp, &e, err, sizeof err);
    check("5: a pointer chain: main holds a heap address, its word (444) loaded and stored", ok && u32(0, 0x60) == 444);
    // R3 = heap+0x300; store 0xAB (1 byte) at R3 with the increment twice
    uint64_t h3 = (uint64_t)(uintptr_t)(mem[1] + 0x300);
    static uint32_t wr6[] = {0x40030000, 0, 0, 0x61031000, 0x00000000, 0x000000AB, 0x61031000, 0x00000000, 0x000000CD};
    wr6[1] = (uint32_t)(h3 >> 32); wr6[2] = (uint32_t)h3; Cheat c6 = prog("t", wr6, 9); ok = cheat_run(&c6, &e, err, sizeof err);
    check("6: a store through a register with the increment: 0xAB then 0xCD in the next byte", ok && mem[1][0x300] == 0xAB && mem[1][0x301] == 0xCD);
    Cheat ck = P(0x80000001, 0x04000000, 0x00000070, 0x00000001, 0x20000000);
    CheatEnv held = env(1), none = env(0); cheat_run(&ck, &none, err, sizeof err); uint32_t kn = u32(0, 0x70); cheat_run(&ck, &held, err, sizeof err);
    check("8: a key condition (A): not held, no store; held, stored", kn == 0 && u32(0, 0x70) == 1);
    // R4 = 1.5f; R5 = R4 + 2.25f (9 float add, T 4); main+0x80 = R5
    Cheat cf = P(0x40040000, 0x00000000, 0x3FC00000, 0x94A54100, 0x40100000, 0xA4500400, 0x00000080);
    ok = cheat_run(&cf, &e, err, sizeof err); float fv; uint32_t fu = u32(0, 0x80); memcpy(&fv, &fu, 4);
    check("9: a float add, 1.5 + 2.25 = 3.75", ok && fv == 3.75f);
    // R6 = 10; if R6 > 7 (C0, value) { main+0x90 = 1 } end
    Cheat cc = P(0x40060000, 0x00000000, 0x0000000A, 0xC0416400, 0x00000007, 0x04000000, 0x00000090, 0x00000001, 0x20000000);
    ok = cheat_run(&cc, &e, err, sizeof err);
    check("C0: register 10 > 7: stored", ok && u32(0, 0x90) == 1);
    // R7 = 5; save R7 into saved 2; R7 = 0 (clear register); restore R7 from saved 2; main+0xA0 = R7
    Cheat cs1 = P(0x40070000, 0x00000000, 0x00000005, 0xC1020710, 0xC1070030, 0xC1070200, 0xA4700400, 0x000000A0);
    ok = cheat_run(&cs1, &e, err, sizeof err);
    check("C1: saved, the register cleared, restored: 5 stored", ok && u32(0, 0xA0) == 5);
    statics[0x05] = 77;
    // R8 = static 0x05 (the host's); main+0xB0 = R8; R9 = 123; static 0x81 = R9
    Cheat cst = P(0xC3000058, 0xA4800400, 0x000000B0, 0x40090000, 0x00000000, 0x0000007B, 0xC3000819);
    ok = cheat_run(&cst, &e, err, sizeof err);
    check("C3: the host's static register read (77); the program's written (123), kept after the run", ok && u32(0, 0xB0) == 77 && statics[0x81] == 123);
    // C4 r 0 on A: main+0xC0 += 1 each run while it fires
    Cheat c4 = P(0xC4000000, 0x00000000, 0x00000001, 0x58000000, 0x000000C0, 0x78000000, 0x00000001, 0xA8000400, 0x000000C0, 0x20000000);
    CheatEnv k4 = env(1); cheat_run(&c4, &k4, err, sizeof err); cheat_run(&c4, &k4, err, sizeof err); uint64_t once = u64(0, 0xC0);
    k4.keys = 0; cheat_run(&c4, &k4, err, sizeof err); k4.keys = 1; cheat_run(&c4, &k4, err, sizeof err);
    check("C4 r 0: once while held (two runs: 1), again after a release (2)", once == 1 && u64(0, 0xC0) == 2);
    // read 4 bytes at R10 = 16 (no region there): stops before main+0xD0 = 1
    Cheat cb = P(0x400A0000, 0x00000000, 0x00000010, 0x540A1000, 0x00000000, 0x04000000, 0x000000D0, 0x00000001);
    int st = cheat_run(&cb, &e, err, sizeof err);
    Cheat cu = P(0xB0000000); int su = cheat_run(&cu, &e, err, sizeof err);
    check("a read outside the regions stops it (0) before its next store; an unknown opcode (B) stops it", st == 0 && u32(0, 0xD0) == 0 && su == 0);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
