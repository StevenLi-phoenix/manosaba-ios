// Symbol binding as Horizon's rtld (plan item 64h; the rules of the ISC-licensed oss-rtld reimplementation) beside ours, on synthetic modules in
// Tears of the Kingdom's profile order (main, sdk, subsdk0):
//   sdk refers to malloc, which both main and sdk define: ours sdk's own, rtld's main's (default visibility goes through the global list)
//   sdk refers to bar, its own hidden symbol, which main defines too: sdk's either way
//   main refers to foo, which sdk and subsdk0 define: ours sdk's (the profile's order), rtld's subsdk0's (main, subsdk0-9, sdk)
//   subsdk0 refers to baz, local in main and global in sdk: ours main's, rtld's sdk's (local definitions are not found)
// Three bound otherwise, counted; rtld's taken by default (DM_RTLD unset) and with DM_RTLD=1. The profile's order kept
// (-DDM_TEST_RTLD_LOAD_ORDER): 3 fail; ours the default (-DDM_TEST_RTLD_DEFAULT_OURS): 1 fails.
#include "../loader.c"

struct dm_flags g_flag;
uint64_t unimpl_stubs[4096];
void *hlecount_wrap(const char *kind, const char *name, void *function) { (void)kind; (void)name; return function; }
void *dm_hwrap(void *fn) { return fn; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

enum { GLOBAL = 0x10, LOCAL = 0x00, HIDDEN = 2 };
static const char names[] = "\0malloc\0foo\0bar\0baz";   // malloc 1, foo 8, bar 12, baz 16
static _Alignas(16) uint8_t mem[3][4096];
static Elf64_Sym sym[3][6]; static Elf64_Dyn dyn[3][3];
static void module(int i, const char *name, const Elf64_Sym *s, int ns, const int *refs, int nrefs) {   // refs: symbol indices with a GLOB_DAT each, slots at 0x200
    memcpy(sym[i], s, sizeof *s * (size_t)ns);
    Elf64_Rela *r = (Elf64_Rela *)(mem[i] + 0x300);
    for (int k = 0; k < nrefs; k++) r[k] = (Elf64_Rela){.r_offset = 0x200 + (uint64_t)k * 8, .r_info = ((uint64_t)refs[k] << 32) | R_AARCH64_GLOB_DAT};
    dyn[i][0] = (Elf64_Dyn){.d_tag = DT_RELA, .d_un.d_ptr = 0x300}; dyn[i][1] = (Elf64_Dyn){.d_tag = DT_RELASZ, .d_un.d_val = (uint64_t)nrefs * sizeof(Elf64_Rela)};
    g_mod[i] = (module_t){.base = mem[i], .size = sizeof mem[i], .symtab = sym[i], .strtab = names, .nsym = (unsigned)ns, .dyn = dyn[i]};
    snprintf(g_mod[i].name, sizeof g_mod[i].name, "%s", name);
}
static uint64_t slot(int i, int k) { uint64_t v; memcpy(&v, mem[i] + 0x200 + k * 8, 8); return v; }
static uint64_t at(int i, uint64_t off) { return (uint64_t)(uintptr_t)mem[i] + off; }

int main(void) {
    const Elf64_Sym m_main[] = {{0}, {.st_name = 1, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x10}, {.st_name = 8, .st_info = GLOBAL},
                                {.st_name = 12, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x20}, {.st_name = 16, .st_info = LOCAL, .st_shndx = 1, .st_value = 0x30}};
    const Elf64_Sym m_sdk[] = {{0}, {.st_name = 1, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x40}, {.st_name = 8, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x50},
                               {.st_name = 12, .st_info = GLOBAL, .st_other = HIDDEN, .st_shndx = 1, .st_value = 0x60}, {.st_name = 16, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x70}};
    const Elf64_Sym m_sub[] = {{0}, {.st_name = 8, .st_info = GLOBAL, .st_shndx = 1, .st_value = 0x80}, {.st_name = 16, .st_info = GLOBAL}};
    const int r_main[] = {2}, r_sdk[] = {1, 3}, r_sub[] = {2};
    for (int pass = 0; pass < 2; pass++) {
        memset(mem, 0, sizeof mem); g_nmod = 3; g_rtld = pass; g_rtld_diff = 0;
        module(0, "main", m_main, 5, r_main, 1); module(1, "sdk", m_sdk, 5, r_sdk, 2); module(2, "subsdk0", m_sub, 3, r_sub, 1);
        for (int i = 0; i < 3; i++) relocate(&g_mod[i]);
        if (!pass) {
            CHECK(slot(1, 0) == at(1, 0x40) && slot(1, 1) == at(1, 0x60) && slot(0, 0) == at(1, 0x50) && slot(2, 0) == at(0, 0x30),
                  "ours: sdk's malloc and bar, main's foo from sdk, subsdk0's baz from main's local one");
            CHECK(g_rtld_diff == 3, "three relocations bound otherwise by rtld's rules (%lu)", g_rtld_diff);
        } else {
            CHECK(slot(1, 0) == at(0, 0x10), "DM_RTLD=1: sdk's malloc is main's");
            CHECK(slot(1, 1) == at(1, 0x60), "... sdk's hidden bar stays its own");
            CHECK(slot(0, 0) == at(2, 0x80), "... main's foo is subsdk0's (main, subsdk0-9, sdk)");
            CHECK(slot(2, 0) == at(1, 0x70), "... subsdk0's baz is sdk's (main's is local)");
        }
    }
    memset(mem, 0, sizeof mem); g_rtld = -1; unsetenv("DM_RTLD");   // the default
    module(0, "main", m_main, 5, r_main, 1); module(1, "sdk", m_sdk, 5, r_sdk, 2); module(2, "subsdk0", m_sub, 3, r_sub, 1);
    for (int i = 0; i < 3; i++) relocate(&g_mod[i]);
    CHECK(g_rtld == 1 && slot(1, 0) == at(0, 0x10) && slot(0, 0) == at(2, 0x80), "DM_RTLD unset: rtld's rules (sdk's malloc main's, main's foo subsdk0's)");
    printf("t_rtld: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
