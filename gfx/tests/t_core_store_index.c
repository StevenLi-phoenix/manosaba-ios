// The store lookahead index without a GPU (gfx/nvn/core/store_index.c, plan item 264): synthetic command streams, the backend's two lookups faked
// (a handle is the texture serial; a program's shaders come from a table by their stage keys), the replay's order fed through sdx_seq, and the
// decision read at the operation that ends a render pass. Covered: a later read, copy or texture clear makes the write-back needed; a full clear
// after the targets are set again makes it redundant, a draw before the clear makes it needed (load); no event to the end; a clear under a
// partial scissor, a partial channel mask or a render enable that may be off is not a clear; depth and stencil apart; mip levels apart; the
// clear-only pass hoist's moved operations; a draw that samples its own attachment counts as a read; an unknown program reads every bound
// texture; a dispatch reads the compute stage only, a draw the others; jumps and calls are followed, nesting deeper than 64 loses the index;
// a replay order that does not match loses it (one red line); a texture never attached is not indexed unless added first; the SetRenderTargets /
// draw tables; more than SDX_HB / 2 attachment textures lose the index; a pass reopened without SetRenderTargets (the same targets) does not
// count as set again; the same subresource attached twice clears through its last index; a binding record out of range is still read.
// An attachment set before the render pass ended counting as set again after it (-DDM_TEST_SDX_ANY_RTSEQ, the broken variant): 2 fail.
#include "../nvn/core/store_index.h"
#include "../nvn/core/prog.h"
#include <stdio.h>
#include <string.h>

static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }

// ---- the backend's two lookups, faked ----
static nvn_refl g_rf[8];                 // shader id (key byte 0) -> reflection
static int g_rf_stage[8];
static int g_unknown;                    // nvn_be_prog_refl answers "not known"
static int g_bind_calls;
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) {
    if (g_unknown) return 0;
    for (int st = 0; st < 6; st++) { rf[st] = NULL; stage[st] = -1; }
    for (unsigned i = 0; i < p->nstage; i++) { int id = p->key[i][0]; rf[p->kstage[i]] = &g_rf[id]; stage[p->kstage[i]] = g_rf_stage[id]; }
    return 1;
}
void nvn_be_bind_sers(const p_bind_handles *b, int nh, uint64_t *ser) { g_bind_calls++; for (int k = 0; k < nh; k++) ser[k] = b->h[k]; }
// a program of one shader (id) at one stage, reading texture slot reg (kind NVN_RB_TEX) or image slot (NVN_RB_IMG)
static prog_t g_prog[8];
static const prog_t *shader(int id, int stage, int kind, int reg) {
    g_rf[id].nb = 1; g_rf[id].b[0] = (nvn_rbind){.kind = (uint8_t)kind, .reg = (uint16_t)reg}; g_rf_stage[id] = stage;
    prog_t *p = &g_prog[id]; memset(p, 0, sizeof *p); p->nstage = 1; p->kstage[0] = (uint8_t)stage; p->key[0][0] = (uint8_t)id; p->serial = 7000 + id;
    return p;
}

// ---- command streams ----
static uint8_t g_mem[4][1 << 18] __attribute__((aligned(8))); static size_t g_len[4];
static const uint8_t *g_ops[4096]; static int g_nops;   // the replay order of the main stream (for sdx_seq)
static int g_cur;                                       // the buffer being written
static const uint8_t *rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem[g_cur] + g_len[g_cur]; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz);
    g_len[g_cur] += size; if (g_cur == 0) g_ops[g_nops++] = p;
    return p;
}
static tex_t A, B, C, D;   // 64x64 2D colour textures A B C, depth-stencil D
static void reset(void) {
    memset(g_len, 0, sizeof g_len); g_nops = 0; g_cur = 0; g_unknown = 0; g_bind_calls = 0;
    tex_t *t[4] = {&A, &B, &C, &D};
    for (int i = 0; i < 4; i++) { memset(t[i], 0, sizeof *t[i]); t[i]->d.target = 1; t[i]->d.width = t[i]->d.height = 64; t[i]->d.levels = 4; t[i]->serial = 101 + (uint64_t)i; }
}
static const uint8_t *set_rt(const tex_t *c0, const tex_t *c1, const tex_t *ds, int lv) {
    p_set_rt r = {0}; r.n = c1 ? 2 : c0 ? 1 : 0; r.color[0] = c0; r.color[1] = c1; r.depth = ds;
    if (lv) { r.has_cview[0] = 1; r.cview[0].set_levels = 1; r.cview[0].base_level = (uint8_t)lv; }
    return rec(OP_SET_RT, &r, sizeof r);
}
static const uint8_t *draw(void) { p_draw d = {.count = 3}; return rec(OP_DRAW, &d, sizeof d); }
static const uint8_t *dispatch(void) { p_dispatch d = {1, 1, 1, 0}; return rec(OP_DISPATCH, &d, sizeof d); }
static const uint8_t *clear(int index, int mask) { p_clear_color c = {index, mask, {0, 0, 0, 0}}; return rec(OP_CLEAR_COLOR, &c, sizeof c); }
static const uint8_t *clear_ds(int dm, int sm) { p_clear_ds c = {1.0f, dm, 0, sm}; return rec(OP_CLEAR_DS, &c, sizeof c); }
static const uint8_t *scissor(int w, int h) { p_scissor s = {0, 0, w, h}; return rec(OP_SCISSOR, &s, sizeof s); }
static const uint8_t *render_enable(int mode) { p_render_enable r = {mode, 0}; return rec(OP_RENDER_ENABLE, &r, sizeof r); }
static const uint8_t *bind_prog(const prog_t *p, int stages) { p_bind_program b = {p, stages}; return rec(OP_BIND_PROGRAM, &b, sizeof b); }
static const uint8_t *bind_tex(int stage, int first, const tex_t *t) { p_bind_handles b = {0}; b.stage = stage; b.first = first; b.n = 1; b.h[0] = t->serial; return rec(OP_BIND_TEX, &b, sizeof b); }
static const uint8_t *copy(const tex_t *s, const tex_t *d) { p_copy_t2t c = {0}; c.src = s; c.dst = d; return rec(OP_COPY_T2T, &c, sizeof c); }
static const uint8_t *clear_tex(const tex_t *t) { p_clear_tex c = {0}; c.tex = t; return rec(OP_CLEAR_TEX, &c, sizeof c); }
static const uint8_t *call(const void *sub) { uint64_t a = (uint64_t)(uintptr_t)sub; return rec(OP_CALL, &a, 8); }
static void end(void) { rec(OP_END, NULL, 0); }

static sdx_start g_st;
static int build(void) {   // the index of buffer 0, from an empty start state
    memset(&g_st, 0, sizeof g_st);
    if (!sdx_begin(1)) return 0;
    uint64_t h = (uint64_t)(uintptr_t)g_mem[0]; sdx_build(&g_st, &h, 1); return g_sdx.ok;
}
static int g_replayed;
// The decision at the operation `at` that ends a render pass (replaying the main stream up to it), for texture t's subresource
static int decide_skip(const uint8_t *at, const tex_t *t, int lv, int depth, const uint8_t *const *skip, int nskip) {
    if (!(g_replayed && g_ops[g_replayed - 1] == at)) while (g_replayed < g_nops) { const uint8_t *p = g_ops[g_replayed++]; sdx_seq(p); if (p == at) break; }
    uint32_t P; if (!sdx_pos(at, &P)) return -1;
    return sdx_first(t->serial, (uint32_t)lv, 0, depth, P, skip, nskip);
}
static int decide(const uint8_t *at, const tex_t *t, int lv, int depth) { return decide_skip(at, t, lv, depth, NULL, 0); }
static void start_case(void) { g_replayed = 0; }
#define RED 2              /* redundant */
#define WHY(w) ((w) << 2)  /* needed, and why */

int main(void) {
    const prog_t *fs0 = shader(1, 1, NVN_RB_TEX, 0);      // fragment shader reading texture slot 0
    const prog_t *cs1 = shader(2, 5, NVN_RB_TEX, 1);      // compute shader reading texture slot 1
    const prog_t *fs3 = shader(3, 1, NVN_RB_TEX, 3);      // fragment shader reading texture slot 3
    const uint8_t *e, *x, *y;

    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); bind_prog(fs0, 2); bind_tex(1, 0, &A); draw(); end(); build();
    check("a later draw samples it: needed (1, read)", WHY(1), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); draw(); set_rt(&A, 0, 0, 0); clear(0, 0xf); draw(); end(); build();
    check("set again and cleared in full before any draw: redundant", RED, decide(e, &A, 0, 0));
    check("... the SetRenderTargets table: 3 entries, at 0 2 4", 3024, g_sdx.nrtx * 1000 + g_sdx.rtx[0] * 100 + g_sdx.rtx[1] * 10 + g_sdx.rtx[2]);
    check("... the draw table: 3 entries, at 1 3 6", 3136, g_sdx.ndrx * 1000 + g_sdx.drx[0] * 100 + g_sdx.drx[1] * 10 + g_sdx.drx[2]);
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 0); draw(); clear(0, 0xf); end(); build();
    check("set again and drawn to before the clear: needed (2, load)", WHY(2), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); draw(); end(); build();
    check("no event to the end: needed (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 0); scissor(32, 32); clear(0, 0xf); draw(); end(); build();
    check("a clear under a partial scissor is not a clear (the draw after it loads: 2)", WHY(2), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 0); scissor(64, 64); clear(0, 0xf); draw(); end(); build();
    check("... under a scissor covering the target it is: redundant", RED, decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 0); clear(0, 0x7); end(); build();
    check("a clear of three channels is not a clear: needed (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); render_enable(2); set_rt(&A, 0, 0, 0); clear(0, 0xf); end(); build();
    check("a clear while rendering may be off (conditional render) is not a clear: needed (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); render_enable(1); set_rt(&A, 0, 0, 0); clear(0, 0xf); end(); build();
    check("... with rendering on it is: redundant", RED, decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); copy(&A, &C); set_rt(&A, 0, 0, 0); clear(0, 0xf); end(); build();
    check("a copy reads it first: needed (3)", WHY(3), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); clear_tex(&A); end(); build();
    check("a texture clear: needed (4)", WHY(4), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(0, 0, &D, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(0, 0, &D, 0); clear_ds(1, 0); draw(); end(); build();
    check("depth cleared alone: the depth plane is redundant", RED, decide(e, &D, 0, 1));
    check("... the stencil plane is drawn with before any clear: needed (2)", WHY(2), decide(e, &D, 0, 2));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 1); clear(0, 0xf); end(); build();
    check("a clear of mip level 1: level 0 has no event (5)", WHY(5), decide(e, &A, 0, 0));
    check("... and level 1 is redundant", RED, decide(e, &A, 1, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); draw(); x = set_rt(&A, 0, 0, 0); y = clear(0, 0xf); draw(); end(); build();
    { const uint8_t *skip[2] = {x, y};
      check("the clear-only pass hoist moved the SetRenderTargets and the clear: no event (5)", WHY(5), decide_skip(e, &A, 0, 0, skip, 2)); }
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, 0, 0, 0); bind_prog(fs0, 2); bind_tex(1, 0, &A); draw(); end(); build();
    check("a draw that samples its own attachment: the read decides (1, not 2)", WHY(1), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); bind_prog(fs0, 2); bind_tex(1, 3, &A); draw(); end(); build();
    check("a texture on a slot the program does not read: no event (5)", WHY(5), decide(e, &A, 0, 0));
    g_unknown = 1; start_case(); build();
    check("... the same with a program the backend does not know: every bound texture is read (1)", WHY(1), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); bind_prog(fs0, 2); bind_tex(1, 0, &A); dispatch(); end(); build();
    check("a dispatch does not read the fragment stage's textures (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); bind_prog(cs1, 0x20); bind_tex(5, 1, &A); dispatch(); end(); build();
    check("... it reads the compute stage's (1)", WHY(1), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); bind_prog(fs3, 2); bind_prog(cs1, 0x20); bind_tex(1, 3, &A); draw(); end(); build();
    check("BindProgram per stage: the compute bind keeps the fragment stage, whose read counts (1)", WHY(1), decide(e, &A, 0, 0));
    reset(); start_case(); g_cur = 1; set_rt(&A, 0, 0, 0); clear(0, 0xf); end(); g_cur = 0;
    set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); call(g_mem[1]); end(); build();
    { uint32_t P; while (g_replayed < 3) sdx_seq(g_ops[g_replayed++]); int ok = sdx_pos(e, &P);
      check("a call is followed: the clear in the called list makes it redundant", RED, ok ? sdx_first(A.serial, 0, 0, 0, P, NULL, 0) : -1); }
    reset(); start_case();
    for (int i = 1; i < 4; i++) { g_cur = i; call(g_mem[i % 3 + 1]); end(); }   // 1 -> 2 -> 3 -> 1 ...: a call cycle, deeper than 64
    g_cur = 0; set_rt(&A, 0, 0, 0); call(g_mem[1]); end();
    check("calls nested deeper than 64: the index is lost", 0, build());
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); draw(); end(); build();
    { int before = g_red_once[53]; sdx_seq(g_ops[0]); sdx_seq(g_ops[2]); uint32_t P;
      check("a replay order that does not match the index loses it (sdx_pos answers 0)", 0, sdx_pos(g_ops[2], &P) || g_sdx.ok);
      check("... with one red line", 1, g_red_once[53] - before); }
    reset(); start_case(); bind_prog(fs0, 2); bind_tex(1, 0, &C); draw(); end(); build();
    check("a texture never attached is not indexed (5)", WHY(5), decide(g_ops[0], &C, 0, 0));
    start_case(); memset(&g_st, 0, sizeof g_st); sdx_begin(1); sdx_bucket(C.serial, 1);
    { uint64_t h = (uint64_t)(uintptr_t)g_mem[0]; sdx_build(&g_st, &h, 1); }
    check("... added to the candidates first (the ledger's textures), its read is (1)", WHY(1), decide(g_ops[0], &C, 0, 0));
    reset(); start_case();
    { static tex_t many[1100]; for (int i = 0; i < 1100; i++) { many[i] = A; many[i].serial = 1000 + (uint64_t)i; }
      for (int i = 0; i + 1 < 1100; i += 2) set_rt(&many[i], &many[i + 1], 0, 0); end(); }
    check("1100 attachment textures (more than SDX_HB / 2): the index is lost", 0, build());
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = copy(&B, &C); clear(0, 0xf); draw(); end(); build();
    check("a pass reopened without SetRenderTargets (its clear and draw after the copy that ended it) does not count: (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, &A, 0, 0); clear(0, 0xf); end(); build();
    check("A attached at colour 0 and 1: a clear through index 0 is not its clear (5)", WHY(5), decide(e, &A, 0, 0));
    reset(); start_case(); set_rt(&A, 0, 0, 0); draw(); e = set_rt(&B, 0, 0, 0); set_rt(&A, &A, 0, 0); clear(1, 0xf); end(); build();
    check("... through index 1, the last one, it is: redundant", RED, decide(e, &A, 0, 0));
    reset(); start_case(); { p_bind_handles b = {0}; b.stage = 1; b.first = 127; b.n = 4; b.h[0] = A.serial; rec(OP_BIND_TEX, &b, sizeof b); } end(); build();
    check("a binding record out of range (slots 127-130) is still read once (deferred handles), with no slot set", 1, g_bind_calls);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
