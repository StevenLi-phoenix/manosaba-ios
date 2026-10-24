// The svc gate hands svc_dispatch the svc instruction's address (hle.c gate_dispatch, plan item 125): a gate registered for an svc at offset
// 0x100 of a module returns there + 4; svc_dispatch must see the module's base + 0x100, so its log lines can name the module (the offset alone,
// as before, is in no module: every red SVC line and Break said "?"). The offset alone (-DDM_TEST_GATE_ORIG_REL): 2 fail.
#include "../runtime/loader.c"
struct dm_flags g_flag;
uint64_t unimpl_stubs[4096];
void *hlecount_wrap(const char *kind, const char *name, void *function) { (void)kind; (void)name; return function; }
void *dm_hwrap(void *fn) { return fn; }
uint64_t guest_ns(void) { return 0; }
static uint64_t g_seen_pc, g_seen_enter; static int g_seen_num;
uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr) { (void)regs; g_seen_num = num; g_seen_pc = orig_addr; return 0; }
void thr_svc_enter(uint64_t *regs, uint64_t pc) { (void)regs; g_seen_enter = pc; }
void thr_svc_exit(void) {}
uint64_t gate_dispatch(uint64_t ret_addr, uint64_t *regs);
void gate_register(uint64_t ret_abs, const char *kind, int arg, uint64_t orig);
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) {
    static _Alignas(16) uint8_t code[0x1000];
    g_mod[0] = (module_t){.base = code, .size = sizeof code, .name = "main"}; g_nmod = 1;
    gate_register((uint64_t)(uintptr_t)code + 0x104, "svc", 0x26, 0x100);
    uint64_t regs[32] = {0}; gate_dispatch((uint64_t)(uintptr_t)code + 0x104, regs);
    module_t *m = module_of((void *)(uintptr_t)g_seen_pc);
    CHECK(g_seen_num == 0x26 && g_seen_pc == (uint64_t)(uintptr_t)code + 0x100, "svc_dispatch gets the svc's address (base + 0x100), got %#llx", (unsigned long long)g_seen_pc);
    CHECK(m == &g_mod[0] && g_seen_pc - (uint64_t)(uintptr_t)m->base == 0x100, "... which module_of places: main+0x100");
    CHECK(g_seen_enter == (uint64_t)(uintptr_t)code + 0x100, "the thread's svc entry is told the same address");
    printf("t_gate_svc: %d checks, %d failed\n", n, fail); return fail != 0;
}
