// The replay loop without a GPU (gfx/nvn/core/exec.c, plan item 264), the backend faked by recording what it is handed (gfx/nvn/nvn_backend.h).
// A list with a state command, a draw, a call, a buffer copy and a jump: the backend handed the draws, the clear, the copy and the dispatch in
// command order; a draw sees the op after it as the lookahead's start, and inside the called list the caller's next op on the return stack; the
// state command applied to R, the backend told what changed (only that it is a state command when nothing did); the work counted. FenceSync:
// the submission's fence, or for a sync the submit call fenced to its token that fence, while another token stays. An op run early is not handed
// over and leaves the skip set; the replay passing a deferred group's P end clears its mark; hoisted ops never reached are dropped at the end of
// the submission. A list that calls itself stops 64 lists deep.
// The call itself pushed for the caller instead of the op after it (-DDM_TEST_EXEC_RET_AT_CALL, the broken variant): 1 fails.
#include "../nvn/core/exec.h"
#include "../nvn/nvn_backend.h"
#include "../nvn/core/capture.h"
#include "../nvn/core/defer.h"
#include "../nvn/core/pass.h"
#include "../nvn/core/state.h"
#include "../nvn/core/store_index.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

module_t g_mod[MAX_MOD];
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
int nvn_be_pool_span(uint64_t lo, uint64_t len) { (void)lo; (void)len; return 0; }
void nvn_be_arena(size_t len, uint8_t **cpu, uint64_t *gpu, void **buf) { (void)len; *cpu = NULL; *gpu = 0; *buf = NULL; }
void nvn_be_parallel(int cnt, void (*fn)(void *ctx, size_t i), void *ctx) { for (int i = 0; i < cnt; i++) fn(ctx, (size_t)i); }
uint64_t nvn_be_done_fence(void) { return 0; }
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) { (void)p; (void)rf; (void)stage; return 0; }
void nvn_be_bind_sers(const p_bind_handles *b, int nh, uint64_t *ser) { for (int k = 0; k < nh; k++) ser[k] = b->h[k]; }
uint64_t nvn_be_handle_ser(uint64_t h) { return h; }
int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v) { (void)h; (void)ser; (void)v; (void)has_v; return 0; }
void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) { (void)r2; (void)first; (void)p2map; (void)clr; (void)n1; (void)n; }

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
void nvn_be_set_rt(const uint8_t *p, const void *a) { (void)p; (void)a; lg("set_rt"); }
void nvn_be_clear_color(const uint8_t *p, const void *a) { (void)p; (void)a; lg("clear_color"); }
void nvn_be_clear_ds(const uint8_t *p, const void *a) { (void)p; (void)a; lg("clear_ds"); }
void nvn_be_render_enable(const uint8_t *p, const void *a) { (void)p; (void)a; lg("render_enable"); }
void nvn_be_report_counter(const uint8_t *p, const void *a) { (void)p; (void)a; lg("report"); }
void nvn_be_reset_counter(const uint8_t *p, const void *a) { (void)p; (void)a; lg("reset"); }
void nvn_be_barrier(const uint8_t *p, const void *a) { (void)p; (void)a; lg("barrier"); }
void nvn_be_set_pool(const uint8_t *p, const void *a) { (void)p; (void)a; lg("set_pool"); }
static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }
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
    // list 1: clear, indexed draw; list 2 (jumped to): dispatch; list 0: scissor, draw, call 1, copy, jump 2
    g_cur = 1; p_clear_color cc = {0, 0xf, {0, 0, 0, 1}}; rec(OP_CLEAR_COLOR, &cc, sizeof cc); p_draw_idx di = {0}; rec(OP_DRAW_IDX, &di, sizeof di); rec(OP_END, NULL, 0);
    g_cur = 2; p_dispatch dp = {1, 1, 1, 0}; rec(OP_DISPATCH, &dp, sizeof dp); rec(OP_END, NULL, 0);
    g_cur = 0; p_scissor sc = {1, 2, 3, 4}; rec(OP_SCISSOR, &sc, sizeof sc); p_draw dw = {.count = 3}; const uint8_t *d0 = rec(OP_DRAW, &dw, sizeof dw);
    ptr_op(OP_CALL, g_mem[1]); uint64_t cp[3] = {0x1000, 0x2000, 16}; const uint8_t *after_call = rec(OP_COPY_B2B, cp, sizeof cp); ptr_op(OP_JUMP, g_mem[2]);
    reset_log(); nvn_exec(g_mem[0]);
    check("the first replay resets the backend's state once", 1, g_resets);
    printf("  handed over: %s\n", joined());
    check("handed over in command order: state, draw, the called list's clear and draw, the copy, the jumped-to dispatch", 0,
          strcmp(joined(), "state draw clear_color draw_idx copy_b2b dispatch"));
    op_hdr h0; memcpy(&h0, d0, 8);
    check("a draw sees the op after it as the lookahead's start; NULL after it", 1, g_draw_next[0] == d0 + h0.size && g_exec_next == NULL);
    check("inside the called list the caller's next op is on the return stack [-DDM_TEST_EXEC_RET_AT_CALL]", 1, g_draw_retn[1] == 1 && g_draw_ret0[1] == after_call);
    check("... and off it after the call", 0, g_ret_n);
    check("the scissor applied to R, the backend told it is a state command", 1, R.has_sc && R.sc.w == 3 && (g_last_c & ST_STATE));
    check("the work counted: two draws, a clear, a copy, a dispatch", 5, g_sub_work);

    // FenceSync, the ops run early, the deferred group's P end
    static sync_t sa, sb, sc_; memset(g_len, 0, sizeof g_len); g_cur = 0;
    sync_t *pa = &sa, *pb = &sb, *pc = &sc_;
    rec(OP_FENCE_SYNC, &pa, 8); const uint8_t *cl = rec(OP_CLEAR_COLOR, &cc, sizeof cc); const uint8_t *pe = rec(OP_SET_RT, &(p_set_rt){0}, sizeof(p_set_rt)); rec(OP_END, NULL, 0);
    g_hoist_skip[0] = cl; g_hoist_n = 1;
    g_defer_slots[1].active = 1; g_defer_slots[1].in_p = 1; g_defer_slots[1].p_end_op = pe; g_defer_pend = pe;
    reset_log(); nvn_exec(g_mem[0]);
    uint64_t fv = 0; check("a FenceSync record: the submission's fence", 1, sync_fence_load(&sa, &fv) && fv == 42);
    check("an op run early: not handed over, out of the skip set", 1, !strcmp(joined(), "set_rt") && g_hoist_n == 0);
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
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
