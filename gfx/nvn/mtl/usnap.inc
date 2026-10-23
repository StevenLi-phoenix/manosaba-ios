// gfx/nvn/mtl/usnap.inc, part of nvn_mtl.m (included there in order, one translation unit): Uniform snapshots' backend side: the pools, arena, worker threads and fences they use; dropping the copies a GPU write reaches, and what a range reads as at this point of the replay (pend_read)
// The snapshots themselves are gfx/nvn/core/usnap.c, the submit call's capture gfx/nvn/core/capture.c (plan item 264)
static inline void args_dirty_all(void);
// arena_alloc's buffer comes back unretained (plan item 185): the arena holds it until the submission completes, and an autoreleasing out parameter
// cost a retain, an autorelease and a release per call, several per draw (t_fbx ar; -DDM_TEST_ARENA_AUTORELEASING as before)
#ifndef DM_TEST_ARENA_AUTORELEASING
#define ARENA_OUT __unsafe_unretained
#else
#define ARENA_OUT __autoreleasing
#endif
static size_t arena_alloc(size_t n, ARENA_OUT id<MTLBuffer> *buf);
uint64_t mtl_done_fence(void);
int nvn_be_pool_span(uint64_t lo, uint64_t n) { void *mb; uint64_t mo; return mtl_addr(lo, n, &mb, &mo); }
void nvn_be_arena(size_t n, uint8_t **cpu, uint64_t *gpu, void **buf) {
    ARENA_OUT id<MTLBuffer> ab; size_t off = arena_alloc(n, &ab); *cpu = (uint8_t *)ab.contents + off; *gpu = ab.gpuAddress + off; *buf = (__bridge void *)ab;
}
void nvn_be_parallel(int n, void (*fn)(void *ctx, size_t i), void *ctx) { dispatch_apply_f((size_t)n, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ctx, fn); }
uint64_t nvn_be_done_fence(void) { return mtl_done_fence(); }
static void us_drop(uint64_t a, uint64_t n) { if (us_drop_range(a, n, g_fence + 1)) args_dirty_all(); }   // a GPU write of this submission reaches [a, a + n): bindings encoded from here on read memory
static int g_upd_flushing;   // upd_flush is writing the renamed inline uniform updates (g_nupd, upd_any, upd_apply: gfx/nvn/core/upd.h)
static int pend_read(uint64_t a, void *out, uint64_t n) {
    { const us_ent *u = us_find(a, n); if (u) { memcpy(out, u->cpu + (a - u->lo), n); if (upd_any()) upd_apply(a, out, n); return 1; } }   // not dropped: no GPU write of this submission reached it but renamed updates
    return pend_read_x(a, out, n, g_pend.n, 0);
}
