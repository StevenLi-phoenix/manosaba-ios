// A streamed texture re-uploaded 512 times (plan item 179; lordran-native 2dc78cc t_tex_registry.m test_movie_stream, MIT, written anew for this
// replay): a 1920x1080 RGBA8 texture filled by the CPU, written again before each of 512 submissions that use it, as a movie's frames are. Each
// submission re-uploads it; at most three textures of it are ever alive at once (the one in use, the spare, one still on the GPU); from the 128th
// on the GPU allocation stays within 128 MB of what it was then; released at the end, none of them stays in the residency set. Released textures
// left in the residency set (-DDM_TEST_PKG_DEALLOC_KEEPS_RESIDENT): 1 fails
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
void crash_install(void);
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); crash_install();
    tex_t t = {.serial = 71003}; t.d.width = 1920; t.d.height = 1080; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); size_t size = (L.size + getpagesize() - 1) & ~((size_t)getpagesize() - 1);
    uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), size)) abort(); memset(mem, 0x55, size);
    mtl_pool_add(mem, size); nvn_dirty_add_range(mem, size);
    static pool_t pl; pl.mem = mem; pl.size = size; pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    t.size = (uint32_t)L.size; t.mem = mem; t.pool = &pl; mtl_reg_tex(259, &t, NULL);
    NSHashTable *seen = [NSHashTable weakObjectsHashTable]; uint64_t settled = 0, worst = 0, up0, re0, up, re; unsigned most = 0;
    mtl_pkg_stats(&up0, &re0);
    for (int i = 0; i < 512; i++) { @autoreleasepool {
        dm_guest_write_prepare(mem, L.size); for (size_t o = 0; o < L.size; o += 4096) mem[o] = (uint8_t)(i + o);   // a new frame
        state_reset(); mtl_sub_begin();
        pthread_mutex_lock(&g_rmu); id<MTLTexture> x = tex_by_id(259); pthread_mutex_unlock(&g_rmu);
        if (x) [seen addObject:x];
        mtl_wait(mtl_sub_commit(), 5000000000ull); up_wait_zero(&g_up_pending);
#if !DM_NVN_M3
        dispatch_sync(g_feedback_q, ^{});
#endif
        if (seen.allObjects.count > most) most = (unsigned)seen.allObjects.count;
        uint64_t a = g_dev.currentAllocatedSize;
        if (i == 127) settled = a;
        if (i >= 128 && a > worst) worst = a;
    } }
    mtl_pkg_stats(&up, &re);
    CHECK(up - up0 == 1 && re - re0 == 511, "512 submissions: uploaded once, then re-uploaded before each of the 511 others (%llu / %llu)", (unsigned long long)(up - up0), (unsigned long long)(re - re0));
    CHECK(most <= 3, "at most three of its textures alive at once (%u)", most);
    CHECK(worst <= settled + (128ull << 20), "from the 128th on the GPU allocation within 128 MB of %.0f MB (at most %.0f MB)", settled / 1048576.0, worst / 1048576.0);
    mtl_tex_release(&t); for (int k = 0; k < 20; k++) { state_reset(); mtl_sub_begin(); mtl_wait(mtl_sub_commit(), 5000000000ull); }
#if !DM_NVN_M3
    dispatch_sync(g_feedback_q, ^{});
#endif
    int resident = 0; pthread_mutex_lock(&g_res_mu); for (id o in seen.allObjects) resident += [g_res_objects containsObject:o]; pthread_mutex_unlock(&g_res_mu);
    CHECK(!resident, "released: none of its textures in the residency set (%d of %lu still alive)", resident, (unsigned long)seen.allObjects.count);
    mtl_pool_del(mem); free(mem);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
