// The live texture at a storage address and the texture objects' host copies without a GPU (gfx/nvn/core/live.c, plan item 264). A registration
// of a texture still alive is not followed; once finalized, it follows the texture initialized at its storage when that one has the same shape
// (not one of another format or size); after its release only when the caller says so; a texture finalized at that storage leaves nothing to
// follow; finalizations are counted. Host copies: one per serial, holding the object as it was first copied; an object without a serial is its own;
// a released one is freed at its due submission, in due order, and a later copy of the serial is a new one.
// The texture living there followed whatever its shape (-DDM_TEST_LIVE_ANY_SHAPE, the broken variant): 1 fails.
#include "../nvn/core/live.h"
#include "core_test.h"
#include <stdio.h>
#include <string.h>

static uint8_t store[4][4096];
static tex_t tex(uint64_t ser, int s, int fmt) { tex_t t = {0}; t.d.target = 1; t.d.width = t.d.height = 32; t.d.levels = 1; t.d.format = fmt; t.size = 4096; t.mem = store[s]; t.serial = ser; return t; }

int main(void) {
    tex_t a = tex(10, 0, 37), out;
    mtl_tex_live(&a);
    check("a registration of a texture still alive is not followed", 0, live_follow(&a, 0, &out));
    uint64_t g0 = g_fin_gen; live_finalized(&a);
    check("a finalization is counted", 1, (long long)(g_fin_gen - g0));
    tex_t b = tex(11, 0, 37); mtl_tex_live(&b);
    check("finalized, a texture of the same shape now at its storage: followed to it", 11, live_follow(&a, 0, &out) ? (long long)out.serial : 0);
    tex_t c = tex(12, 1, 37), d = tex(13, 1, 38); mtl_tex_live(&c); live_finalized(&c); mtl_tex_live(&d);
    check("... one of another format at its storage: not followed [-DDM_TEST_LIVE_ANY_SHAPE]", 0, live_follow(&c, 0, &out));
    live_released(a.serial);
    check("after its release: not followed unless the caller says it is released", 1, !live_follow(&a, 0, &out) && live_follow(&a, 1, &out) && out.serial == 11);
    live_finalized(&b);
    check("the texture at that storage finalized too: nothing to follow", 0, live_follow(&a, 1, &out));
    tex_t e = tex(14, 2, 37), f = tex(15, 2, 37); mtl_tex_live(&e); mtl_tex_live(&f); live_finalized(&e);
    check("two textures initialized at one storage, the older finalized: it follows the newer", 15, live_follow(&e, 0, &out) ? (long long)out.serial : 0);

    uint64_t n0 = mtl_tex_snap_count();
    tex_t g = tex(20, 3, 37); const tex_t *s1 = mtl_tex_snap(&g); g.d.format = 99; const tex_t *s2 = mtl_tex_snap(&g);
    check("one host copy per serial, the object as first copied", 1, s1 == s2 && s1 != &g && s1->d.format == 37 && mtl_tex_snap_count() == n0 + 1);
    tex_t h = tex(0, 3, 37); check("an object without a serial is its own copy; NULL stays NULL", 1, mtl_tex_snap(&h) == &h && mtl_tex_snap(NULL) == NULL);
    tex_t i2 = tex(21, 3, 37); mtl_tex_snap(&i2);
    tex_snap_retire(20, 5); tex_snap_retire(21, 7);
    tex_snap_reap(4); check("released copies kept until their due submission", n0 + 2, mtl_tex_snap_count());
    tex_snap_reap(6); check("... the one due at 5 freed at 6, the one due at 7 kept", n0 + 1, mtl_tex_snap_count());
    g.d.format = 37; const tex_t *s3 = mtl_tex_snap(&g); check("a copy of a released serial made later is a new one", 1, s3 && s3->serial == 20 && mtl_tex_snap_count() == n0 + 2);
    tex_snap_reap(7); check("... the one due at 7 freed at 7", n0 + 1, mtl_tex_snap_count());
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
