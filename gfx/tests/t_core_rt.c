// Render target helpers without a GPU (gfx/nvn/core/rt.h and rt.c, plan item 264): a mip level's extent (at least 1); an attachment's texture,
// level and layer from its view; whether a scissor covers a target's level (none set covers); target sets the same when copied by value, not when
// a colour view or the depth differs; one level / layer attached at two colour slots is attached once, at the slot the write mask writes, else the
// first, and other levels of one texture both.
// Colour targets compared by texture alone (-DDM_TEST_RT_SAME_NO_VIEW, t_rt_continue's broken variant): 1 fails.
#include "../nvn/core/rt.h"
#include "../nvn/core/state.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <string.h>

module_t g_mod[MAX_MOD];   // the draw state's links (state.c: programs, deferred binding records), faked as in t_core_state.c
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
int nvn_be_pool_span(uint64_t lo, uint64_t len) { (void)lo; (void)len; return 0; }
void nvn_be_arena(size_t len, uint8_t **cpu, uint64_t *gpu, void **buf) { (void)len; *cpu = NULL; *gpu = 0; *buf = NULL; }
void nvn_be_parallel(int cnt, void (*fn)(void *ctx, size_t i), void *ctx) { for (int i = 0; i < cnt; i++) fn(ctx, (size_t)i); }
uint64_t nvn_be_done_fence(void) { return 0; }
static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }
static tex_t A, B, D;
static p_set_rt rt_of(const tex_t *c0, const tex_t *ds) { p_set_rt r = {0}; r.n = c0 ? 1 : 0; r.color[0] = c0; r.depth = ds; return r; }

int main(void) {
    tex_t *t[3] = {&A, &B, &D};
    for (int i = 0; i < 3; i++) { t[i]->d.target = 1; t[i]->d.width = t[i]->d.height = 64; t[i]->d.levels = 4; t[i]->serial = 101 + (uint64_t)i; }
    st_reset();
    check("mip extents: 64 at level 2 is 16, level 7 and past 63 are 1", 1, nvn_mip_extent(64, 2) == 16 && nvn_mip_extent(64, 7) == 1 && nvn_mip_extent(64, 64) == 1);
    p_set_rt r1 = rt_of(&A, &D); r1.has_dview = 1; r1.dview.set_levels = 1; r1.dview.base_level = 1; r1.dview.set_layers = 1; r1.dview.min_layer = 3;
    uint64_t ser; int lv, sl; rt_sub(&r1, 8, &ser, &lv, &sl); int dsub = ser == D.serial && lv == 1 && sl == 3; rt_sub(&r1, 0, &ser, &lv, &sl);
    check("an attachment's subresource: the depth view's level 1 layer 3, the colour target without a view level 0", 1, dsub && ser == A.serial && lv == 0 && sl == 0);
    p_scissor sc = {0, 0, 32, 32};
    check("a 32x32 scissor covers level 1 of a 64x64 target, not level 0; no scissor covers", 1, scissor_full_of(&sc, 1, &D, &r1.dview, 1) && !scissor_full_of(&sc, 1, &A, NULL, 0) && scissor_full_of(&sc, 0, &A, NULL, 0));
    r1 = rt_of(&A, &D); p_set_rt r2 = r1; tex_t A2 = A; r2.color[0] = &A2;
    check("the same targets through a copied texture object: the same set", 1, rt_same(&r1, &r2));
    r2.has_cview[0] = 1; r2.cview[0].set_levels = 1; r2.cview[0].base_level = 1;
    check("... a colour view of another level: not [-DDM_TEST_RT_SAME_NO_VIEW]", 0, rt_same(&r1, &r2));
    r2 = r1; r2.depth = &B; check("... another depth texture: not", 0, rt_same(&r1, &r2));
    p_set_rt d = {0}; d.n = 4; d.color[0] = &B; d.color[2] = &A; d.color[3] = &A;
    S.chmask.mask = 0xf000;   // the opening draw writes slot 3 only
    check("one level attached at slots 2 and 3, slot 3 written: 2 left out, 3 kept", 1, rt_same_sub(&d, 2, 3) && rt_dup_slot(&d, 2) && !rt_dup_slot(&d, 3) && !rt_dup_slot(&d, 0));
    S.chmask.mask = 0; check("... neither written: the first kept", 1, !rt_dup_slot(&d, 2) && rt_dup_slot(&d, 3));
    d.has_cview[3] = 1; d.cview[3].set_levels = 1; d.cview[3].base_level = 2; check("... other levels of one texture: both attached", 0, rt_dup_slot(&d, 2) + rt_dup_slot(&d, 3));
    S.chmask.mask = 0xffffffffu;

    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
