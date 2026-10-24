// The replay loop without a GPU (gfx/nvn/core/exec.c, plan item 264), the backend faked by recording what it is handed (gfx/nvn/nvn_backend.h).
// A list with a state command, a draw, a call, a buffer copy and a jump: the backend handed the draws, the clear, the copy and the dispatch in
// command order; a draw sees the op after it as the lookahead's start, and inside the called list the caller's next op on the return stack; the
// state command applied to R, the backend told what changed (only that it is a state command when nothing did); the work counted. FenceSync:
// the submission's fence, or for a sync the submit call fenced to its token that fence, while another token stays. An op run early is not handed
// over and leaves the skip set; the replay passing a deferred group's P end clears its mark; hoisted ops never reached are dropped at the end of
// the submission. A list that calls itself stops 64 lists deep. SetRenderTargets: new targets end the open pass; the same targets continue it,
// not a pass opened for clears only nor with clears pending; another layer goes on in a layered pass; a waiting deferred group's Q ends the pass
// and takes Q's targets. Clears: the next pass's load action with no pass open, drawn inside one or under a partial mask; a continued pass ended
// first; none while rendering is off; a depth clear with a partial stencil mask drawn. SetRenderEnable decides whether clears are dropped. A
// draw drawing nothing whatever its pass: rendering off (counted under a conditional on a pending result), no program, depth bounds that admit
// no depth (t_dbounds's -DDM_TEST_DBOUNDS_EMPTY_DRAWN fails 1 here too).
// The call itself pushed for the caller instead of the op after it (-DDM_TEST_EXEC_RET_AT_CALL, a broken variant): 1 fails. A clear inside an
// open pass taken as the next pass's load action (-DDM_TEST_EXEC_CLEAR_IN_PASS, the other): 1 fails.
#include "../nvn/core/exec.h"
#include "../nvn/nvn_backend.h"
#include "../nvn/core/capture.h"
#include "../nvn/core/cond.h"
#include "../nvn/core/defer.h"
#include "../nvn/core/pass.h"
#include "../nvn/core/state.h"
#include "../nvn/core/store_index.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ---- the backend, recording ----
static char g_log[64][24]; static int g_nlog; static int g_resets, g_last_c;
static const uint8_t *g_draw_next[8], *g_draw_ret0[8]; static int g_draw_retn[8], g_ndraw;
static void lg(const char *s) { if (g_nlog < 64) snprintf(g_log[g_nlog++], sizeof g_log[0], "%s", s); }
void nvn_be_reset(void) { g_resets++; }
void nvn_be_state(uint32_t op, const void *a, int c) { (void)op; (void)a; g_last_c = c; lg("state"); }
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) { (void)dw; if (g_ndraw < 8) { g_draw_next[g_ndraw] = g_exec_next; g_draw_retn[g_ndraw] = g_ret_n; g_draw_ret0[g_ndraw] = g_ret_n ? g_ret[0] : NULL; } g_ndraw++; lg(di ? "draw_idx" : "draw"); }
void nvn_be_dispatch(const p_dispatch *d) { (void)d; lg("dispatch"); }
void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n) { (void)src; (void)dst; (void)n; lg("copy_b2b"); }
void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n) { (void)data; (void)dst; (void)n; lg("update_ub"); }
void nvn_be_copy_b2t(const p_copy_b2t *c) { (void)c; lg("copy_b2t"); }
void nvn_be_copy_t2b(const p_copy_b2t *c) { (void)c; lg("copy_t2b"); }
void nvn_be_copy_t2t(const p_copy_t2t *c) { (void)c; lg("copy_t2t"); }
void nvn_be_clear_tex(const p_clear_tex *c) { (void)c; lg("clear_tex"); }
void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { (void)val; (void)mode; (void)v; lg("event"); }
uint64_t nvn_be_sub_fence(void) { return 42; }
static int g_mode = -1, g_layered, g_layer_cont;   // the backend's pass: its mode (-1 none), layered, another layer goes on in it
int nvn_be_pass_mode(void) { return g_mode; }
void nvn_be_pass_flush(void) { g_mode = -1; lg("flush"); }
void nvn_be_pass_end(void) { g_mode = -1; lg("end"); }
int nvn_be_layered(void) { return g_layered; }
int nvn_be_layer_continue(const p_set_rt *r) { (void)r; return g_layer_cont; }
void nvn_be_clear_quad(const p_clear_color *c) { (void)c; lg("clear_quad"); }
void nvn_be_clear_ds_quad(const p_clear_ds *c) { (void)c; lg("clear_ds_quad"); }
void nvn_be_report_counter(const uint8_t *p, const void *a) { (void)p; (void)a; lg("report"); }
void nvn_be_reset_counter(const uint8_t *p, const void *a) { (void)p; (void)a; lg("reset"); }
void nvn_be_barrier(const uint8_t *p, const void *a) { (void)p; (void)a; lg("barrier"); }
void nvn_be_set_pool(const uint8_t *p, const void *a) { (void)p; (void)a; lg("set_pool"); }
static const char *joined(void) { static char s[512]; s[0] = 0; for (int i = 0; i < g_nlog; i++) { if (i) strcat(s, " "); strcat(s, g_log[i]); } return s; }
static void reset_log(void) { g_nlog = 0; g_ndraw = 0; g_sub_work = 0; }

// ---- command lists ----
static uint8_t g_mem[4][4096] __attribute__((aligned(8))); static size_t g_len[4]; static int g_cur;
static const uint8_t *rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem[g_cur] + g_len[g_cur]; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz); g_len[g_cur] += size; return p;
}
static const uint8_t *ptr_op(uint32_t op, const void *t) { uint64_t a = (uint64_t)(uintptr_t)t; return rec(op, &a, 8); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    // list 1: texture clear, indexed draw; list 2 (jumped to): dispatch; list 0: scissor, draw, call 1, copy, jump 2
    g_cur = 1; p_clear_tex ct = {0}; rec(OP_CLEAR_TEX, &ct, sizeof ct); p_draw_idx di = {0}; rec(OP_DRAW_IDX, &di, sizeof di); rec(OP_END, NULL, 0);
    g_cur = 2; p_dispatch dp = {1, 1, 1, 0}; rec(OP_DISPATCH, &dp, sizeof dp); rec(OP_END, NULL, 0);
    g_cur = 0; p_scissor sc = {1, 2, 3, 4}; rec(OP_SCISSOR, &sc, sizeof sc); p_draw dw = {.count = 3}; const uint8_t *d0 = rec(OP_DRAW, &dw, sizeof dw);
    ptr_op(OP_CALL, g_mem[1]); uint64_t cp[3] = {0x1000, 0x2000, 16}; const uint8_t *after_call = rec(OP_COPY_B2B, cp, sizeof cp); ptr_op(OP_JUMP, g_mem[2]);
    reset_log(); nvn_exec(g_mem[0]);
    check("the first replay resets the backend's state once", 1, g_resets);
    printf("  handed over: %s\n", joined());
    check("handed over in command order: state, draw, the called list's texture clear and draw, the copy, the jumped-to dispatch", 0,
          strcmp(joined(), "state draw clear_tex draw_idx copy_b2b dispatch"));
    op_hdr h0; memcpy(&h0, d0, 8);
    check("a draw sees the op after it as the lookahead's start; NULL after it", 1, g_draw_next[0] == d0 + h0.size && g_exec_next == NULL);
    check("inside the called list the caller's next op is on the return stack [-DDM_TEST_EXEC_RET_AT_CALL]", 1, g_draw_retn[1] == 1 && g_draw_ret0[1] == after_call);
    check("... and off it after the call", 0, g_ret_n);
    check("the scissor applied to R, the backend told it is a state command", 1, R.has_sc && R.sc.w == 3 && (g_last_c & ST_STATE));
    check("the work counted: two draws, a texture clear, a copy, a dispatch", 5, g_sub_work);

    // FenceSync, the ops run early, the deferred group's P end
    static sync_t sa, sb, sc_; memset(g_len, 0, sizeof g_len); g_cur = 0;
    sync_t *pa = &sa, *pb = &sb, *pc = &sc_;
    rec(OP_FENCE_SYNC, &pa, 8); const uint8_t *cl = rec(OP_CLEAR_TEX, &ct, sizeof ct); const uint8_t *pe = rec(OP_SET_RT, &(p_set_rt){0}, sizeof(p_set_rt)); rec(OP_END, NULL, 0);
    g_hoist_skip[0] = cl; g_hoist_n = 1;
    g_defer_slots[1].active = 1; g_defer_slots[1].in_p = 1; g_defer_slots[1].p_end_op = pe; g_defer_pend = pe;
    reset_log(); nvn_exec(g_mem[0]);
    uint64_t fv = 0; check("a FenceSync record: the submission's fence", 1, sync_fence_load(&sa, &fv) && fv == 42);
    check("an op run early: not handed over, out of the skip set", 1, !strcmp(joined(), "flush") && g_hoist_n == 0);
    check("passing a deferred group's P end: its mark cleared", 1, !g_defer_slots[1].in_p && !g_defer_pend);
    memset(&g_defer_slots[1], 0, sizeof g_defer_slots[1]);
    static mtl_cap cap; cap.token = 7; g_cap_cur = &cap;
    sync_token_store(&sb, 7); sync_token_store(&sc_, 8);
    memset(g_len, 0, sizeof g_len); rec(OP_FENCE_SYNC, &pb, 8); rec(OP_FENCE_SYNC, &pc, 8); rec(OP_END, NULL, 0);
    nvn_exec(g_mem[0]); g_cap_cur = NULL;
    check("the submit call fenced it to this submission's token: the fence; another token stays", 1, sb.fence == 42 && sc_.fence == (8 | SYNC_TOKEN));

    // hoisted ops never reached; a call cycle
    memset(g_len, 0, sizeof g_len); rec(OP_END, NULL, 0); g_hoist_skip[0] = g_mem[1]; g_hoist_n = 1;
    nvn_exec(g_mem[0]); check("hoisted ops never reached: dropped at the end of the submission", 0, g_hoist_n);
    memset(g_len, 0, sizeof g_len); rec(OP_DRAW, &dw, sizeof dw); ptr_op(OP_CALL, g_mem[0]); rec(OP_END, NULL, 0);
    reset_log(); nvn_exec(g_mem[0]);
    check("a list that calls itself: followed 64 lists deep (65 draws), then not", 65, g_ndraw);
    check("... the return stack empty again", 0, g_ret_n);

    // SetRenderTargets, the clears, render enable
    static tex_t A, B; A.serial = 501; B.serial = 502; A.d.width = A.d.height = B.d.width = B.d.height = 64; A.d.levels = B.d.levels = 1; A.d.target = B.d.target = 1;
    p_set_rt ra = {0}, rb = {0}; ra.n = rb.n = 1; ra.color[0] = &A; rb.color[0] = &B;
    #define ONE(op, pl) do { memset(g_len, 0, sizeof g_len); rec(op, &(pl), sizeof(pl)); rec(OP_END, NULL, 0); reset_log(); nvn_exec(g_mem[0]); } while (0)
    R = (nvn_rt_state){0}; g_mode = -1;
    ONE(OP_SET_RT, ra); check("new targets, no pass open: the pass flushed, R takes them", 1, !strcmp(joined(), "flush") && R.has_rt && R.rt.color[0] == &A);
    g_mode = 0; g_rt_merged = 0; ONE(OP_SET_RT, ra); check("the same targets with the pass open: continued", 1, g_nlog == 0 && g_rt_merged == 1 && g_mode == 0);
    g_mode = 1; g_rt_merged = 0; ONE(OP_SET_RT, ra); check("... not a pass opened for clears only", 1, !strcmp(joined(), "flush") && !g_rt_merged);
    g_mode = 0; R.clear_c[0] = 1; ONE(OP_SET_RT, ra); check("... nor with a clear pending", 0, strcmp(joined(), "flush")); R.clear_c[0] = 0;
    g_mode = 0; g_layered = 1; g_layer_cont = 1; ONE(OP_SET_RT, rb); check("another layer of a layered pass: goes on in it", 1, g_nlog == 0 && R.rt.color[0] == &A); g_layered = g_layer_cont = 0;
    g_mode = 0; ONE(OP_SET_RT, rb); check("other targets with the pass open: it ends, R takes them", 1, !strcmp(joined(), "flush") && R.rt.color[0] == &B);
    memset(g_len, 0, sizeof g_len); const uint8_t *qop = rec(OP_SET_RT, &ra, sizeof ra); rec(OP_END, NULL, 0);
    defer_t *d = &g_defer_slots[2]; memset(d, 0, sizeof *d); d->active = 1; d->q_op = qop; d->q_rt = ra; d->q_rt.n = 1; d->lmap[0] = 0; d->clear_c[0] = 1; d->cc[0] = nvn_rgba_make(0, 1, 0, 1); g_defer_nact = 1;
    g_mode = 0; reset_log(); nvn_exec(g_mem[0]);
    check("a waiting deferred group's Q: the pass ends, Q's targets with the group's clear", 1, !strcmp(joined(), "flush") && R.rt.color[0] == &A && R.clear_c[0] && d->at_q);
    defer_release_one(d); memset(&R.clear_c, 0, sizeof R.clear_c);
    p_clear_color cfull = {0, 0xf, {1, 0, 0, 1}}, cpart = {0, 0x7, {1, 0, 0, 1}};
    g_mode = -1; ONE(OP_CLEAR_COLOR, cfull); check("a full colour clear, no pass open: the next pass's load action", 1, g_nlog == 0 && R.clear_c[0] && R.cc[0].red == 1);
    R.clear_c[0] = 0; g_mode = 0; ONE(OP_CLEAR_COLOR, cfull); check("... inside an open pass: drawn [-DDM_TEST_EXEC_CLEAR_IN_PASS]", 1, !strcmp(joined(), "clear_quad") && !R.clear_c[0]);
    g_mode = -1; ONE(OP_CLEAR_COLOR, cpart); check("three channels: drawn", 0, strcmp(joined(), "clear_quad"));
    g_mode = 0; g_rt_merged = 1; ONE(OP_CLEAR_COLOR, cfull); check("a clear in a continued pass: it ends first, the clear is the next pass's", 1, !strcmp(joined(), "end") && R.clear_c[0]); g_rt_merged = 0; R.clear_c[0] = 0;
    p_render_enable roff = {0, 0}, ron = {1, 0};
    ONE(OP_RENDER_ENABLE, roff); g_mode = -1; ONE(OP_CLEAR_COLOR, cfull); check("rendering off: the clear dropped", 1, g_nlog == 0 && !R.clear_c[0] && g_render_off);
    ONE(OP_RENDER_ENABLE, ron);
    R.rt.depth = &B; p_clear_ds dpart = {1.0f, 1, 0, 0x0f}, dfull = {0.5f, 1, 0, 0};
    ONE(OP_CLEAR_DS, dpart); check("a depth clear with a partial stencil mask: drawn", 0, strcmp(joined(), "clear_ds_quad"));
    ONE(OP_CLEAR_DS, dfull); check("a depth clear alone: the load action", 1, g_nlog == 0 && R.clear_d && R.cd == 0.5);
    static prog_t pg; S.prog = &pg; S.db_en = 0; g_render_off = 0; g_rc_on_pending = 0;
    check("a draw with a program, rendering on: not dropped", 0, exec_draw_dropped());
    g_render_off = 1; g_rc_on_pending = 1; uint64_t pd0 = g_rc_pend_draws;
    check("rendering off: dropped, counted under a conditional on a pending result", 1, exec_draw_dropped() && g_rc_pend_draws == pd0); g_render_off = 0;
    check("... with rendering on under it, the draw counted", 1, !exec_draw_dropped() && g_rc_pend_draws == pd0 + 1); g_rc_on_pending = 0;
    S.prog = NULL; check("no program: dropped", 1, exec_draw_dropped()); S.prog = &pg;
    S.db_en = 1; S.db_lo = 0.7f; S.db_hi = 0.3f; check("depth bounds that admit no depth: dropped [t_dbounds's -DDM_TEST_DBOUNDS_EMPTY_DRAWN]", 1, exec_draw_dropped());
    S.db_lo = 0.2f; S.db_hi = 0.8f; check("... bounds that admit some: not", 0, exec_draw_dropped()); S.db_en = 0;
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
