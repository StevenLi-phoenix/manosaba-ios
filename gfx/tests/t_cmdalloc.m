// The command allocators in the memory line (nvn_mtl.m mtl_cmd_alloc_bytes / mtl_mem_stat, plan item 178, after lordran-native 24740d4, MIT): after
// a few submissions with a blit each, the main submission's allocators hold memory and the line printed (DM_MEM_STAT_ALL=1) gives them, the
// uploads' and the presents'; on Metal 3 there are none and the line has no such part. Left out of the line (-DDM_TEST_MEMLINE_NO_ALLOC): 1 fails
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    char lp[512]; snprintf(lp, sizeof lp, "%s/t_cmdalloc_%d.log", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());
    setenv("DM_MEM_STAT_ALL", "1", 1); mtl_init();
    id<MTLBuffer> a = [g_dev newBufferWithLength:1 << 20 options:MTLResourceStorageModeShared], b = [g_dev newBufferWithLength:1 << 20 options:MTLResourceStorageModeShared];
    res_add(a); res_add(b); res_commit();
    for (int i = 0; i < 4; i++) { mtl_sub_begin(); [(id)blit() copyFromBuffer:a sourceOffset:0 toBuffer:b destinationOffset:0 size:1 << 20]; mtl_wait(mtl_sub_commit(), 5000000000ull); }
    uint64_t ca[3]; mtl_cmd_alloc_bytes(ca);
    int fd = dup(2); if (!freopen(lp, "w", stderr)) return 2;
    mtl_mem_stat(300, 100, 0, 0, 0, 0); fflush(stderr); dup2(fd, 2); close(fd);
    char line[4096] = ""; FILE *f = fopen(lp, "r"); size_t k = f ? fread(line, 1, sizeof line - 1, f) : 0; line[k] = 0; if (f) fclose(f); unlink(lp);
#if !DM_NVN_M3
    CHECK(ca[0] > 0, "after four submissions the main submission's allocators hold memory (%.1f KB), uploads %.1f KB, presents %.1f KB", ca[0] / 1024.0, ca[1] / 1024.0, ca[2] / 1024.0);
    CHECK(strstr(line, "; command allocators ") && strstr(line, "for the main submission), uploads ") && strstr(line, "presents "), "the memory line gives them");
#else
    CHECK(!ca[0] && !ca[1] && !ca[2], "Metal 3: no command allocators");
    CHECK(strstr(line, "memory up to frame 300") && !strstr(line, "command allocators"), "... and the memory line has no such part");
#endif
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
